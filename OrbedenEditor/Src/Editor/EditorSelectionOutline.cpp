#include "Editor/EditorSelectionOutline.h"

#include "Rendering/RenderMath.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/SpaceComponent.h"
#include "Runtime/Object/StaticMeshRenderer.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>

namespace
{
    constexpr uint8 ExplicitSelection = 1;
    constexpr uint8 DescendantSelection = 2;

    struct WeldKey
    {
    public:
        int64 x = 0;
        int64 y = 0;
        int64 z = 0;

        /// <summary>判断两个焊接网格坐标是否一致。</summary>
        bool operator==(const WeldKey& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct WeldKeyHash
    {
        /// <summary>计算焊接网格坐标的哈希。</summary>
        usize operator()(const WeldKey& value) const
        {
            usize hash = std::hash<int64>()(value.x);
            hash ^= std::hash<int64>()(value.y) + static_cast<usize>(0x9e3779b9u) + (hash << 6) + (hash >> 2);
            hash ^= std::hash<int64>()(value.z) + static_cast<usize>(0x9e3779b9u) + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    //把 EnsId 合并为可用于本帧查找的稳定键。
    uint64 GetEnsKey(EnsId ens)
    {
        return (static_cast<uint64>(ens.version) << 32) | static_cast<uint64>(ens.id);
    }

    //按选择类型绘制三层发光线。
    void DrawGlowLine(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, uint8 selectionType)
    {
        if (!drawList) return;

        bool explicitSelection = selectionType == ExplicitSelection;
        int32 red = explicitSelection ? 255 : 58;
        int32 green = explicitSelection ? 134 : 145;
        int32 blue = explicitSelection ? 24 : 255;
        drawList->AddLine(a, b, IM_COL32(red, green, blue, 42), 7.0f);
        drawList->AddLine(a, b, IM_COL32(red, green, blue, 112), 4.0f);
        drawList->AddLine(a, b, IM_COL32(red, green, blue, 255), 2.0f);
    }
}

//绘制当前选择及其后代的屏幕空间轮廓。
void EditorSelectionOutline::Draw(const RenderScene& scene, World& world, EnsId cameraEns,
    const EditorSelection& selection, const vector2& workspacePosition, const vector2& workspaceSize)
{
    const List<EnsId>& selectedEns = selection.GetSelectedEnsList();
    if (selectedEns.empty()) return;
    if (workspaceSize.x <= 0.0f || workspaceSize.y <= 0.0f) return;

    //定期回收已经销毁或长期未使用的拓扑缓存。
    ++frameIndex;
    if (frameIndex % 300 == 0)
    {
        for (auto cacheIt = topologyCache.begin(); cacheIt != topologyCache.end();)
        {
            List<MeshTopology>& entries = cacheIt->second;
            entries.erase(std::remove_if(entries.begin(), entries.end(), [this](const MeshTopology& entry)
            {
                return !Object::IsObjectAlive(entry.objectId)
                    || frameIndex - entry.lastUsedFrame > 600;
            }), entries.end());
            if (entries.empty()) cacheIt = topologyCache.erase(cacheIt);
            else ++cacheIt;
        }
    }

    //查找本帧实际使用的相机记录。
    const RenderCamera* camera = nullptr;
    for (const RenderCamera& candidate : scene.cameras)
    {
        if (candidate.ens == cameraEns)
        {
            camera = &candidate;
            break;
        }
    }
    if (!camera || camera->renderTargetId.IsValid() || camera->viewportWidth <= 0 || camera->viewportHeight <= 0) return;

    //收集显式选择和层级后代，显式选择拥有最高优先级。
    std::unordered_map<uint64, uint8> selectionTypes;
    List<EnsId> pendingEns;
    for (EnsId ens : selectedEns)
    {
        if (ens.IsNull() || !world.IsAlive(ens)) continue;
        selectionTypes[GetEnsKey(ens)] = ExplicitSelection;
    }
    for (EnsId ens : selectedEns)
    {
        SpaceComponent* selectedSpace = world.GetSpaceComponent(ens);
        if (!selectedSpace) continue;

        EnsId child = selectedSpace->firstChild;
        while (!child.IsNull())
        {
            pendingEns.push_back(child);
            SpaceComponent* childSpace = world.GetSpaceComponent(child);
            child = childSpace ? childSpace->next : EnsId();
        }
    }
    while (!pendingEns.empty())
    {
        EnsId ens = pendingEns.back();
        pendingEns.pop_back();
        if (ens.IsNull() || !world.IsAlive(ens)) continue;

        uint64 key = GetEnsKey(ens);
        auto selectionIt = selectionTypes.find(key);
        if (selectionIt != selectionTypes.end()) continue;
        selectionTypes.emplace(key, DescendantSelection);
        SpaceComponent* space = world.GetSpaceComponent(ens);
        if (!space) continue;

        EnsId child = space->firstChild;
        while (!child.IsNull())
        {
            pendingEns.push_back(child);
            SpaceComponent* childSpace = world.GetSpaceComponent(child);
            child = childSpace ? childSpace->next : EnsId();
        }
    }

    struct InstanceGroup
    {
    public:
        EnsId ens;
        StaticMeshRenderer* renderer = nullptr;
        Mesh* mesh = nullptr;
        matrix4x4 localToWorld;
        uint8 selectionType = DescendantSelection;
        List<IndexRange> ranges;
    };

    //按 renderer 实例聚合同一对象的所有有效 SubMesh 范围。
    List<InstanceGroup> groups;
    std::unordered_map<StaticMeshRenderer*, usize> groupIndices;
    for (const RenderItem& item : scene.items)
    {
        auto selectionIt = selectionTypes.find(GetEnsKey(item.ens));
        if (selectionIt == selectionTypes.end()) continue;

        Ens* currentEns = world.GetEns(item.ens);
        StaticMeshRenderer* currentRenderer = currentEns ? currentEns->GetComponent<StaticMeshRenderer>() : nullptr;
        Mesh* currentMesh = currentRenderer ? currentRenderer->mesh.Get() : nullptr;
        if (!currentRenderer || currentRenderer != item.renderer || !currentMesh || currentMesh != item.mesh) continue;
        if ((item.drawLayer & camera->drawLayerMask) == 0) continue;
        if (!RenderMath::Intersects(camera->viewFrustum, item.worldBounds)) continue;

        usize start = static_cast<usize>(item.indexStart);
        usize count = static_cast<usize>(item.indexCount);
        if (start > currentMesh->indices.size() || count > currentMesh->indices.size() - start) continue;
        count -= count % 3;
        if (count == 0) continue;

        auto groupIt = groupIndices.find(item.renderer);
        if (groupIt == groupIndices.end())
        {
            InstanceGroup group;
            group.ens = item.ens;
            group.renderer = currentRenderer;
            group.mesh = currentMesh;
            group.localToWorld = item.localToWorld;
            group.selectionType = selectionIt->second;
            group.ranges.push_back({ item.indexStart, static_cast<uint32>(count) });
            groupIndices.emplace(item.renderer, groups.size());
            groups.push_back(group);
        }
        else
        {
            InstanceGroup& group = groups[groupIt->second];
            if (group.mesh == currentMesh)
            {
                group.ranges.push_back({ item.indexStart, static_cast<uint32>(count) });
            }
        }
    }
    if (groups.empty()) return;

    //规范索引范围并确保后代先画、显式选择最后画。
    for (InstanceGroup& group : groups)
    {
        std::sort(group.ranges.begin(), group.ranges.end(), [](const IndexRange& a, const IndexRange& b)
        {
            return a.start != b.start ? a.start < b.start : a.count < b.count;
        });
        group.ranges.erase(std::unique(group.ranges.begin(), group.ranges.end()), group.ranges.end());
    }
    std::stable_sort(groups.begin(), groups.end(), [](const InstanceGroup& a, const InstanceGroup& b)
    {
        return a.selectionType > b.selectionType;
    });

    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    if (!mainViewport || mainViewport->Size.x <= 0.0f || mainViewport->Size.y <= 0.0f) return;

    //把相机的归一化 viewport 映射到 ImGui 主视口逻辑坐标。
    ImVec2 cameraPosition(
        mainViewport->Pos.x + camera->normalizedViewportX * mainViewport->Size.x,
        mainViewport->Pos.y + (1.0f - camera->normalizedViewportY - camera->normalizedViewportHeight) * mainViewport->Size.y);
    ImVec2 cameraSize(
        camera->normalizedViewportWidth * mainViewport->Size.x,
        camera->normalizedViewportHeight * mainViewport->Size.y);
    if (cameraSize.x <= 0.0f || cameraSize.y <= 0.0f) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImVec2 clipMin(workspacePosition.x, workspacePosition.y);
    ImVec2 clipMax(workspacePosition.x + workspaceSize.x, workspacePosition.y + workspaceSize.y);
    drawList->PushClipRect(clipMin, clipMax, true);

    for (const InstanceGroup& group : groups)
    {
        const MeshTopology& topology = GetTopology(group.mesh, group.ranges);
        if (topology.vertices.empty() || topology.triangles.empty() || topology.edges.empty()) continue;

        //一次性计算本实例所有焊接顶点的世界和齐次裁剪坐标。
        matrix4x4 localToClip = RenderMath::Mul(camera->viewProjectionMatrix, group.localToWorld);
        worldVerticesScratch.resize(topology.vertices.size());
        clipVerticesScratch.resize(topology.vertices.size());
        for (usize index = 0; index < topology.vertices.size(); ++index)
        {
            worldVerticesScratch[index] = RenderMath::TransformPoint(group.localToWorld, topology.vertices[index]);
            clipVerticesScratch[index] = TransformClip(localToClip, topology.vertices[index]);
        }

        //按相机位置判断每个面的世界空间正反朝向。
        faceOrientationsScratch.assign(topology.triangles.size(), 0);
        for (usize index = 0; index < topology.triangles.size(); ++index)
        {
            const TopologyTriangle& triangle = topology.triangles[index];
            const vector3& a = worldVerticesScratch[triangle.a];
            const vector3& b = worldVerticesScratch[triangle.b];
            const vector3& c = worldVerticesScratch[triangle.c];
            vector3 ab = { b.x - a.x, b.y - a.y, b.z - a.z };
            vector3 ac = { c.x - a.x, c.y - a.y, c.z - a.z };
            vector3 normal = RenderMath::Cross(ab, ac);
            vector3 center = { (a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f, (a.z + b.z + c.z) / 3.0f };
            vector3 toCamera = { camera->position.x - center.x, camera->position.y - center.y, camera->position.z - center.z };
            float32 facing = RenderMath::Dot(normal, toCamera);
            float32 facingScaleSquared = RenderMath::Dot(normal, normal) * RenderMath::Dot(toCamera, toCamera);
            float32 facingThresholdSquared = facingScaleSquared * 0.000000000001f;
            if (facing * facing > facingThresholdSquared) faceOrientationsScratch[index] = facing > 0.0f ? 1 : -1;
        }

        //绘制开放边和任意邻接数下的正反混合轮廓边。
        for (const TopologyEdge& edge : topology.edges)
        {
            bool drawEdge = edge.faceCount == 1;
            bool hasFront = false;
            bool hasBack = false;
            bool hasTangent = false;
            for (uint32 faceIndex = 0; faceIndex < edge.faceCount; ++faceIndex)
            {
                uint32 face = topology.edgeFaces[edge.faceOffset + faceIndex];
                if (face >= faceOrientationsScratch.size()) continue;
                int8 orientation = faceOrientationsScratch[face];
                hasFront |= orientation > 0;
                hasBack |= orientation < 0;
                hasTangent |= orientation == 0;
            }
            drawEdge |= hasFront && hasBack;
            drawEdge |= hasTangent && (hasFront || hasBack);
            if (!drawEdge) continue;

            ClipPoint a = clipVerticesScratch[edge.a];
            ClipPoint b = clipVerticesScratch[edge.b];
            if (!ClipLine(a, b)) continue;
            if (std::abs(a.w) <= 0.000001f || std::abs(b.w) <= 0.000001f) continue;
            if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.w)
                || !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.w)) continue;

            float32 ndcAx = a.x / a.w;
            float32 ndcAy = a.y / a.w;
            float32 ndcBx = b.x / b.w;
            float32 ndcBy = b.y / b.w;
            ImVec2 screenA(
                cameraPosition.x + (ndcAx * 0.5f + 0.5f) * cameraSize.x,
                cameraPosition.y + (1.0f - (ndcAy * 0.5f + 0.5f)) * cameraSize.y);
            ImVec2 screenB(
                cameraPosition.x + (ndcBx * 0.5f + 0.5f) * cameraSize.x,
                cameraPosition.y + (1.0f - (ndcBy * 0.5f + 0.5f)) * cameraSize.y);
            DrawGlowLine(drawList, screenA, screenB, group.selectionType);
        }
    }

    drawList->PopClipRect();
}

//清空网格拓扑缓存。
void EditorSelectionOutline::Clear()
{
    topologyCache.clear();
    topologyCache.rehash(0);
    worldVerticesScratch.clear();
    worldVerticesScratch.shrink_to_fit();
    clipVerticesScratch.clear();
    clipVerticesScratch.shrink_to_fit();
    faceOrientationsScratch.clear();
    faceOrientationsScratch.shrink_to_fit();
    frameIndex = 0;
}

//计算顶点的齐次裁剪空间坐标。
EditorSelectionOutline::ClipPoint EditorSelectionOutline::TransformClip(const matrix4x4& matrix, const vector3& point)
{
    ClipPoint result;
    result.x = matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12];
    result.y = matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13];
    result.z = matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14];
    result.w = matrix.m[3] * point.x + matrix.m[7] * point.y + matrix.m[11] * point.z + matrix.m[15];
    return result;
}

//获取与网格版本和有效索引范围匹配的拓扑缓存。
const EditorSelectionOutline::MeshTopology& EditorSelectionOutline::GetTopology(Mesh* mesh, const List<IndexRange>& ranges)
{
    List<MeshTopology>& entries = topologyCache[mesh];
    int32 objectId = mesh ? mesh->GetObjectId() : 0;
    uint64 instanceHash = mesh ? mesh->GetInstanceId().GetHash() : 0;
    uint64 revision = mesh ? mesh->GetRevision() : 0;
    usize vertexCount = mesh ? mesh->vertices.size() : 0;
    usize indexCount = mesh ? mesh->indices.size() : 0;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [objectId, instanceHash, revision, vertexCount, indexCount](const MeshTopology& entry)
    {
        return entry.objectId != objectId
            || entry.instanceHash != instanceHash
            || entry.revision != revision
            || entry.vertexCount != vertexCount
            || entry.indexCount != indexCount;
    }), entries.end());
    for (MeshTopology& entry : entries)
    {
        if (entry.ranges == ranges)
        {
            entry.lastUsedFrame = frameIndex;
            return entry;
        }
    }

    MeshTopology topology;
    topology.objectId = objectId;
    topology.instanceHash = instanceHash;
    topology.revision = revision;
    topology.vertexCount = vertexCount;
    topology.indexCount = indexCount;
    topology.lastUsedFrame = frameIndex;
    topology.ranges = ranges;
    if (!mesh || mesh->vertices.empty() || mesh->indices.empty())
    {
        entries.push_back(topology);
        return entries.back();
    }

    //按局部包围盒尺度的 1e-5 焊接位置接近的顶点。
    const bounds3& bounds = mesh->GetLocalBounds();
    float32 boundsScale = bounds.valid
        ? std::max({ bounds.extents.x * 2.0f, bounds.extents.y * 2.0f, bounds.extents.z * 2.0f })
        : 0.0f;
    float32 weldEpsilon = std::max(boundsScale * 0.00001f, 0.0000001f);
    float32 weldEpsilonSquared = weldEpsilon * weldEpsilon;
    List<uint32> weldedIndices(mesh->vertices.size(), std::numeric_limits<uint32>::max());
    std::unordered_map<WeldKey, List<uint32>, WeldKeyHash> weldBuckets;
    for (usize index = 0; index < mesh->vertices.size(); ++index)
    {
        const vector3& point = mesh->vertices[index];
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        {
            weldedIndices[index] = static_cast<uint32>(topology.vertices.size());
            topology.vertices.push_back(point);
            continue;
        }

        WeldKey cell =
        {
            static_cast<int64>(std::floor(point.x / weldEpsilon)),
            static_cast<int64>(std::floor(point.y / weldEpsilon)),
            static_cast<int64>(std::floor(point.z / weldEpsilon)),
        };
        uint32 weldedIndex = std::numeric_limits<uint32>::max();
        for (int32 z = -1; z <= 1 && weldedIndex == std::numeric_limits<uint32>::max(); ++z)
        {
            for (int32 y = -1; y <= 1 && weldedIndex == std::numeric_limits<uint32>::max(); ++y)
            {
                for (int32 x = -1; x <= 1 && weldedIndex == std::numeric_limits<uint32>::max(); ++x)
                {
                    auto bucketIt = weldBuckets.find({ cell.x + x, cell.y + y, cell.z + z });
                    if (bucketIt == weldBuckets.end()) continue;
                    for (uint32 candidate : bucketIt->second)
                    {
                        const vector3& other = topology.vertices[candidate];
                        float32 dx = point.x - other.x;
                        float32 dy = point.y - other.y;
                        float32 dz = point.z - other.z;
                        if (dx * dx + dy * dy + dz * dz <= weldEpsilonSquared)
                        {
                            weldedIndex = candidate;
                            break;
                        }
                    }
                }
            }
        }

        if (weldedIndex == std::numeric_limits<uint32>::max())
        {
            weldedIndex = static_cast<uint32>(topology.vertices.size());
            topology.vertices.push_back(point);
            weldBuckets[cell].push_back(weldedIndex);
        }
        weldedIndices[index] = weldedIndex;
    }

    //构建三角形和支持任意邻接面数量的无向边。
    struct EdgeFacePair
    {
    public:
        uint32 edge = 0;
        uint32 face = 0;
    };

    std::unordered_map<uint64, uint32> edgeIndices;
    List<EdgeFacePair> edgeFacePairs;
    for (const IndexRange& range : ranges)
    {
        usize end = static_cast<usize>(range.start) + static_cast<usize>(range.count);
        if (end > mesh->indices.size()) continue;
        for (usize index = range.start; index + 2 < end; index += 3)
        {
            uint32 originalA = mesh->indices[index + 0];
            uint32 originalB = mesh->indices[index + 1];
            uint32 originalC = mesh->indices[index + 2];
            if (originalA >= weldedIndices.size() || originalB >= weldedIndices.size() || originalC >= weldedIndices.size()) continue;

            uint32 a = weldedIndices[originalA];
            uint32 b = weldedIndices[originalB];
            uint32 c = weldedIndices[originalC];
            if (a == b || b == c || c == a) continue;

            uint32 face = static_cast<uint32>(topology.triangles.size());
            topology.triangles.push_back({ a, b, c });
            const uint32 edgeVertices[3][2] = { { a, b }, { b, c }, { c, a } };
            for (const auto& edgeVerticesPair : edgeVertices)
            {
                uint32 edgeA = std::min(edgeVerticesPair[0], edgeVerticesPair[1]);
                uint32 edgeB = std::max(edgeVerticesPair[0], edgeVerticesPair[1]);
                uint64 edgeKey = (static_cast<uint64>(edgeA) << 32) | static_cast<uint64>(edgeB);
                auto [edgeIt, inserted] = edgeIndices.emplace(edgeKey, static_cast<uint32>(topology.edges.size()));
                if (inserted)
                {
                    TopologyEdge edge;
                    edge.a = edgeA;
                    edge.b = edgeB;
                    topology.edges.push_back(edge);
                }

                uint32 edgeIndex = edgeIt->second;
                ++topology.edges[edgeIndex].faceCount;
                edgeFacePairs.push_back({ edgeIndex, face });
            }
        }
    }

    //把邻接面压入连续数组，避免每条边单独分配容器。
    uint32 faceOffset = 0;
    List<uint32> writeOffsets(topology.edges.size(), 0);
    for (usize edgeIndex = 0; edgeIndex < topology.edges.size(); ++edgeIndex)
    {
        TopologyEdge& edge = topology.edges[edgeIndex];
        edge.faceOffset = faceOffset;
        writeOffsets[edgeIndex] = faceOffset;
        faceOffset += edge.faceCount;
    }
    topology.edgeFaces.resize(faceOffset);
    for (const EdgeFacePair& pair : edgeFacePairs)
    {
        topology.edgeFaces[writeOffsets[pair.edge]++] = pair.face;
    }

    entries.push_back(std::move(topology));
    return entries.back();
}

//将齐次裁剪空间线段裁剪到六个视锥平面内。
bool EditorSelectionOutline::ClipLine(ClipPoint& a, ClipPoint& b)
{
    const ClipPoint start = a;
    const ClipPoint end = b;
    float32 enter = 0.0f;
    float32 exit = 1.0f;
    const float32 distancesA[6] =
    {
        start.x + start.w,
        start.w - start.x,
        start.y + start.w,
        start.w - start.y,
        start.z + start.w,
        start.w - start.z,
    };
    const float32 distancesB[6] =
    {
        end.x + end.w,
        end.w - end.x,
        end.y + end.w,
        end.w - end.y,
        end.z + end.w,
        end.w - end.z,
    };

    //逐平面收紧线段的有效参数区间。
    for (int32 plane = 0; plane < 6; ++plane)
    {
        float32 distanceA = distancesA[plane];
        float32 distanceB = distancesB[plane];
        if (distanceA < 0.0f && distanceB < 0.0f) return false;
        if (distanceA >= 0.0f && distanceB >= 0.0f) continue;

        float32 denominator = distanceA - distanceB;
        if (std::abs(denominator) <= 0.000001f) return false;
        float32 parameter = distanceA / denominator;
        if (distanceA < 0.0f) enter = std::max(enter, parameter);
        else exit = std::min(exit, parameter);
        if (enter > exit) return false;
    }

    auto interpolate = [](float32 from, float32 to, float32 t)
    {
        return from + (to - from) * t;
    };
    a =
    {
        interpolate(start.x, end.x, enter),
        interpolate(start.y, end.y, enter),
        interpolate(start.z, end.z, enter),
        interpolate(start.w, end.w, enter),
    };
    b =
    {
        interpolate(start.x, end.x, exit),
        interpolate(start.y, end.y, exit),
        interpolate(start.z, end.z, exit),
        interpolate(start.w, end.w, exit),
    };
    return true;
}
