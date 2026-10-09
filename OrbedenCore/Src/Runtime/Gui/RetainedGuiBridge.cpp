#include "Runtime/Gui/RetainedGuiBridge.h"
#include "Runtime/Object/Material.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "InputManager/InputManager.h"
#include "Platform/WindowsTextInput.h"
#include "Log/Log.h"
#include "Runtime/Fonts/FontRasterizer.h"
#include "Runtime/Object/Font.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/World.h"

namespace
{
    //上下文持有的一份网格内容。网格只在 UpdateMeshes 时被替换。
    struct MeshRecord
    {
        uint64 revision = 0;
        List<UIVertex> vertices;
        List<uint32> indices;
        uint64 lastUsedFrame = 0;
    };

    //一块画布的提交内容。
    struct CanvasRecord
    {
        UICanvasSubmission submission;
        List<UIDrawCommand> commands;
        List<matrix4x4> matrices;
    };

    //一个原生 UI 上下文。生命周期独立于世界：世界换了仍然复用同一个表。
    struct UIContext
    {
        uint32 generation = 1;
        uint64 worldRevision = 0;
        uint64 managedGeneration = 0;
        World* world = nullptr;

        std::unordered_map<uint64, MeshRecord> meshes;
        List<CanvasRecord> staged;
        List<CanvasRecord> published;
        //本帧命令引用到的显式材质。暂存的与已发布的分开：提交与绘制之间资源不会被回收。
        List<Ref<Material>> stagedMaterials;
        List<Ref<Material>> publishedMaterials;
        List<UIView> views;
        List<UISceneChange> changes;
        uint64 nextChangeSequence = 1;
        bool fullResync = false;
        bool frameFailed = false;
        uint64 activeFrame = 0;
        uint64 publishedFrame = 0;
    };

    //渲染系统装进来的深度读取器；未接入时回读一律失败。
    RetainedGuiFrame::DepthReader depthReader = nullptr;

    //句柄高 32 位是代次、低 32 位是槽位；槽位 0 保留为无效。
    constexpr uint32 MaxContexts = 256;

    //槽位 0 占位：句柄 0 表示失败，上下文必须从槽位 1 开始分配，否则首个句柄会被 Resolve 拒绝。
    List<std::unique_ptr<UIContext>> MakeReservedContextSlots()
    {
        List<std::unique_ptr<UIContext>> slots;
        slots.push_back(nullptr);
        return slots;
    }

    List<std::unique_ptr<UIContext>> contexts = MakeReservedContextSlots();

    //订阅世界结构变化的监听器，把通知翻译成 UISceneChange 记录。
    class SceneChangeCollector final : public IWorldLifecycleListener, public ITransformListener
    {
    public:
        explicit SceneChangeCollector(UIContext& owner) : context(owner) {}

        //队列上限；溢出后改为整表重建，不再逐条累积。
        static constexpr usize MaxPendingChanges = 65536;

        void OnEnsCreated(EnsId ens) override { Push(ens, EnsId(), 0, UISceneChangeKind::Added); }
        void OnEnsReparented(EnsId ens, EnsId parent) override { Push(ens, parent, 0, UISceneChangeKind::Reparented); }
        void OnEnsDestroyed(EnsId ens) override { Push(ens, EnsId(), 0, UISceneChangeKind::Removed); }
        void OnEnsWorldActiveChanged(EnsId ens, bool) override { Push(ens, EnsId(), 0, UISceneChangeKind::ActiveChanged); }

        //派生写入不会再回写布局，因此只登记作者写入。
        void OnTransformChanged(World& world, EnsId ens, TransformChangeSource source) override
        {
            if (source == TransformChangeSource::Derived) return;
            if (&world != context.world) return;
            Push(ens, EnsId(), 0, UISceneChangeKind::TransformChanged);
        }

    private:
        UIContext& context;

        void Push(EnsId ens, EnsId parent, int32 objectId, UISceneChangeKind kind)
        {
            if (context.fullResync) return;
            if (context.changes.size() >= MaxPendingChanges)
            {
                //溢出：丢掉已积累的记录，改为下一边界整表重建。
                context.changes.clear();
                context.fullResync = true;
                return;
            }

            UISceneChange change;
            change.sequence = context.nextChangeSequence++;
            change.worldRevision = context.worldRevision;
            change.ens = ens;
            change.parent = parent;
            change.objectId = objectId;
            change.kind = static_cast<uint32>(kind);
            change.flags = 0;
            context.changes.push_back(change);
        }
    };

    //每个上下文各持一个订阅者，随上下文创建与销毁。
    std::unordered_map<UIContext*, std::unique_ptr<SceneChangeCollector>> collectors;

    //把句柄解成上下文；代次不符视为失效。
    UIContext* Resolve(uint64 handle)
    {
        uint32 slot = static_cast<uint32>(handle & 0xFFFFFFFFull);
        uint32 generation = static_cast<uint32>(handle >> 32);
        if (slot == 0 || slot >= contexts.size()) return nullptr;

        UIContext* context = contexts[slot].get();
        if (!context || context->generation != generation) return nullptr;
        return context;
    }

    //把长度参数收敛成合法范围：负值与非正数量一律视为空。
    template<typename T>
    bool CheckSpan(const T* pointer, int32 count)
    {
        if (count < 0) return false;
        if (count == 0) return true;
        return pointer != nullptr;
    }

    //校验一块画布的命令与矩阵；长度、索引、矩阵下标与裁剪栈必须自洽。
    bool ValidateCanvas(UIContext& context, const UICanvasSubmission& submission,
        const UIDrawCommand* commands, int32 commandCount, const matrix4x4* matrices, int32 matrixCount)
    {
        if (submission.width < 0 || submission.height < 0) return false;
        if (matrixCount < 0 || (commandCount > 0 && matrixCount == 0)) return false;

        int32 clipDepth = 0;
        for (int32 index = 0; index < commandCount; ++index)
        {
            const UIDrawCommand& command = commands[index];
            if (command.matrixIndex >= static_cast<uint32>(matrixCount)) return false;

            switch (static_cast<UIDrawCommandKind>(command.commandKind))
            {
            case UIDrawCommandKind::Draw:
            {
                auto found = context.meshes.find(command.meshId);
                if (found == context.meshes.end()) return false;
                //加乘溢出与索引范围一起查：越界的片段会在渲染期读到别的网格。
                uint64 end = static_cast<uint64>(command.firstIndex) + command.indexCount;
                if (end > found->second.indices.size()) return false;
                break;
            }
            case UIDrawCommandKind::PushRectangle:
            case UIDrawCommandKind::PushImageAlpha:
                ++clipDepth;
                break;
            case UIDrawCommandKind::Pop:
                --clipDepth;
                //Pop 多于 Push 说明裁剪栈不配对。
                if (clipDepth < 0) return false;
                break;
            default:
                return false;
            }
        }
        //结束时不配对同样拒绝：半开的裁剪栈会污染后续画布。
        return clipDepth == 0;
    }

    uint64 ORBEDEN_NATIVE_CALL GuiCreateContext(uint64 worldRevision, uint64 managedGeneration)
    {
        try
        {
            uint32 slot = 0;
            for (uint32 index = 1; index < contexts.size(); ++index)
            {
                if (!contexts[index]) { slot = index; break; }
            }
            if (slot == 0)
            {
                if (contexts.size() >= MaxContexts) return 0;
                contexts.push_back(nullptr);
                slot = static_cast<uint32>(contexts.size() - 1);
            }

            std::unique_ptr<UIContext> created = std::make_unique<UIContext>();
            created->worldRevision = worldRevision;
            created->managedGeneration = managedGeneration;
            created->world = World::CurrentWorld();
            if (created->world)
            {
                auto collector = std::make_unique<SceneChangeCollector>(*created);
                created->world->AddLifecycleListener(collector.get());
                created->world->AddTransformListener(collector.get());
                collectors.emplace(created.get(), std::move(collector));
            }

            UIContext* raw = created.get();
            contexts[slot] = std::move(created);
            return (static_cast<uint64>(raw->generation) << 32) | slot;
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui CreateContext failed: ") + exception.what()).c_str());
            return 0;
        }
    }

    void ORBEDEN_NATIVE_CALL GuiDestroyContext(uint64 handle)
    {
        try
        {
            UIContext* context = Resolve(handle);
            if (!context) return;

            auto collector = collectors.find(context);
            if (collector != collectors.end())
            {
                if (context->world)
                {
                    context->world->RemoveLifecycleListener(collector->second.get());
                    context->world->RemoveTransformListener(collector->second.get());
                }
                collectors.erase(collector);
            }

            //重复销毁无副作用：代次推进后同一个句柄再也解不到东西。
            ++context->generation;
            context->meshes.clear();
            context->staged.clear();
            context->published.clear();
            context->views.clear();
            context->changes.clear();
            contexts[static_cast<uint32>(handle & 0xFFFFFFFFull)].reset();
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui DestroyContext failed: ") + exception.what()).c_str());
        }
    }

    uint8 ORBEDEN_NATIVE_CALL GuiUpdateMeshes(uint64 handle, const UIMeshUpdate* updates, int32 updateCount,
        const UIVertex* vertices, int32 vertexCount, const uint32* indices, int32 indexCount)
    {
        try
        {
            UIContext* context = Resolve(handle);
            if (!context) return 0;
            if (!CheckSpan(updates, updateCount) || !CheckSpan(vertices, vertexCount) || !CheckSpan(indices, indexCount)) return 0;

            //先整体校验再落库：半套顶点会让后续帧画到错误的内容。
            for (int32 index = 0; index < updateCount; ++index)
            {
                const UIMeshUpdate& update = updates[index];
                if (update.meshId == 0) return 0;
                uint64 vertexEnd = static_cast<uint64>(update.vertexOffset) + update.vertexCount;
                uint64 indexEnd = static_cast<uint64>(update.indexOffset) + update.indexCount;
                if (vertexEnd > static_cast<uint64>(vertexCount)) return 0;
                if (indexEnd > static_cast<uint64>(indexCount)) return 0;
            }

            for (int32 index = 0; index < updateCount; ++index)
            {
                const UIMeshUpdate& update = updates[index];
                MeshRecord& mesh = context->meshes[update.meshId];
                mesh.revision = update.revision;
                mesh.vertices.assign(vertices + update.vertexOffset, vertices + update.vertexOffset + update.vertexCount);
                mesh.indices.assign(indices + update.indexOffset, indices + update.indexOffset + update.indexCount);
            }
            return 1;
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui UpdateMeshes failed: ") + exception.what()).c_str());
            return 0;
        }
    }

    uint8 ORBEDEN_NATIVE_CALL GuiRemoveMeshes(uint64 handle, const uint64* meshIds, int32 count)
    {
        try
        {
            UIContext* context = Resolve(handle);
            if (!context) return 0;
            if (!CheckSpan(meshIds, count)) return 0;

            for (int32 index = 0; index < count; ++index) context->meshes.erase(meshIds[index]);
            return 1;
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui RemoveMeshes failed: ") + exception.what()).c_str());
            return 0;
        }
    }

    //记住本帧命令引用到的显式材质：核对类型后持有引用，绘制前不会被回收。
    void HoldFrameMaterials(UIContext& context, const List<UIDrawCommand>& commands)
    {
        for (const UIDrawCommand& command : commands)
        {
            if (command.materialObjectId == 0) continue;
            Object* object = Object::FindObjectById(static_cast<int32>(command.materialObjectId));
            Material* material = object ? object->Cast<Material>() : nullptr;
            if (!material)
            {
                //类型不对或对象已经没了：绘制时按标识解析不到就退回内置材质。
                Log::Warning("RetainedGui: 提交里的材质标识无效，本帧回退到内置 UI 材质。");
                continue;
            }

            bool held = false;
            for (const Ref<Material>& existing : context.stagedMaterials)
            {
                if (existing.Get() != material) continue;
                held = true;
                break;
            }
            if (!held) context.stagedMaterials.push_back(Ref<Material>(material));
        }
    }

    uint8 ORBEDEN_NATIVE_CALL GuiSubmitCanvas(uint64 handle, const UICanvasSubmission* submission,
        const UIDrawCommand* commands, int32 commandCount, const matrix4x4* matrices, int32 matrixCount)
    {
        try
        {
            UIContext* context = Resolve(handle);
            if (!context || !submission) return 0;
            if (!CheckSpan(commands, commandCount) || !CheckSpan(matrices, matrixCount)) return 0;

            if (!ValidateCanvas(*context, *submission, commands, commandCount, matrices, matrixCount))
            {
                //一块画布不合法就整帧作废，避免显示半帧。
                context->frameFailed = true;
                return 0;
            }

            CanvasRecord record;
            record.submission = *submission;
            if (commandCount > 0) record.commands.assign(commands, commands + commandCount);
            if (matrixCount > 0) record.matrices.assign(matrices, matrices + matrixCount);
            HoldFrameMaterials(*context, record.commands);
            context->staged.push_back(std::move(record));
            return 1;
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui SubmitCanvas failed: ") + exception.what()).c_str());
            return 0;
        }
    }

    uint8 ORBEDEN_NATIVE_CALL GuiEndFrame(uint64 handle, uint64 frameId)
    {
        try
        {
            UIContext* context = Resolve(handle);
            if (!context) return 0;

            if (context->frameFailed)
            {
                context->staged.clear();
                context->frameFailed = false;
                return 0;
            }

            //原子发布：先备好新内容，再一次性交换。
            List<CanvasRecord> staged;
            staged.swap(context->staged);
            context->published.swap(staged);
            //材质引用跟着已发布帧走：提交与绘制之间引用一直有效。
            List<Ref<Material>> stageMaterials;
            stageMaterials.swap(context->stagedMaterials);
            context->publishedMaterials.swap(stageMaterials);
            context->publishedFrame = frameId;
            return 1;
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui EndFrame failed: ") + exception.what()).c_str());
            return 0;
        }
    }

    int32 ORBEDEN_NATIVE_CALL GuiReadViews(uint64 handle, UIView* output, int32 capacity)
    {
        try
        {
            UIContext* context = Resolve(handle);
            if (!context || capacity < 0) return -1;
            int32 required = static_cast<int32>(context->views.size());
            //容量不足不部分写。
            if (output == nullptr || capacity < required) return required;
            std::copy(context->views.begin(), context->views.end(), output);
            return required;
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui ReadViews failed: ") + exception.what()).c_str());
            return -1;
        }
    }

    //输入事件在有平台采集与输入模块之前保持空集：返回 0 条、0 字节，不做任何消费。
    int32 ORBEDEN_NATIVE_CALL GuiReadInput(uint64 handle, UIInputRecord* output, int32 capacity,
        uint8* text, int32 textCapacity, int32* textBytes)
    {
        try
        {
            if (!Resolve(handle) || capacity < 0 || textBytes == nullptr) return -1;

            const std::vector<InputEvent>& events = InputManager::GetFrameEvents();
            int32 required = static_cast<int32>(events.size());
            int32 requiredBytes = 0;
            for (const InputEvent& event : events)
            {
                requiredBytes += static_cast<int32>(event.text.size());
            }
            *textBytes = requiredBytes;

            //容量不足不部分写、也不消费：调用方按返回的所需条数扩容后重来。
            if (output == nullptr || (requiredBytes > 0 && text == nullptr) || capacity < required || textCapacity < requiredBytes)
                return required;

            int32 textOffset = 0;
            for (int32 index = 0; index < required; ++index)
            {
                const InputEvent& source = events[index];
                UIInputRecord record;
                record.sequence = source.sequence;
                record.textSession = 0;
                record.timestamp = source.timestamp;
                record.windowId = source.windowId;
                record.pointerId = source.pointerId;
                record.kind = source.kind;
                record.device = source.device;
                record.key = source.key;
                record.modifiers = source.modifiers;
                record.position = source.position;
                record.delta = source.delta;
                record.value = source.value;
                record.textOffset = static_cast<uint32>(textOffset);
                record.textLength = static_cast<uint32>(source.text.size());
                record.caretScalar = source.caretScalar;
                output[index] = record;

                if (!source.text.empty())
                {
                    std::copy(source.text.begin(), source.text.end(), text + textOffset);
                    textOffset += static_cast<int32>(source.text.size());
                }
            }
            return required;
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui ReadInput failed: ") + exception.what()).c_str());
            return -1;
        }
    }

    //确认消费：被消费的按下会占有对应键，抬起或失焦时放手。
    void ORBEDEN_NATIVE_CALL GuiConsumeInput(uint64 handle, const uint64* sequences, int32 count)
    {
        if (!Resolve(handle) || count <= 0 || sequences == nullptr) return;
        for (int32 index = 0; index < count; ++index) InputManager::MarkEventHandled(sequences[index]);
    }

    uint8 ORBEDEN_NATIVE_CALL GuiApplyDerivedPositions(uint64 handle, const UIDerivedPosition* positions, int32 count)
    {
        try
        {
            UIContext* context = Resolve(handle);
            if (!context || !context->world) return 0;
            if (count < 0 || (count > 0 && positions == nullptr)) return 0;

            //持有者用上下文句柄：不同上下文写同一个 Ens 时后写的不生效。
            context->world->ApplyDerivedPositions(handle, std::span<const UIDerivedPosition>(positions, static_cast<usize>(count)));
            return 1;
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui ApplyDerivedPositions failed: ") + exception.what()).c_str());
            return 0;
        }
    }

    //深度回读需要渲染目标，随渲染阶段接入。
    uint8 ORBEDEN_NATIVE_CALL GuiReadDepth(uint64 handle, uint64 viewId, uint64 viewerId, uint64 presentedFrame,
        int32 x, int32 y, float32* depth)
    {
        if (depth) *depth = 0.0f;
        if (!Resolve(handle) || !depth) return 0;
        //只回读已经呈现的那一帧；渲染系统按画布、观察者与帧号核对。
        return depthReader && depthReader(viewId, viewerId, presentedFrame, x, y, depth) ? 1 : 0;
    }

    //按运行时 ID 取出仍然存活的字体资源。
    Font* ResolveFont(int32 fontObjectId)
    {
        if (fontObjectId == 0) return nullptr;
        Object* object = Object::FindObjectById(fontObjectId);
        return object ? object->Cast<Font>() : nullptr;
    }

    //读取指定字体版本的预烘焙载荷
    int32 ORBEDEN_NATIVE_CALL GuiReadPrebakedAtlas(uint64 handle, int32 fontObjectId, uint64 revision, uint8* output, int32 capacity)
    {
        Font* font = ResolveFont(fontObjectId);
        if (!Resolve(handle) || !font || font->GetRevision() != revision || capacity < 0) return -1;
        const List<uint8>& data = font->GetPrebakedAtlas();
        if (data.size() > static_cast<usize>(std::numeric_limits<int32>::max())) return -1;
        int32 required = static_cast<int32>(data.size());
        if (required > 0 && output && capacity >= required) std::memcpy(output, data.data(), data.size());
        return required;
    }

    //一次字形光栅化的暂存结果。像素要立刻复制出来：FontRasterizer 的缓冲会被下一次调用覆盖。
    struct GlyphBatch
    {
        List<UIGlyphResult> results;
        List<uint8> pixels;
    };

    GlyphBatch glyphBatch;

    int32 ORBEDEN_NATIVE_CALL GuiQueryGlyphs(uint64 handle, const UIGlyphRequest* requests, int32 requestCount,
        UIGlyphResult* output, int32 capacity)
    {
        try
        {
            if (!Resolve(handle) || requestCount < 0 || capacity < 0) return -1;
            if (requestCount == 0) return 0;
            //容量不足不部分写，只报出所需条数。
            if (requests == nullptr || output == nullptr || capacity < requestCount) return requestCount;

            for (int32 index = 0; index < requestCount; ++index)
            {
                UIGlyphResult result;
                result.glyphIndex = requests[index].glyphIndex;
                Font* font = ResolveFont(requests[index].fontObjectId);
                FontGlyphMetrics metrics;
                //查不到字形或字体时留下零值：缺字由托管侧统一画方框。
                if (font && FontRasterizer::GetGlyphMetrics(*font, requests[index].scalar, metrics))
                {
                    result.glyphIndex = metrics.glyphIndex;
                    result.advance = metrics.advance;
                    result.bearingX = metrics.bearingX;
                    result.bearingY = metrics.bearingY;
                    result.width = metrics.width;
                    result.height = metrics.height;
                }
                output[index] = result;
            }
            return requestCount;
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui QueryGlyphs failed: ") + exception.what()).c_str());
            return -1;
        }
    }

    int32 ORBEDEN_NATIVE_CALL GuiRasterizeGlyphs(uint64 handle, const UIGlyphRequest* requests, int32 requestCount,
        UIGlyphResult* output, int32 resultCapacity, uint8* pixels, int32 pixelCapacity)
    {
        try
        {
            if (!Resolve(handle) || requestCount < 0 || resultCapacity < 0 || pixelCapacity < 0) return -1;
            if (requestCount == 0) return 0;

            //先按请求生成全部结果并累计像素，容量确认足够之后才发布。
            glyphBatch.results.assign(static_cast<usize>(requestCount), UIGlyphResult());
            glyphBatch.pixels.clear();

            for (int32 index = 0; index < requestCount; ++index)
            {
                const UIGlyphRequest& request = requests[index];
                const FontRasterMode mode = static_cast<FontRasterMode>(request.rasterMode);
                Font* font = ResolveFont(request.fontObjectId);
                FontGlyphBitmap bitmap;
                bool rasterized = font && FontRasterizer::RasterizeGlyph(*font, request.scalar, mode, request.pixelSize, bitmap);

                if (rasterized)
                {
                    //托管侧已经知道像素尺寸，这里只回填布局数据与像素偏移。
                    glyphBatch.pixels.insert(glyphBatch.pixels.end(), bitmap.pixels, bitmap.pixels + bitmap.byteCount);
                }
                else
                {
                    //空轮廓（空格）与缺字都不产出像素，只回度量。
                    FontGlyphMetrics metrics;
                    if (font) FontRasterizer::GetGlyphMetrics(*font, request.scalar, metrics);
                }

                UIGlyphResult result;
                result.bitmapWidth = rasterized ? bitmap.width : 0;
                result.bitmapHeight = rasterized ? bitmap.height : 0;
                result.channels = rasterized ? bitmap.channels : 0;
                result.rowStride = rasterized ? bitmap.rowStride : 0;
                result.originX = rasterized ? static_cast<float32>(bitmap.originX) : 0.0f;
                result.originY = rasterized ? static_cast<float32>(bitmap.originY) : 0.0f;
                result.byteOffset = rasterized
                    ? static_cast<uint32>(glyphBatch.pixels.size() - bitmap.byteCount)
                    : 0;
                result.byteCount = rasterized ? static_cast<uint32>(bitmap.byteCount) : 0;
                glyphBatch.results[index] = result;
            }

            int32 requiredBytes = static_cast<int32>(glyphBatch.pixels.size());
            //容量不足两样都不写，返回所需字节数。
            if (output == nullptr || pixels == nullptr
                || resultCapacity < requestCount || pixelCapacity < requiredBytes)
            {
                return requiredBytes;
            }

            std::copy(glyphBatch.results.begin(), glyphBatch.results.end(), output);
            std::copy(glyphBatch.pixels.begin(), glyphBatch.pixels.end(), pixels);
            return requiredBytes;
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui RasterizeGlyphs failed: ") + exception.what()).c_str());
            return -1;
        }
    }

    float32 ORBEDEN_NATIVE_CALL GuiGetKerning(int32 fontObjectId, uint32 leftGlyph, uint32 rightGlyph)
    {
        try
        {
            Font* font = ResolveFont(fontObjectId);
            return font ? FontRasterizer::GetKerning(*font, leftGlyph, rightGlyph) : 0.0f;
        }
        catch (const std::exception&)
        {
            return 0.0f;
        }
    }

    //文本输入会话：会话标识随事件回传，上层据此丢弃离焦后的旧提交。
    void ORBEDEN_NATIVE_CALL GuiSetTextInput(uint64 handle, uint64 token, uint8 active, int32 x, int32 y, int32 width, int32 height)
    {
        if (!Resolve(handle)) return;
        WindowsTextInput& input = WindowsTextInputDetail::Instance();
        input.SetTextSession(token);
        input.SetCaretRect(x, y, width, height);
        input.SetActive(active != 0);
        if (active == 0) input.CancelComposition();
    }

    //剪贴板：读取返回 UTF-8 字节数，容量不足不部分写。
    int32 ORBEDEN_NATIVE_CALL GuiReadClipboard(uint8* output, int32 capacity)
    {
        try
        {
            if (capacity < 0) return -1;
            std::string text;
            if (!WindowsTextInputDetail::Instance().ReadClipboard(text)) return 0;
            int32 required = static_cast<int32>(text.size());
            if (output == nullptr || capacity < required) return required;
            std::copy(text.begin(), text.end(), output);
            return required;
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui ReadClipboard failed: ") + exception.what()).c_str());
            return -1;
        }
    }

    uint8 ORBEDEN_NATIVE_CALL GuiWriteClipboard(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return 0;
        return WindowsTextInputDetail::Instance().WriteClipboard(std::string(reinterpret_cast<const char*>(text), static_cast<usize>(length))) ? 1 : 0;
    }

    int32 ORBEDEN_NATIVE_CALL GuiReadChanges(uint64 handle, UISceneChange* output, int32 capacity, uint8* fullResync)
    {
        try
        {
            UIContext* context = Resolve(handle);
            if (!context || capacity < 0 || fullResync == nullptr) return -1;

            *fullResync = context->fullResync ? 1 : 0;
            int32 required = static_cast<int32>(context->changes.size());
            //空缓冲只查询数量，不排空。
            if (output == nullptr)
            {
                if (capacity >= required) context->changes.clear();
                return required;
            }
            if (capacity < required) return required;

            std::copy(context->changes.begin(), context->changes.end(), output);
            //复制成功才确认排空；整表重建请求同样在这一刻清除。
            context->changes.clear();
            context->fullResync = false;
            return required;
        }
        catch (const std::exception& exception)
        {
            Log::Error((std::string("RetainedGui ReadChanges failed: ") + exception.what()).c_str());
            return -1;
        }
    }

    //读取主显示目标的像素尺寸；渲染器还没报过尺寸时返回 0。
    uint8 ORBEDEN_NATIVE_CALL GuiReadDisplaySize(int32* width, int32* height)
    {
        if (!width || !height) return 0;
        *width = 0;
        *height = 0;
        int32 reportedWidth = 0;
        int32 reportedHeight = 0;
        if (!RetainedGuiFrame::GetDisplaySize(reportedWidth, reportedHeight)) return 0;
        *width = reportedWidth;
        *height = reportedHeight;
        return 1;
    }
}

const RetainedGuiApi* GetRetainedGuiApiTable()
{
    //进程期只构造一次：托管侧拿到的是稳定地址，不随世界切换变化。
    static const RetainedGuiApi table = RetainedGuiApi::Create();
    return &table;
}

//主显示目标的像素尺寸。渲染器每帧报一次，托管侧据此给屏幕画布做首帧视口引导。
namespace
{
    int32 displayPixelWidth = 0;
    int32 displayPixelHeight = 0;
}

void RetainedGuiFrame::SetDisplaySize(int32 width, int32 height)
{
    if (width < 0 || height < 0) return;
    displayPixelWidth = width;
    displayPixelHeight = height;
}

bool RetainedGuiFrame::GetDisplaySize(int32& width, int32& height)
{
    width = displayPixelWidth;
    height = displayPixelHeight;
    return displayPixelWidth > 0 && displayPixelHeight > 0;
}

void RetainedGuiFrame::BeginViewPublish()
{
    for (const std::unique_ptr<UIContext>& context : contexts)
    {
        if (context) context->views.clear();
    }
}

void RetainedGuiFrame::PublishView(uint64 canvasId, int32 width, int32 height, uint32 flags,
    const vector2& logicalOrigin, const vector2& logicalSize,
    const matrix4x4& viewMatrix, const matrix4x4& projectionMatrix, uint64 viewerId)
{
    for (const std::unique_ptr<UIContext>& context : contexts)
    {
        if (!context) continue;
        UIView view;
        view.viewId = canvasId;
        view.presentedFrame = context->publishedFrame;
        view.viewerId = viewerId;
        view.width = width;
        view.height = height;
        view.logicalOrigin = logicalOrigin;
        view.logicalSize = logicalSize;
        view.view = viewMatrix;
        view.projection = projectionMatrix;
        view.flags = flags;
        context->views.push_back(view);
    }
}

void RetainedGuiFrame::SetDepthReader(DepthReader reader)
{
    depthReader = reader;
}

uint64 RetainedGuiFrame::GetPublishedFrame()
{
    //同时只应有一个上下文在发布；取最大的已发布帧号作为本帧内容。
    uint64 published = 0;
    for (const std::unique_ptr<UIContext>& context : contexts)
    {
        if (context && context->publishedFrame > published) published = context->publishedFrame;
    }
    return published;
}

int32 RetainedGuiFrame::CollectCanvases(List<CanvasView>& out)
{
    out.clear();
    for (const std::unique_ptr<UIContext>& context : contexts)
    {
        if (!context || context->publishedFrame == 0) continue;
        for (const CanvasRecord& record : context->published)
        {
            CanvasView view;
            view.submission = &record.submission;
            view.commands = record.commands.data();
            view.commandCount = static_cast<int32>(record.commands.size());
            view.matrices = record.matrices.data();
            view.matrixCount = static_cast<int32>(record.matrices.size());
            out.push_back(view);
        }
    }
    return static_cast<int32>(out.size());
}

bool RetainedGuiFrame::GetMesh(uint64 meshId, uint64& revision, const UIVertex*& vertices, int32& vertexCount,
    const uint32*& indices, int32& indexCount)
{
    for (const std::unique_ptr<UIContext>& context : contexts)
    {
        if (!context) continue;
        auto found = context->meshes.find(meshId);
        if (found == context->meshes.end()) continue;

        revision = found->second.revision;
        vertices = found->second.vertices.data();
        vertexCount = static_cast<int32>(found->second.vertices.size());
        indices = found->second.indices.data();
        indexCount = static_cast<int32>(found->second.indices.size());
        return true;
    }
    return false;
}

RetainedGuiApi RetainedGuiApi::Create()
{
    RetainedGuiApi api;
    api.CreateContext = reinterpret_cast<void*>(&GuiCreateContext);
    api.DestroyContext = reinterpret_cast<void*>(&GuiDestroyContext);
    api.UpdateMeshes = reinterpret_cast<void*>(&GuiUpdateMeshes);
    api.RemoveMeshes = reinterpret_cast<void*>(&GuiRemoveMeshes);
    api.SubmitCanvas = reinterpret_cast<void*>(&GuiSubmitCanvas);
    api.EndFrame = reinterpret_cast<void*>(&GuiEndFrame);
    api.ReadViews = reinterpret_cast<void*>(&GuiReadViews);
    api.ReadInput = reinterpret_cast<void*>(&GuiReadInput);
    api.ConsumeInput = reinterpret_cast<void*>(&GuiConsumeInput);
    api.ApplyDerivedPositions = reinterpret_cast<void*>(&GuiApplyDerivedPositions);
    api.ReadDepth = reinterpret_cast<void*>(&GuiReadDepth);
    api.QueryGlyphs = reinterpret_cast<void*>(&GuiQueryGlyphs);
    api.RasterizeGlyphs = reinterpret_cast<void*>(&GuiRasterizeGlyphs);
    api.GetKerning = reinterpret_cast<void*>(&GuiGetKerning);
    api.SetTextInput = reinterpret_cast<void*>(&GuiSetTextInput);
    api.ReadClipboard = reinterpret_cast<void*>(&GuiReadClipboard);
    api.WriteClipboard = reinterpret_cast<void*>(&GuiWriteClipboard);
    api.ReadChanges = reinterpret_cast<void*>(&GuiReadChanges);
    api.ReadDisplaySize = reinterpret_cast<void*>(&GuiReadDisplaySize);
    api.ReadPrebakedAtlas = reinterpret_cast<void*>(&GuiReadPrebakedAtlas);
    return api;
}
