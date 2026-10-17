#include "Rendering/GeometryExpander.h"

#include "Rendering/RenderMath.h"
#include "Profiler/Profiler.h"
#include "Runtime/Object/Mesh.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace
{
    //保存索引区间的引用顺序与局部索引
    struct IndexTopology
    {
        List<uint32> referencedVertices;
        List<uint32> localIndices;
        uint32 triangleVertexCount = 0;
        bool valid = false;
    };

    //按网格身份与内容版本管理拓扑，不持有网格所有权
    struct MeshTopologyCache
    {
        const Mesh* mesh = nullptr;
        uint64 contentVersion = 0;
        usize vertexCount = 0;
        usize indexCount = 0;
        std::unordered_map<uint64, IndexTopology> ranges;
    };

    std::unordered_map<int32, MeshTopologyCache> meshTopologies;

    //顶点映射的工作区。stamp 记世代号表示「本世代已映射」，localIndex 记该顶点在本次追加里的局部编号。
    //渲染是单线程的，复用这两张表可以避免每个绘制项都分配；推进世代号代替逐次清零。
    List<uint32> vertexStamp;
    List<uint32> vertexLocalIndex;
    uint32 vertexGeneration = 0;

    //准备顶点映射的工作区并返回可写的标记表
    uint32* PrepareVertexMapping(usize vertexCount)
    {
        if (vertexStamp.size() < vertexCount)
        {
            vertexStamp.resize(vertexCount, 0u);
            vertexLocalIndex.resize(vertexCount, 0u);
        }

        ++vertexGeneration;
        if (vertexGeneration == 0)
        {
            //世代号回绕：真正清空一次，之后继续用新的世代号
            std::fill(vertexStamp.begin(), vertexStamp.end(), 0u);
            vertexGeneration = 1;
        }

        return vertexStamp.data();
    }

    //读取或重建一个索引区间的引用顶点和局部索引
    const IndexTopology& GetOrBuildTopology(const Mesh& mesh, uint32 indexStart, uint32 indexCount)
    {
        PROFILE("Render/Geometry/GetTopology");
        MeshTopologyCache& cache = meshTopologies[mesh.GetObjectId()];
        uint64 version = mesh.GetContentVersion();
        if (cache.mesh != &mesh || cache.contentVersion != version
            || cache.vertexCount != mesh.vertices.size() || cache.indexCount != mesh.indices.size())
        {
            cache.ranges.clear();
            cache.mesh = &mesh;
            cache.contentVersion = version;
            cache.vertexCount = mesh.vertices.size();
            cache.indexCount = mesh.indices.size();
        }

        uint64 key = (static_cast<uint64>(indexStart) << 32) | indexCount;
        auto [entry, inserted] = cache.ranges.try_emplace(key);
        IndexTopology& topology = entry->second;
        if (!inserted) return topology;

        PROFILE("Render/Geometry/BuildTopology");
        usize start = indexStart;
        usize count = indexCount;
        if (count == 0 || start > mesh.indices.size() || count > mesh.indices.size() - start)
            return topology;

        topology.referencedVertices.reserve(std::min(count, mesh.vertices.size()));
        topology.localIndices.reserve(count);
        usize triangleIndexCount = count - count % 3;
        uint32* stamp = PrepareVertexMapping(mesh.vertices.size());
        for (usize offset = 0; offset < count; ++offset)
        {
            uint32 vertex = mesh.indices[start + offset];
            if (vertex >= mesh.vertices.size())
            {
                topology.referencedVertices.clear();
                topology.localIndices.clear();
                topology.triangleVertexCount = 0;
                return topology;
            }
            if (stamp[vertex] != vertexGeneration)
            {
                stamp[vertex] = vertexGeneration;
                vertexLocalIndex[vertex] = static_cast<uint32>(topology.referencedVertices.size());
                topology.referencedVertices.push_back(vertex);
                if (offset < triangleIndexCount) ++topology.triangleVertexCount;
            }
            topology.localIndices.push_back(vertexLocalIndex[vertex]);
        }
        topology.valid = true;
        return topology;
    }

    //矩阵是否有限且可逆；奇异矩阵会让法线失去意义
    bool IsTransformInvertible(const matrix4x4& model)
    {
        for (uint32 element = 0; element < 16; ++element)
        {
            if (!std::isfinite(model.m[element])) return false;
        }

        vector3 column0 = { model.m[0], model.m[1], model.m[2] };
        vector3 column1 = { model.m[4], model.m[5], model.m[6] };
        vector3 column2 = { model.m[8], model.m[9], model.m[10] };
        float32 determinant = RenderMath::Dot(column0, RenderMath::Cross(column1, column2));
        return std::isfinite(determinant) && std::fabs(determinant) >= 1.0e-8f;
    }

    //法线取逆转置，切线先线性变换再与法线正交化
    vector3 TransformNormal(const matrix4x4& model, const vector3& normal)
    {
        vector3 column0 = { model.m[0], model.m[1], model.m[2] };
        vector3 column1 = { model.m[4], model.m[5], model.m[6] };
        vector3 column2 = { model.m[8], model.m[9], model.m[10] };
        vector3 row0 = { (column1.y * column2.z - column1.z * column2.y), (column2.y * column0.z - column2.z * column0.y),
            (column0.y * column1.z - column0.z * column1.y) };
        vector3 row1 = { (column1.z * column2.x - column1.x * column2.z), (column2.z * column0.x - column2.x * column0.z),
            (column0.z * column1.x - column0.x * column1.z) };
        vector3 row2 = { (column1.x * column2.y - column1.y * column2.x), (column2.x * column0.y - column2.y * column0.x),
            (column0.x * column1.y - column0.y * column1.x) };
        vector3 transformed = { RenderMath::Dot(row0, normal), RenderMath::Dot(row1, normal), RenderMath::Dot(row2, normal) };
        return RenderMath::Normalize(transformed);
    }

    //把一个顶点写成世界空间的展开顶点
    GpuExpandedVertex BuildExpandedVertex(const Mesh& mesh, usize vertex, const matrix4x4& model,
        const color& tint, const color& uvRect)
    {
        GpuExpandedVertex expanded{};
        vector3 world = RenderMath::TransformPoint(model, mesh.vertices[vertex]);
        expanded.position[0] = world.x;
        expanded.position[1] = world.y;
        expanded.position[2] = world.z;

        vector3 sourceNormal = vertex < mesh.normals.size() ? mesh.normals[vertex] : vector3();
        vector3 normal = TransformNormal(model, sourceNormal);
        expanded.normal[0] = normal.x;
        expanded.normal[1] = normal.y;
        expanded.normal[2] = normal.z;

        vector2 uv = vertex < mesh.texcoords.size() ? mesh.texcoords[vertex] : vector2();
        expanded.uv[0] = uv.x * uvRect.b + uvRect.r;
        expanded.uv[1] = uv.y * uvRect.a + uvRect.g;

        vector3 tangentSource = vertex < mesh.tangents.size() ? mesh.tangents[vertex] : vector3();
        vector3 tangent = RenderMath::TransformDirection(model, tangentSource);
        float32 projection = RenderMath::Dot(tangent, normal);
        tangent = { tangent.x - normal.x * projection, tangent.y - normal.y * projection, tangent.z - normal.z * projection };
        float32 tangentLength = std::sqrt(RenderMath::Dot(tangent, tangent));
        if (tangentLength > 1.0e-6f)
        {
            tangent = { tangent.x / tangentLength, tangent.y / tangentLength, tangent.z / tangentLength };
        }
        else
        {
            tangent = { 0.0f, 0.0f, 0.0f };
        }

        expanded.tangent[0] = tangent.x;
        expanded.tangent[1] = tangent.y;
        expanded.tangent[2] = tangent.z;
        expanded.tint[0] = tint.r;
        expanded.tint[1] = tint.g;
        expanded.tint[2] = tint.b;
        expanded.tint[3] = tint.a;
        return expanded;
    }
}

void GeometryExpander::ReleaseDestroyedMeshCaches()
{
    PROFILE("Render/Geometry/ReleaseCaches");
    for (auto iterator = meshTopologies.begin(); iterator != meshTopologies.end();)
    {
        if (Object::FindObjectById(iterator->first) != iterator->second.mesh)
        {
            iterator = meshTopologies.erase(iterator);
            continue;
        }

        //清理子网格区间调整后留下的旧映射
        MeshTopologyCache& cache = iterator->second;
        if (cache.ranges.size() > cache.mesh->subMeshes.size())
        {
            for (auto range = cache.ranges.begin(); range != cache.ranges.end();)
            {
                bool present = std::any_of(cache.mesh->subMeshes.begin(), cache.mesh->subMeshes.end(),
                    [&](const SubMesh& subMesh)
                    {
                        uint64 key = (static_cast<uint64>(subMesh.indexStart) << 32) | subMesh.indexCount;
                        return key == range->first;
                    });
                if (!present) range = cache.ranges.erase(range);
                else ++range;
            }
        }
        ++iterator;
    }
}

void GeometryExpander::ClearCaches()
{
    meshTopologies.clear();
    vertexStamp.clear();
    vertexLocalIndex.clear();
    vertexGeneration = 0;
}

uint32 GeometryExpander::CountReferencedVertices(const Mesh& mesh, uint32 indexStart, uint32 indexCount)
{
    PROFILE("Render/Geometry/CountReferencedVertices");
    usize start = std::min<usize>(indexStart, mesh.indices.size());
    usize count = std::min<usize>(indexCount, mesh.indices.size() - start);
    const IndexTopology& topology = GetOrBuildTopology(mesh, static_cast<uint32>(start), static_cast<uint32>(count));
    return topology.valid ? static_cast<uint32>(topology.referencedVertices.size()) : 0;
}

bool GeometryExpander::AppendExpandedMesh(const Mesh& mesh, uint32 subMeshIndex, const matrix4x4& model,
    const color& tint, const color& uvRect, List<GpuExpandedVertex>& vertices, List<uint32>& indices)
{
    PROFILE("Render/Geometry/AppendMesh");
    if (subMeshIndex >= mesh.subMeshes.size()) return false;
    if (!IsTransformInvertible(model)) return false;

    const SubMesh& subMesh = mesh.subMeshes[subMeshIndex];
    const IndexTopology* topology;
    {
        PROFILE("Render/Geometry/ValidateIndices");
        topology = &GetOrBuildTopology(mesh, subMesh.indexStart, subMesh.indexCount);
        if (!topology->valid) return false;
    }

    usize indexCount = topology->localIndices.size() - topology->localIndices.size() % 3;
    if (indexCount == 0) return false;

    //准备追加空间并保留失败回滚位置
    usize vertexBase = vertices.size();
    usize indexBase = indices.size();
    {
        PROFILE("Render/Geometry/ReserveBuffers");
        usize requiredVertices = vertexBase + topology->triangleVertexCount;
        usize requiredIndices = indexBase + indexCount;
        if (requiredVertices > vertices.capacity())
            vertices.reserve(std::max(requiredVertices, vertices.capacity() * 2));
        if (requiredIndices > indices.capacity())
            indices.reserve(std::max(requiredIndices, indices.capacity() * 2));
    }

    //按首次引用顺序转换每个顶点
    {
        PROFILE("Render/Geometry/TransformVertices");
        const uint32* sourceVertices = topology->referencedVertices.data();
        for (usize vertex = 0; vertex < topology->triangleVertexCount; ++vertex)
        {
            GpuExpandedVertex expanded = BuildExpandedVertex(mesh, sourceVertices[vertex], model, tint, uvRect);
            if (!std::isfinite(expanded.position[0]) || !std::isfinite(expanded.position[1]) || !std::isfinite(expanded.position[2]))
            {
                vertices.resize(vertexBase);
                indices.resize(indexBase);
                return false;
            }
            vertices.push_back(expanded);
        }
    }

    //批量复制局部索引并应用本次追加的顶点偏移
    {
        PROFILE("Render/Geometry/RemapIndices");
        const uint32* localIndices = topology->localIndices.data();
        indices.insert(indices.end(), localIndices, localIndices + indexCount);
        if (vertexBase != 0)
        {
            uint32* output = indices.data() + indexBase;
            uint32 offset = static_cast<uint32>(vertexBase);
            for (usize index = 0; index < indexCount; ++index) output[index] += offset;
        }
    }

    return true;
}
