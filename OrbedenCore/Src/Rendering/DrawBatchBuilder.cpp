#include "Rendering/DrawBatchBuilder.h"

#include "Log/Log.h"
#include "Rendering/ColorSpace.h"
#include "Rendering/GeometryExpander.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/RenderMath.h"
#include "Rendering/ParticleRenderer.h"
#include "Rendering/StaticBatchCache.h"
#include "Rendering/RenderScene.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/World.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    //动态批的容量上限：单项引用顶点、整批引用顶点、整批索引
    constexpr uint32 MaximumDynamicBatchItemVertices = 1024u;
    constexpr uint32 MaximumDynamicBatchVertices = 4096u;
    constexpr uint32 MaximumDynamicBatchIndices = 12288u;

    //策略是否允许走实例批与动态批
    bool AllowsInstancing(DrawStrategy strategy)
    {
        return strategy == DrawStrategy::Auto || strategy == DrawStrategy::GpuInstancing;
    }

    bool AllowsDynamicBatching(DrawStrategy strategy)
    {
        return strategy == DrawStrategy::Auto || strategy == DrawStrategy::DynamicBatching;
    }

    //几何模式的诊断名称，用在「没能合批」的报告里
    const char* GetBatchingModeName(GeometryMode mode)
    {
        if (mode == GeometryMode::Instanced) return "instanced";
        if (mode == GeometryMode::Expanded) return "dynamic";
        if (mode == GeometryMode::TrailInstanced) return "trail instanced";
        return "per-object";
    }

    //非有限距离排到最后，与既有排序器一致
    constexpr float32 SortableDistanceLimit = std::numeric_limits<float32>::max();

    float32 GetSortableDistance(float32 value)
    {
        return std::isfinite(value) ? value : SortableDistanceLimit;
    }

    //判断矩阵元素是否全部有限
    bool IsFiniteMatrix(const matrix4x4& value)
    {
        for (uint32 index = 0; index < 16; ++index)
        {
            if (!std::isfinite(value.m[index])) return false;
        }

        return true;
    }

    //稳定顺序键比较：来源、所属 Ens、来源对象、元素编号、子网格
    bool StableKeyLess(const DrawItem& a, const DrawItem& b)
    {
        if (a.source != b.source) return static_cast<uint32>(a.source) < static_cast<uint32>(b.source);
        if (a.owner.id != b.owner.id) return a.owner.id < b.owner.id;
        if (a.owner.version != b.owner.version) return a.owner.version < b.owner.version;
        if (a.sourceObjectId != b.sourceObjectId) return a.sourceObjectId < b.sourceObjectId;
        if (a.elementId != b.elementId) return a.elementId < b.elementId;
        return a.subMeshIndex < b.subMeshIndex;
    }

    //批次内容比较：除几何模式与 program 之外的键字段
    bool SameBatchContent(const DrawItem& a, const DrawItem& b)
    {
        return a.queue == b.queue
            && a.source == b.source
            && a.geometry == b.geometry
            && a.mode == b.mode
            && a.blendMode == b.blendMode
            && a.mesh == b.mesh
            && a.material == b.material
            && a.subMeshIndex == b.subMeshIndex
            && a.indexStart == b.indexStart
            && a.indexCount == b.indexCount
            && a.receiveShadows == b.receiveShadows;
    }

    //批次分组用的键内容排序：队列、材质、网格与几何区间，再加绘制状态。
    //不含对象身份——键里带上 owner 或 elementId，分组粒度就退化成一个对象一项，
    //同键项永远凑不满两项，静态自动合批整个不生效。稳定键只用于同键项之间的排序。
    bool KeyContentLess(const DrawItem& a, const DrawItem& b)
    {
        if (a.queue != b.queue) return static_cast<uint32>(a.queue) < static_cast<uint32>(b.queue);
        int32 materialA = a.material ? a.material->GetObjectId() : 0;
        int32 materialB = b.material ? b.material->GetObjectId() : 0;
        if (materialA != materialB) return materialA < materialB;
        int32 meshA = a.mesh ? a.mesh->GetObjectId() : 0;
        int32 meshB = b.mesh ? b.mesh->GetObjectId() : 0;
        if (meshA != meshB) return meshA < meshB;
        if (a.subMeshIndex != b.subMeshIndex) return a.subMeshIndex < b.subMeshIndex;
        if (a.indexStart != b.indexStart) return a.indexStart < b.indexStart;
        if (a.indexCount != b.indexCount) return a.indexCount < b.indexCount;
        if (a.blendMode != b.blendMode) return static_cast<uint32>(a.blendMode) < static_cast<uint32>(b.blendMode);
        if (a.receiveShadows != b.receiveShadows) return a.receiveShadows < b.receiveShadows;
        if (a.geometry != b.geometry) return static_cast<uint32>(a.geometry) < static_cast<uint32>(b.geometry);
        if (a.mode != b.mode) return static_cast<uint32>(a.mode) < static_cast<uint32>(b.mode);
        return false;
    }

    //键内容相等：排序后同键项必然相邻，据此切组
    bool KeyContentEqual(const DrawItem& a, const DrawItem& b)
    {
        return !KeyContentLess(a, b) && !KeyContentLess(b, a);
    }

    //动态批的内容比较：不要求同网格，只要求同材质、队列、几何种类与最终渲染状态
    bool SameDynamicContent(const DrawItem& a, const DrawItem& b)
    {
        return a.queue == b.queue
            && a.source == b.source
            && a.geometry == b.geometry
            && a.blendMode == b.blendMode
            && a.material == b.material
            && a.receiveShadows == b.receiveShadows;
    }

    //一段连续项是否都允许走实例批
    bool AllAllowInstancing(const List<DrawItem>& items, usize begin, usize end)
    {
        for (usize index = begin; index < end; ++index)
        {
            if (!AllowsInstancing(items[index].strategy)) return false;
        }

        return true;
    }

    //一段连续项是否都允许走动态批
    bool AllAllowDynamicBatching(const List<DrawItem>& items, usize begin, usize end)
    {
        for (usize index = begin; index < end; ++index)
        {
            if (!AllowsDynamicBatching(items[index].strategy)) return false;
        }

        return true;
    }

    //按预算收编连续的同材质项，返回动态批的结束下标。
    //单项超预算不参与；整批达到任一上限就结束，由调用方在下一个下标重新开始。
    usize CollectDynamicBatch(const List<DrawItem>& items, usize begin, bool allowed)
    {
        if (!allowed) return begin + 1;

        usize end = begin;
        uint64 vertices = 0;
        uint64 indices = 0;
        while (end < items.size())
        {
            const DrawItem& item = items[end];
            if (item.geometry != DrawGeometry::Mesh || !item.mesh) break;
            if (end > begin && (!SameDynamicContent(items[begin], item) || !AllowsDynamicBatching(item.strategy))) break;

            uint32 itemVertices = GeometryExpander::CountReferencedVertices(*item.mesh, item.indexStart, item.indexCount);
            if (itemVertices == 0 || itemVertices > MaximumDynamicBatchItemVertices) break;
            if (end > begin && (vertices + itemVertices > MaximumDynamicBatchVertices ||
                indices + item.indexCount > MaximumDynamicBatchIndices)) break;

            vertices += itemVertices;
            indices += item.indexCount;
            ++end;
        }

        return end;
    }
}

bool DrawBatchKey::operator==(const DrawBatchKey& other) const
{
    return queue == other.queue
        && mode == other.mode
        && geometry == other.geometry
        && blendMode == other.blendMode
        && meshObjectId == other.meshObjectId
        && subMeshIndex == other.subMeshIndex
        && indexStart == other.indexStart
        && indexCount == other.indexCount
        && materialObjectId == other.materialObjectId
        && programId == other.programId
        && depthTest == other.depthTest
        && depthWrite == other.depthWrite
        && blend == other.blend
        && cull == other.cull
        && receiveShadows == other.receiveShadows;
}

void DrawBatchBuilder::Initialize(GpuResourceManager* resources, bool instancing, ParticleRenderer* particles,
    StaticBatchCache* staticCache)
{
    gpuResources = resources;
    instancingAvailable = instancing;
    particleRenderer = particles;
    staticBatches = staticCache;
}

void DrawBatchBuilder::Clear()
{
}

void DrawBatchBuilder::InvalidateReports()
{
    reportedMissingGeometryAbi.clear();
}

//上报一次「这个 Pass 没有接入几何 ABI，因此没能按指定模式合批」。
//去重键是来源 ObjectId 与几何模式的合成，同一 Shader 的同一模式只报一条，避免每帧刷屏。
void DrawBatchBuilder::ReportMissingGeometryAbi(const GpuShader* shader, const GpuShaderPass& pass, GeometryMode mode, const DrawItem& first)
{
    int32 sourceId = (shader && shader->source) ? shader->source->GetObjectId() : 0;
    uint64 key = (static_cast<uint64>(static_cast<uint32>(sourceId)) << 8) | static_cast<uint64>(static_cast<uint32>(mode));
    if (!reportedMissingGeometryAbi.insert(key).second) return;

    std::string message = "Shader pass is not wired to the geometry interface, so the ";
    message += GetBatchingModeName(mode);
    message += " batch was drawn per object instead. Shader: ";
    message += (shader && shader->source) ? shader->source->name : std::string("<unknown>");
    message += ", pass: ";
    message += pass.name;
    if (first.material)
    {
        message += ", material: ";
        message += first.material->name;
    }
    message += ". Include \"Builtin/geometry_input.orbinc\" and declare '--------geometry Standard' on that pass "
        "to use instancing and dynamic batching; a pass without the geometry interface never receives the "
        "per-instance world matrix, so batched draws would collapse.";
    Log::Warning(message.c_str());
}

//把当前相机的可见静态项展开为统一绘制项
void DrawBatchBuilder::BuildCameraItems(const RenderScene& scene, const VisibleSet& visibleSet,
    const List<InstanceSubmission>& submissions, const ParticleFrameSnapshot& particles, List<DrawItem>& items)
{
    items.clear();
    if (!gpuResources) return;

    const RenderCamera& camera = visibleSet.camera;
    for (const RenderItem& source : visibleSet.renderItems)
    {
        StaticMeshRenderer* renderer = source.renderer;
        if (!renderer || !source.mesh || !source.material) continue;
        //drawLayer 在剔除前已经按渲染器过滤过一次，这里覆盖子网格声明的层
        if ((source.drawLayer & camera.drawLayerMask) == 0) continue;
        if (!source.worldBounds.valid) continue;
        if (!IsFiniteMatrix(source.localToWorld)) continue;

        const GpuMaterial* material = gpuResources->GetMaterial(source.material);
        if (!material || !material->shader || material->shader->passes.empty())
        {
            Log::Error("DrawBatchBuilder skipped a draw item: the material GPU resources are invalid.");
            continue;
        }

        if (staticBatches && staticBatches->ContainsRenderer(renderer->GetObjectId())) continue;

        DrawItem item;
        item.source = DrawSource::StaticMesh;
        item.geometry = DrawGeometry::Mesh;
        item.owner = source.ens;
        item.sourceObjectId = renderer->GetObjectId();
        item.elementId = 0;
        item.subMeshIndex = source.subMeshIndex;
        item.indexStart = source.indexStart;
        item.indexCount = source.indexCount;
        item.drawLayer = source.drawLayer;
        item.mesh = source.mesh;
        item.material = source.material;
        item.queue = source.drawQueue;
        item.mode = GeometryMode::Uniform;
        item.blendMode = BlendMode::Alpha;
        item.model = source.localToWorld;
        item.worldBounds = source.worldBounds;
        item.cameraDistance = source.cameraDistance;
        item.castShadows = source.castShadows;
        item.receiveShadows = source.receiveShadows;
        //指定只走单绘制的渲染器始终是屏障项，前后不合并
        item.strategy = renderer->drawStrategy;

        //重排只允许单 Pass Standard、且最终状态为深度测试开、深度写开、混合关的不透明项
        const GpuShaderPass& pass = material->shader->passes[0];
        bool alphaBlended = item.queue != DrawQueue::Opaque;
        bool resolvedDepthTest = pass.state.depthTest != ShaderPassToggle::Off;
        bool resolvedDepthWrite = pass.state.depthWrite == ShaderPassToggle::On ||
            (pass.state.depthWrite == ShaderPassToggle::Auto && !alphaBlended);
        bool resolvedBlend = pass.state.blend == ShaderPassToggle::On ||
            (pass.state.blend == ShaderPassToggle::Auto && alphaBlended);
        item.reorderable = item.strategy != DrawStrategy::Individual
            && item.queue == DrawQueue::Opaque
            && material->shader->passes.size() == 1
            && pass.geometryContract == ShaderGeometryContract::Standard
            && resolvedDepthTest && resolvedDepthWrite && !resolvedBlend;

        items.push_back(item);
    }

    //显式实例提交：逐实例展开，引用与内容 revision 在消费时重新验证
    for (const InstanceSubmission& submission : submissions)
    {
        //Ref 是软引用，不阻止销毁，使用前必须重新解析
        Mesh* mesh = submission.mesh.Get();
        Material* material = submission.material.Get();
        if (!mesh || !material)
        {
            Log::Error("DrawBatchBuilder skipped an instance submission: its mesh or material was destroyed.");
            continue;
        }
        if (submission.world && submission.world->GetContentRevision() != submission.contentRevision) continue;
        //指定相机的提交只参与该相机；空句柄表示参与全部相机
        if (!submission.options.camera.IsNull() && submission.options.camera != camera.ens) continue;
        if ((submission.options.drawLayer & camera.drawLayerMask) == 0) continue;
        if (submission.subMeshIndex >= mesh->subMeshes.size()) continue;

        const GpuMaterial* gpuMaterial = gpuResources->GetMaterial(material);
        if (!gpuMaterial || !gpuMaterial->shader || gpuMaterial->shader->passes.size() != 1) continue;

        const SubMesh& subMesh = mesh->subMeshes[submission.subMeshIndex];
        usize start = static_cast<usize>(subMesh.indexStart);
        usize count = static_cast<usize>(subMesh.indexCount);
        if (count == 0 || start > mesh->indices.size() || count > mesh->indices.size() - start) continue;

        const bounds3& localBounds = mesh->GetLocalBounds();
        DrawQueue queue = material->GetDrawQueue();
        for (usize index = 0; index < submission.instances.size(); ++index)
        {
            const MeshInstanceData& instance = submission.instances[index];
            matrix4x4 model = RenderMath::TRS(instance.position, instance.rotation, instance.scale);
            if (!IsFiniteMatrix(model)) continue;

            DrawItem item;
            item.source = DrawSource::ExplicitInstance;
            item.geometry = DrawGeometry::Mesh;
            item.sourceObjectId = submission.sourceObjectId;
            //提交序号在高 32 位、实例序号在低 32 位，跨帧可稳定排序
            item.elementId = (submission.submissionId << 32) | static_cast<uint64>(index);
            item.subMeshIndex = submission.subMeshIndex;
            item.indexStart = subMesh.indexStart;
            item.indexCount = subMesh.indexCount;
            item.drawLayer = submission.options.drawLayer;
            item.mesh = mesh;
            item.material = material;
            item.queue = queue;
            //显式提交即使只有一个实例也走实例绘制，不悄悄改成普通绘制
            item.mode = GeometryMode::Instanced;
            item.blendMode = BlendMode::Alpha;
            item.model = model;
            item.worldBounds = localBounds.valid ? RenderMath::TransformBounds(model, localBounds) : bounds3();
            if (item.worldBounds.valid && !RenderMath::Intersects(camera.viewFrustum, item.worldBounds)) continue;
            //距离度量与剔除保持一致：相机到对象位置的距离平方
            vector3 center = item.worldBounds.valid ? item.worldBounds.center : instance.position;
            vector3 toItem = { center.x - camera.position.x, center.y - camera.position.y, center.z - camera.position.z };
            item.cameraDistance = RenderMath::Dot(toItem, toItem);
            //输入的 tint 是 sRGB，Alpha 原样；管线内统一用线性值
            item.linearTint = ColorSpace::SrgbToLinear(instance.tint);
            item.linearTint.a = instance.tint.a;
            item.uvRect = instance.uvRect;
            item.castShadows = submission.options.castShadows;
            item.receiveShadows = submission.options.receiveShadows;
            item.strategy = DrawStrategy::Auto;
            items.push_back(item);
        }
    }

    //持久静态批按本相机的视锥裁剪成员，索引区间直接指向缓存组
    if (staticBatches) staticBatches->AppendVisibleItems(camera.viewFrustum, camera.drawLayerMask, items);

    //粒子与拖尾由渲染器按相机追加，builder 不重复实现 Billboard 与拖尾的几何
    if (particleRenderer && !particles.IsEmpty())
    {
        particleRenderer->AppendCameraItems(particles, camera, items);
    }
}

//按队列、距离与稳定键排序
void DrawBatchBuilder::SortItems(List<DrawItem>& items)
{
    std::stable_sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b)
    {
        if (a.queue != b.queue) return static_cast<uint32>(a.queue) < static_cast<uint32>(b.queue);

        float32 distanceA = GetSortableDistance(a.cameraDistance);
        float32 distanceB = GetSortableDistance(b.cameraDistance);
        if (a.queue != DrawQueue::Opaque)
        {
            //透明与折射远到近
            if (distanceA != distanceB) return distanceA > distanceB;
            return StableKeyLess(a, b);
        }

        //不透明近到远
        if (distanceA != distanceB) return distanceA < distanceB;
        return StableKeyLess(a, b);
    });

    //在不透明的连续可重排区间内按批次键分组，区间顺序由组内最小距离决定
    List<DrawItem> groupScratch;
    usize begin = 0;
    while (begin < items.size())
    {
        if (!items[begin].reorderable)
        {
            ++begin;
            continue;
        }

        usize end = begin;
        while (end < items.size() && items[end].reorderable) ++end;

        groupScratch.assign(items.begin() + static_cast<isize>(begin), items.begin() + static_cast<isize>(end));
        std::stable_sort(groupScratch.begin(), groupScratch.end(), KeyContentLess);

        //同键项在排序后必然相邻，按组收集下标再按组重排
        struct Group
        {
            float32 minDistance = SortableDistanceLimit;
            DrawItem first;
        };
        List<Group> groups;
        List<List<uint32>> groupMembers;
        usize cursor = 0;
        while (cursor < groupScratch.size())
        {
            usize groupEnd = cursor;
            while (groupEnd < groupScratch.size() && KeyContentEqual(groupScratch[cursor], groupScratch[groupEnd])) ++groupEnd;

            Group group;
            group.first = groupScratch[cursor];
            List<uint32> members;
            for (usize member = cursor; member < groupEnd; ++member)
            {
                group.minDistance = std::min(group.minDistance, GetSortableDistance(groupScratch[member].cameraDistance));
                members.push_back(static_cast<uint32>(member));
            }

            groups.push_back(group);
            groupMembers.push_back(std::move(members));
            cursor = groupEnd;
        }

        //组内按距离升序，组间按最小距离后稳定键
        List<uint32> order(groups.size());
        for (uint32 index = 0; index < order.size(); ++index) order[index] = index;
        std::stable_sort(order.begin(), order.end(), [&](uint32 a, uint32 b)
        {
            if (groups[a].minDistance != groups[b].minDistance) return groups[a].minDistance < groups[b].minDistance;
            return StableKeyLess(groups[a].first, groups[b].first);
        });

        usize write = begin;
        for (uint32 groupIndex : order)
        {
            List<uint32>& members = groupMembers[groupIndex];
            std::stable_sort(members.begin(), members.end(), [&](uint32 a, uint32 b)
            {
                return GetSortableDistance(groupScratch[a].cameraDistance) < GetSortableDistance(groupScratch[b].cameraDistance);
            });
            for (uint32 member : members)
            {
                items[write] = groupScratch[member];
                ++write;
            }
        }

        begin = end;
    }
}

//按排序结果顺序扫描生成批次
void DrawBatchBuilder::BuildBatches(const List<DrawItem>& items, List<DrawBatch>& batches, RenderBatchStats& stats)
{
    EmitBatches(items, nullptr, batches, stats);
}

//从完整场景、全部显式实例与 Opaque Mesh 粒子收集阴影候选
void DrawBatchBuilder::BuildShadowItems(const RenderScene& scene, const List<InstanceSubmission>& submissions,
    const ParticleFrameSnapshot& particles, const RenderCamera& camera, const frustum& lightFrustum,
    List<DrawItem>& items)
{
    items.clear();

    //阴影候选来自完整静态场景，与主相机的可见集合无关
    for (StaticMeshRenderer* renderer : scene.renderers)
    {
        if (!renderer || !renderer->IsRenderSceneEligible() || !renderer->castShadows) continue;
        if (!(renderer->drawLayer & camera.drawLayerMask)) continue;
        //进了持久静态批的渲染器由缓存的投影项提交，这里不再逐对象生成
        if (staticBatches && staticBatches->ContainsRenderer(renderer->GetObjectId())) continue;

        const StaticMeshRendererRenderState& state = renderer->renderState;
        if (!state.mesh || !state.worldBounds.valid) continue;
        if (!RenderMath::Intersects(lightFrustum, state.worldBounds)) continue;
        if (!IsFiniteMatrix(state.localToWorld)) continue;

        for (usize subIndex = 0; subIndex < state.mesh->subMeshes.size(); ++subIndex)
        {
            Material* material = subIndex < renderer->materials.size() ? renderer->materials[subIndex].Get() : nullptr;
            if (!material || material->GetDrawQueue() != DrawQueue::Opaque) continue;

            const SubMesh& subMesh = state.mesh->subMeshes[subIndex];
            usize start = static_cast<usize>(subMesh.indexStart);
            usize count = static_cast<usize>(subMesh.indexCount);
            if (count == 0 || start > state.mesh->indices.size() || count > state.mesh->indices.size() - start) continue;

            DrawItem item;
            item.source = DrawSource::StaticMesh;
            item.geometry = DrawGeometry::Mesh;
            item.owner = renderer->GetEnsId();
            item.sourceObjectId = renderer->GetObjectId();
            item.subMeshIndex = static_cast<uint32>(subIndex);
            item.indexStart = subMesh.indexStart;
            item.indexCount = subMesh.indexCount;
            item.drawLayer = renderer->drawLayer;
            item.mesh = state.mesh;
            item.queue = DrawQueue::Opaque;
            item.mode = GeometryMode::Uniform;
            item.model = state.localToWorld;
            item.worldBounds = state.worldBounds;
            item.strategy = renderer->drawStrategy;
            items.push_back(item);
        }
    }

    //显式实例的阴影候选：castShadows 关闭时不参与
    for (const InstanceSubmission& submission : submissions)
    {
        if (!submission.options.castShadows) continue;
        Mesh* mesh = submission.mesh.Get();
        Material* material = submission.material.Get();
        if (!mesh || !material) continue;
        if (submission.world && submission.world->GetContentRevision() != submission.contentRevision) continue;
        if (!submission.options.camera.IsNull() && submission.options.camera != camera.ens) continue;
        if ((submission.options.drawLayer & camera.drawLayerMask) == 0) continue;
        if (material->GetDrawQueue() != DrawQueue::Opaque) continue;
        if (submission.subMeshIndex >= mesh->subMeshes.size()) continue;

        const SubMesh& subMesh = mesh->subMeshes[submission.subMeshIndex];
        usize start = static_cast<usize>(subMesh.indexStart);
        usize count = static_cast<usize>(subMesh.indexCount);
        if (count == 0 || start > mesh->indices.size() || count > mesh->indices.size() - start) continue;

        const bounds3& localBounds = mesh->GetLocalBounds();
        for (usize index = 0; index < submission.instances.size(); ++index)
        {
            const MeshInstanceData& instance = submission.instances[index];
            matrix4x4 model = RenderMath::TRS(instance.position, instance.rotation, instance.scale);
            if (!IsFiniteMatrix(model)) continue;

            bounds3 worldBounds = localBounds.valid ? RenderMath::TransformBounds(model, localBounds) : bounds3();
            if (worldBounds.valid && !RenderMath::Intersects(lightFrustum, worldBounds)) continue;

            DrawItem item;
            item.source = DrawSource::ExplicitInstance;
            item.geometry = DrawGeometry::Mesh;
            item.sourceObjectId = submission.sourceObjectId;
            item.elementId = (submission.submissionId << 32) | static_cast<uint64>(index);
            item.subMeshIndex = submission.subMeshIndex;
            item.indexStart = subMesh.indexStart;
            item.indexCount = subMesh.indexCount;
            item.drawLayer = submission.options.drawLayer;
            item.mesh = mesh;
            item.queue = DrawQueue::Opaque;
            item.mode = GeometryMode::Instanced;
            item.model = model;
            item.worldBounds = worldBounds;
            item.strategy = DrawStrategy::GpuInstancing;
            items.push_back(item);
        }
    }

    //持久静态批按级联视锥单独裁剪，不复用主相机可见范围
    if (staticBatches) staticBatches->AppendShadowItems(lightFrustum, camera.drawLayerMask, items);

    //粒子的阴影候选由渲染器按快照追加，builder 不重复实现 Mesh 粒子的几何
    if (particleRenderer && !particles.IsEmpty())
    {
        particleRenderer->AppendShadowItems(particles, camera, lightFrustum, items);
    }
}

//按阴影键分组生成批次
void DrawBatchBuilder::BuildShadowBatches(const List<DrawItem>& items, const GpuShader& depthShader,
    List<DrawBatch>& batches, RenderBatchStats& stats)
{
    EmitBatches(items, &depthShader, batches, stats);
}

//生成批次
void DrawBatchBuilder::EmitBatches(const List<DrawItem>& items, const GpuShader* depthShader,
    List<DrawBatch>& batches, RenderBatchStats& stats)
{
    batches.clear();
    if (!gpuResources) return;

    usize index = 0;
    while (index < items.size())
    {
        usize end = index;
        while (end < items.size() && SameBatchContent(items[index], items[end])) ++end;

        const DrawItem& first = items[index];
        //持久静态批各自成批：索引区间直接指向缓存组，不参与动态/实例合并
        if (first.persistentGeometry)
        {
            const GpuShaderPass* batchPass = depthShader
                ? (depthShader->passes.empty() ? nullptr : &depthShader->passes[0])
                : nullptr;
            if (!depthShader)
            {
                const GpuMaterial* batchMaterial = gpuResources->GetMaterial(first.material);
                batchPass = (batchMaterial && batchMaterial->shader && !batchMaterial->shader->passes.empty())
                    ? &batchMaterial->shader->passes[0] : nullptr;
            }

            DrawBatch batch;
            batch.key.queue = first.queue;
            batch.key.mode = GeometryMode::Expanded;
            batch.key.geometry = first.geometry;
            batch.key.indexStart = first.indexStart;
            batch.key.indexCount = first.indexCount;
            batch.key.materialObjectId = depthShader ? 0 : (first.material ? first.material->GetObjectId() : 0);
            batch.key.receiveShadows = first.receiveShadows;
            if (batchPass) batch.key.programId = batchPass->expandedProgram.id;
            batch.items.push_back(static_cast<uint32>(index));
            batch.persistentGeometry = true;
            batch.staticGroup = first.staticGroup;
            batches.push_back(std::move(batch));
            index += 1;
            continue;
        }

        //阴影批次不引用材质，program 与固定状态由深度 Shader 决定
        const GpuMaterial* material = depthShader ? nullptr : gpuResources->GetMaterial(first.material);
        const GpuShader* shader = depthShader ? depthShader : (material ? material->shader : nullptr);
        const GpuShaderPass* pass = (shader && !shader->passes.empty()) ? &shader->passes[0] : nullptr;
        bool singlePass = shader && shader->passes.size() == 1;

        //合批只处理单 Pass 的 Shader。其余来源是排序屏障：多 Pass 必须每个对象依次跑完全部 Pass。
        if (!singlePass || !pass)
        {
            end = index + 1;
            if (!depthShader && material && material->shader && material->shader->passes.size() > 1) stats.multiPassItems += 1;
        }

        bool instancedCapable = instancingAvailable && singlePass && pass && pass->instancedProgram.IsValid();
        bool expandedCapable = singlePass && pass && pass->supportsExpandedGeometry && pass->expandedProgram.IsValid();
        //未接入几何 ABI 的 Pass 只有单绘制变体：合批请求一律退回逐对象绘制，并且必须报出来
        bool geometryAbiMissing = pass && !pass->usesGeometryAbi;

        //普通来源的绘制策略在这里落地：先试实例批，再试动态批，都不成立就单绘制。
        //显式实例与粒子已经带好自己的模式，不参与这套选择。
        GeometryMode mode = first.mode;
        if (first.source == DrawSource::StaticMesh)
        {
            mode = GeometryMode::Uniform;
            if (end > index + 1 && AllAllowInstancing(items, index, end))
            {
                if (instancedCapable) mode = GeometryMode::Instanced;
                else if (geometryAbiMissing) ReportMissingGeometryAbi(shader, *pass, GeometryMode::Instanced, first);
            }
            else
            {
                //动态批放宽到同材质与同状态，不要求同网格；按预算收编连续项
                bool dynamicAllowed = AllAllowDynamicBatching(items, index, end);
                usize dynamicEnd = CollectDynamicBatch(items, index, expandedCapable && dynamicAllowed);
                if (dynamicEnd >= index + 2)
                {
                    end = dynamicEnd;
                    mode = GeometryMode::Expanded;
                }
                else
                {
                    //同材质项够多却没成动态批：声明了展开几何却拿不到变体时，未接入 ABI 的 Pass 要报出来
                    if (geometryAbiMissing && pass->supportsExpandedGeometry && dynamicAllowed && end >= index + 2)
                    {
                        ReportMissingGeometryAbi(shader, *pass, GeometryMode::Expanded, first);
                    }
                    end = index + 1;
                }
            }
        }
        else if (depthShader && geometryAbiMissing && mode != GeometryMode::Uniform)
        {
            //显式实例与粒子的模式由来源固定，深度 Pass 没接入 ABI 时这里同样静默退回逐对象绘制；
            //主 Pass 的同类情况由绘制阶段按「缺少几何变体」上报，不在这里重复。
            ReportMissingGeometryAbi(shader, *pass, mode, first);
        }

        //实例上限之外的连续项拆成多个同键批次
        usize cursor = index;
        while (cursor < end)
        {
            usize chunkEnd = end;
            if (mode == GeometryMode::Instanced && chunkEnd - cursor > MaximumBatchInstances)
            {
                chunkEnd = cursor + MaximumBatchInstances;
            }

            DrawBatch batch;
            batch.key.queue = first.queue;
            batch.key.mode = mode;
            batch.key.geometry = first.geometry;
            batch.key.blendMode = first.blendMode;
            batch.key.meshObjectId = first.mesh ? first.mesh->GetObjectId() : 0;
            batch.key.subMeshIndex = first.subMeshIndex;
            batch.key.indexStart = first.indexStart;
            batch.key.indexCount = first.indexCount;
            //阴影键不含材质，全部按 0 参与比较
            batch.key.materialObjectId = depthShader ? 0 : (first.material ? first.material->GetObjectId() : 0);
            batch.key.receiveShadows = first.receiveShadows;
            if (pass)
            {
                batch.key.programId = pass->GetProgram(mode).id;
                if (depthShader)
                {
                    uint32 resolvedCull = pass->state.cull == CullMode::Auto ? static_cast<uint32>(CullMode::None) : static_cast<uint32>(pass->state.cull);
                    batch.key.depthTest = pass->state.depthTest == ShaderPassToggle::Off ? 0u : 1u;
                    batch.key.depthWrite = pass->state.depthWrite == ShaderPassToggle::Off ? 0u : 1u;
                    batch.key.blend = pass->state.blend == ShaderPassToggle::On ? 1u : 0u;
                    batch.key.cull = resolvedCull;
                }
                else
                {
                    uint32 resolvedDepthTest = pass->state.depthTest == ShaderPassToggle::Off ? 0u : 1u;
                    uint32 alphaBlended = first.queue == DrawQueue::Opaque ? 0u : 1u;
                    uint32 resolvedDepthWrite = pass->state.depthWrite != ShaderPassToggle::Auto
                        ? (pass->state.depthWrite == ShaderPassToggle::On ? 1u : 0u) : (alphaBlended ? 0u : 1u);
                    uint32 resolvedBlend = pass->state.blend != ShaderPassToggle::Auto
                        ? (pass->state.blend == ShaderPassToggle::On ? 1u : 0u) : alphaBlended;
                    batch.key.depthTest = resolvedDepthTest;
                    batch.key.depthWrite = resolvedDepthWrite;
                    batch.key.blend = resolvedBlend;
                    batch.key.cull = static_cast<uint32>(pass->state.cull == CullMode::Auto ? CullMode::None : pass->state.cull);
                }
            }

            for (usize member = cursor; member < chunkEnd; ++member)
            {
                batch.items.push_back(static_cast<uint32>(member));
            }
            batch.instanceCount = mode == GeometryMode::Instanced ? static_cast<uint32>(batch.items.size()) : 0;
            batches.push_back(std::move(batch));
            cursor = chunkEnd;
        }

        //透明与折射在全局排序后断开的同键段计入统计
        if (!depthShader && first.queue != DrawQueue::Opaque && end < items.size() && items[end].queue == first.queue)
        {
            ++stats.transparentBatchBreaks;
        }

        index = end;
    }
}




namespace
{
    //流式缓冲容量增长的下限，与 GpuDrawStream::MinimumStreamCapacity 保持一致。
    constexpr usize StreamCapacityFloor = 64u * 1024u;

    //把容量按 2 倍增长到至少需要的大小；已有容量更大时保持不变。
    usize GrowCapacity(usize current, usize required)
    {
        usize capacity = current > StreamCapacityFloor ? current : StreamCapacityFloor;
        while (capacity < required) capacity *= 2;
        return capacity;
    }
}

//按世界矩阵与每实例绘制参数填充一条实例记录
bool BuildGpuMeshInstance(const matrix4x4& model, const color& linearTint, const color& uvRect, GpuMeshInstance& instance)
{
    for (uint32 element = 0; element < 16; ++element)
    {
        if (!std::isfinite(model.m[element])) return false;
    }

    vector3 column0 = { model.m[0], model.m[1], model.m[2] };
    vector3 column1 = { model.m[4], model.m[5], model.m[6] };
    vector3 column2 = { model.m[8], model.m[9], model.m[10] };

    //法线矩阵是 3x3 的逆转置：奇异模型会让法线失去意义，整条实例拒绝
    float32 determinant = RenderMath::Dot(column0, RenderMath::Cross(column1, column2));
    if (!std::isfinite(determinant) || std::fabs(determinant) < 1.0e-8f) return false;

    //逆转置后的三列就是两两叉积除以行列式，每列补齐成 vec4
    const vector3 columns[3] =
    {
        RenderMath::Cross(column1, column2),
        RenderMath::Cross(column2, column0),
        RenderMath::Cross(column0, column1),
    };
    float32 inverseDeterminant = 1.0f / determinant;
    for (uint32 index = 0; index < 3; ++index)
    {
        instance.normalColumns[index * 4 + 0] = columns[index].x * inverseDeterminant;
        instance.normalColumns[index * 4 + 1] = columns[index].y * inverseDeterminant;
        instance.normalColumns[index * 4 + 2] = columns[index].z * inverseDeterminant;
        instance.normalColumns[index * 4 + 3] = 0.0f;
    }

    for (uint32 element = 0; element < 16; ++element) instance.model[element] = model.m[element];
    instance.tint[0] = linearTint.r;
    instance.tint[1] = linearTint.g;
    instance.tint[2] = linearTint.b;
    instance.tint[3] = linearTint.a;
    instance.uvRect[0] = uvRect.r;
    instance.uvRect[1] = uvRect.g;
    instance.uvRect[2] = uvRect.b;
    instance.uvRect[3] = uvRect.a;
    return true;
}

bool GpuDrawStream::Initialize(RenderBackend* renderBackend)
{
    if (initialized) Shutdown();

    backend = renderBackend;
    if (!backend)
    {
        Log::Error("GpuDrawStream initialization failed: no render backend.");
        return false;
    }

    //实例与展开顶点共用同一套流式分配参数：只按容量分配，内容由上传填充。
    GpuBufferDesc streamDesc;
    streamDesc.data = nullptr;
    streamDesc.size = MinimumStreamCapacity;
    streamDesc.usage = GpuBufferUsage::Stream;
    instanceBuffer = backend->CreateVertexBuffer(streamDesc);
    expandedVertexBuffer = backend->CreateVertexBuffer(streamDesc);

    //索引缓冲至少要有一个元素，否则顶点输入会因索引数量为零被拒绝创建。
    GpuBufferDesc indexDesc;
    indexDesc.data = nullptr;
    indexDesc.size = static_cast<usize>(MinimumIndexCapacity) * sizeof(uint32);
    indexDesc.usage = GpuBufferUsage::Stream;
    expandedIndexBuffer = backend->CreateIndexBuffer(indexDesc);

    if (!instanceBuffer.IsValid() || !expandedVertexBuffer.IsValid() || !expandedIndexBuffer.IsValid())
    {
        Log::Error("GpuDrawStream initialization failed: streaming buffers were not created.");
        Shutdown();
        return false;
    }

    GpuVertexInputDesc inputDesc;
    inputDesc.vertexBuffer = expandedVertexBuffer;
    inputDesc.indexBuffer = expandedIndexBuffer;
    inputDesc.stride = static_cast<uint32>(sizeof(GpuExpandedVertex));
    inputDesc.layout = GpuVertexLayout::Expanded;
    expandedVertexInput = backend->CreateVertexInput(inputDesc);
    if (!expandedVertexInput.IsValid())
    {
        Log::Error("GpuDrawStream initialization failed: the expanded vertex input was not created.");
        Shutdown();
        return false;
    }

    instanceCapacity = MinimumStreamCapacity;
    expandedVertexCapacity = MinimumStreamCapacity;
    expandedIndexCapacity = static_cast<usize>(MinimumIndexCapacity) * sizeof(uint32);
    initialized = true;
    return true;
}

void GpuDrawStream::Shutdown()
{
    if (backend)
    {
        //先释放顶点输入再释放缓冲，避免顶点输入仍记录着已删除的缓冲。
        backend->DeleteVertexInput(expandedVertexInput);
        backend->DeleteVertexBuffer(instanceBuffer);
        backend->DeleteVertexBuffer(expandedVertexBuffer);
        backend->DeleteIndexBuffer(expandedIndexBuffer);
    }

    expandedVertexInput = GpuVertexInputID();
    instanceBuffer = GpuVertexBufferID();
    expandedVertexBuffer = GpuVertexBufferID();
    expandedIndexBuffer = GpuIndexBufferID();
    instanceCapacity = 0;
    expandedVertexCapacity = 0;
    expandedIndexCapacity = 0;
    initialized = false;
    backend = nullptr;
}

bool GpuDrawStream::UploadMeshInstances(std::span<const GpuMeshInstance> instances)
{
    if (!initialized)
    {
        Log::Error("GpuDrawStream upload skipped: the stream is not initialized.");
        return false;
    }
    if (instances.empty()) return true;
    if (instances.size() > MaximumBatchInstances)
    {
        Log::Error("GpuDrawStream upload rejected: the mesh instance count exceeds the single-batch limit.");
        return false;
    }

    usize bytes = instances.size() * sizeof(GpuMeshInstance);
    usize capacity = GrowCapacity(instanceCapacity, bytes);
    if (!backend->UploadVertexBuffer(instanceBuffer, instances.data(), bytes, capacity)) return false;

    instanceCapacity = capacity;
    return true;
}

bool GpuDrawStream::UploadTrailInstances(std::span<const GpuTrailInstance> instances)
{
    if (!initialized)
    {
        Log::Error("GpuDrawStream upload skipped: the stream is not initialized.");
        return false;
    }
    if (instances.empty()) return true;
    if (instances.size() > MaximumBatchInstances)
    {
        Log::Error("GpuDrawStream upload rejected: the trail instance count exceeds the single-batch limit.");
        return false;
    }

    usize bytes = instances.size() * sizeof(GpuTrailInstance);
    usize capacity = GrowCapacity(instanceCapacity, bytes);
    if (!backend->UploadVertexBuffer(instanceBuffer, instances.data(), bytes, capacity)) return false;

    instanceCapacity = capacity;
    return true;
}

bool GpuDrawStream::UploadExpanded(std::span<const GpuExpandedVertex> vertices, std::span<const uint32> indices)
{
    if (!initialized)
    {
        Log::Error("GpuDrawStream upload skipped: the stream is not initialized.");
        return false;
    }
    if (vertices.empty() || indices.empty()) return true;
    if (vertices.size() > MaximumExpandedVertices || indices.size() > MaximumExpandedIndices)
    {
        Log::Error("GpuDrawStream upload rejected: the expanded geometry exceeds the single-batch limit.");
        return false;
    }

    //顶点与索引分别增长，任一失败时另一份已经写入的内容无副作用，只是不绘制。
    usize vertexBytes = vertices.size() * sizeof(GpuExpandedVertex);
    usize vertexCapacity = GrowCapacity(expandedVertexCapacity, vertexBytes);
    if (!backend->UploadVertexBuffer(expandedVertexBuffer, vertices.data(), vertexBytes, vertexCapacity)) return false;
    expandedVertexCapacity = vertexCapacity;

    usize indexBytes = indices.size() * sizeof(uint32);
    usize indexCapacity = GrowCapacity(expandedIndexCapacity, indexBytes);
    uint32 elementCapacity = static_cast<uint32>(indexCapacity / sizeof(uint32));
    if (!backend->UploadIndexBuffer(expandedIndexBuffer, indices.data(), static_cast<uint32>(indices.size()), elementCapacity))
    {
        return false;
    }
    expandedIndexCapacity = indexCapacity;
    return true;
}
