#include "Rendering/StaticBatchCache.h"

#include "Log/Log.h"
#include "Rendering/GeometryExpander.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/World.h"

#include <algorithm>
#include <cstring>

namespace
{
    //单个组的容量上限：展开顶点与索引
    constexpr uint32 MaximumGroupVertices = 65536u;
    constexpr uint32 MaximumGroupIndices = 196608u;

    //把一条成员折进组签名：编号、变换、子网格区间与源网格内容版本任一变化都会改签名
    uint64 FoldMember(uint64 signature, const StaticBatchMember& member, const Mesh& mesh)
    {
        signature = signature * 131u + static_cast<uint64>(member.rendererId);
        signature = signature * 131u + member.subMeshIndex;
        signature = signature * 131u + static_cast<uint64>(member.sourceMeshId);
        signature = signature * 131u + mesh.GetContentVersion();
        for (float32 value : member.model.m)
        {
            //矩阵按位折进签名，避免浮点比较带来的抖动
            uint32 bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            signature = signature * 131u + bits;
        }

        return signature;
    }
}

bool StaticBatchCache::Initialize(RenderBackend* value)
{
    backend = value;
    return backend != nullptr;
}

//释放一个组的 GPU 资源并清空 CPU 数据
void StaticBatchCache::ReleaseGroup(StaticBatchGroup& group)
{
    if (backend)
    {
        //先释放顶点输入，再释放它引用的缓冲
        if (group.vertexInput.IsValid()) backend->DeleteVertexInput(group.vertexInput);
        if (group.vertexBuffer.IsValid()) backend->DeleteVertexBuffer(group.vertexBuffer);
        if (group.indexBuffer.IsValid()) backend->DeleteIndexBuffer(group.indexBuffer);
    }

    group.vertexInput = {};
    group.vertexBuffer = {};
    group.indexBuffer = {};
    group.members.clear();
    group.vertices.clear();
    group.indices.clear();
    group.material.Set(nullptr);
    group.firstMesh.Set(nullptr);
    group.builtSignature = 0;
}

void StaticBatchCache::Shutdown()
{
    for (StaticBatchGroup& group : groups) ReleaseGroup(group);
    groups.clear();
    cachedRenderers.clear();
    boundWorld = nullptr;
    boundContentRevision = 0;
    backend = nullptr;
}

bool StaticBatchCache::ContainsRenderer(int32 rendererId) const
{
    return std::find(cachedRenderers.begin(), cachedRenderers.end(), rendererId) != cachedRenderers.end();
}

const StaticBatchGroup* StaticBatchCache::GetGroup(usize index) const
{
    return index < groups.size() ? &groups[index] : nullptr;
}

void StaticBatchCache::Refresh(World& world, GpuResourceManager& resources)
{
    cachedRenderers.clear();
    if (!backend || !backend->SupportsInstancing()) return;

    //世界整体替换后旧组引用的对象已经销毁，先全部释放
    if (boundWorld != &world || boundContentRevision != world.GetContentRevision())
    {
        for (StaticBatchGroup& group : groups) ReleaseGroup(group);
        groups.clear();
        boundWorld = &world;
        boundContentRevision = world.GetContentRevision();
    }

    //收集候选：static + Auto、单 Pass、开启展开几何、Opaque、状态满足深度测试开且混合关
    List<StaticBatchMember> candidates;
    List<StaticBatchGroup> keys;
    world.ForEachComponent<StaticMeshRenderer>([&](StaticMeshRenderer* renderer)
    {
        if (!renderer || !renderer->IsRenderSceneEligible() || renderer->drawStrategy != DrawStrategy::Auto) return;
        if (!world.GetEnsStatic(renderer->GetEnsId())) return;

        const StaticMeshRendererRenderState& state = renderer->renderState;
        if (!state.mesh || !state.worldBounds.valid) return;

        for (usize subIndex = 0; subIndex < state.mesh->subMeshes.size(); ++subIndex)
        {
            Material* material = subIndex < renderer->materials.size() ? renderer->materials[subIndex].Get() : nullptr;
            if (!material) continue;

            const GpuMaterial* gpuMaterial = resources.GetMaterial(material);
            if (!gpuMaterial || !gpuMaterial->shader || gpuMaterial->shader->passes.size() != 1) continue;
            const GpuShaderPass& pass = gpuMaterial->shader->passes[0];
            if (!pass.supportsExpandedGeometry || !pass.expandedProgram.IsValid()) continue;
            if (material->GetDrawQueue() != DrawQueue::Opaque) continue;
            if (pass.state.depthTest == ShaderPassToggle::Off) continue;
            if (pass.state.depthWrite != ShaderPassToggle::On && pass.state.depthWrite != ShaderPassToggle::Auto) continue;
            if (pass.state.blend == ShaderPassToggle::On) continue;

            const SubMesh& subMesh = state.mesh->subMeshes[subIndex];
            usize start = static_cast<usize>(subMesh.indexStart);
            usize count = static_cast<usize>(subMesh.indexCount);
            if (count == 0 || start > state.mesh->indices.size() || count > state.mesh->indices.size() - start) continue;

            StaticBatchMember member;
            member.rendererId = renderer->GetObjectId();
            member.owner = renderer->GetEnsId();
            member.subMeshIndex = static_cast<uint32>(subIndex);
            member.sourceMeshId = state.mesh->GetObjectId();
            member.model = state.localToWorld;
            member.worldBounds = state.worldBounds;
            candidates.push_back(member);

            StaticBatchGroup group;
            group.materialObjectId = material->GetObjectId();
            group.programId = pass.expandedProgram.id;
            group.queue = static_cast<uint32>(material->GetDrawQueue());
            group.depthTest = 1u;
            group.depthWrite = 1u;
            group.blend = 0u;
            group.cull = static_cast<uint32>(pass.state.cull == CullMode::Auto ? CullMode::None : pass.state.cull);
            group.receiveShadows = renderer->receiveShadows;
            group.castShadows = renderer->castShadows;
            group.drawLayer = renderer->drawLayer;
            group.material.Set(material);
            group.firstMesh.Set(state.mesh);
            keys.push_back(group);
        }
    });

    //按分组键把候选归到已有的组里；组里本帧没有候选就会被删除
    for (usize candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex)
    {
        const StaticBatchGroup& key = keys[candidateIndex];
        StaticBatchGroup* target = nullptr;
        for (StaticBatchGroup& group : groups)
        {
            if (group.materialObjectId != key.materialObjectId || group.programId != key.programId ||
                group.queue != key.queue || group.depthTest != key.depthTest || group.depthWrite != key.depthWrite ||
                group.blend != key.blend || group.cull != key.cull || group.receiveShadows != key.receiveShadows ||
                group.castShadows != key.castShadows || group.drawLayer != key.drawLayer) continue;
            target = &group;
            break;
        }

        if (!target)
        {
            groups.push_back(key);
            target = &groups.back();
        }

        target->members.push_back(candidates[candidateIndex]);
    }

    for (usize index = 0; index < groups.size();)
    {
        StaticBatchGroup& group = groups[index];
        //不足两个成员的组不做持久批：源对象继续走普通策略选择
        if (group.members.size() < 2)
        {
            ReleaseGroup(group);
            groups.erase(groups.begin() + static_cast<isize>(index));
            continue;
        }

        //成员按渲染器编号与子网格排序，保证同组内的索引区间有确定顺序
        std::stable_sort(group.members.begin(), group.members.end(), [](const StaticBatchMember& a, const StaticBatchMember& b)
        {
            if (a.rendererId != b.rendererId) return a.rendererId < b.rendererId;
            return a.subMeshIndex < b.subMeshIndex;
        });

        //重签名：成员集合、变换或源网格内容变了才需要重建几何
        uint64 signature = 0;
        bool resolvable = true;
        for (const StaticBatchMember& member : group.members)
        {
            Object* object = Object::FindObjectById(member.sourceMeshId);
            Mesh* mesh = object ? object->Cast<Mesh>() : nullptr;
            if (!mesh)
            {
                resolvable = false;
                break;
            }

            signature = FoldMember(signature, member, *mesh);
        }

        if (!resolvable)
        {
            ReleaseGroup(group);
            groups.erase(groups.begin() + static_cast<isize>(index));
            continue;
        }

        if (signature == group.builtSignature && group.vertexInput.IsValid()) ++index;
        else
        {
            if (!BuildGroup(group, signature)) ReleaseGroup(group);
            if (!group.vertexInput.IsValid())
            {
                groups.erase(groups.begin() + static_cast<isize>(index));
                continue;
            }
        }

        for (const StaticBatchMember& member : group.members) cachedRenderers.push_back(member.rendererId);
        ++index;
    }
}

//展开并上传一个组的顶点与索引
bool StaticBatchCache::BuildGroup(StaticBatchGroup& group, uint64 signature)
{
    List<GpuExpandedVertex> vertices;
    List<uint32> indices;
    StaticBatchGroup rebuilt;
    rebuilt.materialObjectId = group.materialObjectId;
    rebuilt.programId = group.programId;
    rebuilt.queue = group.queue;
    rebuilt.depthTest = group.depthTest;
    rebuilt.depthWrite = group.depthWrite;
    rebuilt.blend = group.blend;
    rebuilt.cull = group.cull;
    rebuilt.receiveShadows = group.receiveShadows;
    rebuilt.castShadows = group.castShadows;
    rebuilt.drawLayer = group.drawLayer;
    rebuilt.material = group.material;
    rebuilt.firstMesh = group.firstMesh;
    rebuilt.builtSignature = signature;

    for (const StaticBatchMember& member : group.members)
    {
        Object* object = Object::FindObjectById(member.sourceMeshId);
        Mesh* mesh = object ? object->Cast<Mesh>() : nullptr;
        if (!mesh) return false;

        StaticBatchMember placed = member;
        placed.vertexStart = static_cast<uint32>(vertices.size());
        placed.indexStart = static_cast<uint32>(indices.size());
        if (!GeometryExpander::AppendExpandedMesh(*mesh, member.subMeshIndex, member.model,
            { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, vertices, indices))
        {
            return false;
        }

        placed.vertexCount = static_cast<uint32>(vertices.size()) - placed.vertexStart;
        placed.indexCount = static_cast<uint32>(indices.size()) - placed.indexStart;
        if (vertices.size() > MaximumGroupVertices || indices.size() > MaximumGroupIndices)
        {
            //超限不拆组：整组退回普通选择，由调用方继续按预算走实例批或动态批
            return false;
        }

        rebuilt.members.push_back(placed);
    }

    if (rebuilt.members.size() < 2 || vertices.empty() || indices.empty()) return false;

    GpuBufferDesc vertexDesc;
    vertexDesc.data = vertices.data();
    vertexDesc.size = vertices.size() * sizeof(GpuExpandedVertex);
    GpuVertexBufferID vertexBuffer = backend->CreateVertexBuffer(vertexDesc);
    GpuBufferDesc indexDesc;
    indexDesc.data = indices.data();
    indexDesc.size = indices.size() * sizeof(uint32);
    GpuIndexBufferID indexBuffer = backend->CreateIndexBuffer(indexDesc);
    if (!vertexBuffer.IsValid() || !indexBuffer.IsValid())
    {
        if (vertexBuffer.IsValid()) backend->DeleteVertexBuffer(vertexBuffer);
        if (indexBuffer.IsValid()) backend->DeleteIndexBuffer(indexBuffer);
        return false;
    }

    GpuVertexInputDesc inputDesc;
    inputDesc.vertexBuffer = vertexBuffer;
    inputDesc.indexBuffer = indexBuffer;
    inputDesc.stride = sizeof(GpuExpandedVertex);
    inputDesc.layout = GpuVertexLayout::Expanded;
    GpuVertexInputID vertexInput = backend->CreateVertexInput(inputDesc);
    if (!vertexInput.IsValid())
    {
        backend->DeleteVertexBuffer(vertexBuffer);
        backend->DeleteIndexBuffer(indexBuffer);
        return false;
    }

    //新资源就绪后再释放旧的，避免中途失败留下半套句柄
    ReleaseGroup(group);
    rebuilt.vertices = std::move(vertices);
    rebuilt.indices = std::move(indices);
    rebuilt.vertexBuffer = vertexBuffer;
    rebuilt.indexBuffer = indexBuffer;
    rebuilt.vertexInput = vertexInput;
    group = std::move(rebuilt);
    return true;
}

void StaticBatchCache::AppendVisibleItems(const frustum& viewFrustum, uint32 drawLayerMask, List<DrawItem>& items) const
{
    List<uint32> ranges;
    for (usize index = 0; index < groups.size(); ++index)
    {
        const StaticBatchGroup& group = groups[index];
        if (!group.vertexInput.IsValid()) continue;
        if ((group.drawLayer & drawLayerMask) == 0) continue;

        Material* material = group.material.Get();
        if (!material) continue;

        //逐成员按包围盒裁剪，只把连续的可见成员合并成一段索引范围
        CollectVisibleRanges(group, viewFrustum, drawLayerMask, ranges);
        for (usize range = 0; range + 1 < ranges.size(); range += 2)
        {
            DrawItem item;
            item.source = DrawSource::StaticMesh;
            item.geometry = DrawGeometry::Mesh;
            item.owner = group.members.front().owner;
            item.sourceObjectId = group.members.front().rendererId;
            item.subMeshIndex = 0;
            item.indexStart = ranges[range];
            item.indexCount = ranges[range + 1];
            item.drawLayer = group.drawLayer;
            item.material = material;
            item.queue = static_cast<DrawQueue>(group.queue);
            item.blendMode = BlendMode::Alpha;
            item.mode = GeometryMode::Expanded;
            item.strategy = DrawStrategy::Auto;
            item.persistentGeometry = true;
            item.staticGroup = static_cast<uint32>(index);
            item.receiveShadows = group.receiveShadows;
            item.castShadows = group.castShadows;
            item.worldBounds = group.members.front().worldBounds;
            for (const StaticBatchMember& member : group.members)
            {
                if (member.indexStart < item.indexStart + item.indexCount && member.indexStart + member.indexCount > item.indexStart)
                {
                    item.worldBounds = member.worldBounds;
                    break;
                }
            }

            items.push_back(item);
        }
    }
}

void StaticBatchCache::AppendShadowItems(const frustum& lightFrustum, uint32 drawLayerMask, List<DrawItem>& items) const
{
    List<uint32> ranges;
    for (usize index = 0; index < groups.size(); ++index)
    {
        const StaticBatchGroup& group = groups[index];
        if (!group.vertexInput.IsValid() || !group.castShadows) continue;
        if ((group.drawLayer & drawLayerMask) == 0) continue;

        Material* material = group.material.Get();
        if (!material) continue;

        CollectVisibleRanges(group, lightFrustum, drawLayerMask, ranges);
        for (usize range = 0; range + 1 < ranges.size(); range += 2)
        {
            DrawItem item;
            item.source = DrawSource::StaticMesh;
            item.geometry = DrawGeometry::Mesh;
            item.owner = group.members.front().owner;
            item.sourceObjectId = group.members.front().rendererId;
            item.indexStart = ranges[range];
            item.indexCount = ranges[range + 1];
            item.drawLayer = group.drawLayer;
            item.material = material;
            item.queue = DrawQueue::Opaque;
            item.mode = GeometryMode::Expanded;
            item.strategy = DrawStrategy::Auto;
            item.persistentGeometry = true;
            item.staticGroup = static_cast<uint32>(index);
            item.castShadows = true;
            item.receiveShadows = false;
            item.worldBounds = group.members.front().worldBounds;
            items.push_back(item);
        }
    }
}

//按视锥与层掩码收集一个组的可见索引范围，成对写入 (indexStart,indexCount)
void StaticBatchCache::CollectVisibleRanges(const StaticBatchGroup& group, const frustum& viewFrustum,
    uint32 drawLayerMask, List<uint32>& ranges) const
{
    ranges.clear();
    uint32 runStart = 0;
    uint32 runCount = 0;
    for (const StaticBatchMember& member : group.members)
    {
        bool visible = (group.drawLayer & drawLayerMask) != 0 &&
            (!member.worldBounds.valid || RenderMath::Intersects(viewFrustum, member.worldBounds));
        if (!visible)
        {
            //可见性断开就在这里切段，保留每个源对象自己的裁剪结果
            if (runCount != 0)
            {
                ranges.push_back(runStart);
                ranges.push_back(runCount);
                runCount = 0;
            }
            continue;
        }

        if (runCount == 0) runStart = member.indexStart;
        runCount += member.indexCount;
    }

    if (runCount != 0)
    {
        ranges.push_back(runStart);
        ranges.push_back(runCount);
    }
}
