#include "Rendering/GeometryExpander.h"

#include "Rendering/RenderMath.h"
#include "Profiler/Profiler.h"
#include "Runtime/Object/Mesh.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#if defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define ORBEDEN_EXPANDED_VERTEX_SSE2 1
#else
#define ORBEDEN_EXPANDED_VERTEX_SSE2 0
#endif

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

    //保存本次绘制项共用的变换系数
    struct alignas(16) PreparedVertexTransform
    {
#if ORBEDEN_EXPANDED_VERTEX_SSE2
        __m128 modelColumns[4];
        __m128 normalColumns[3];
        __m128 uvScale;
        __m128 uvOffset;
        __m128 tint;
#else
        matrix4x4 model;
        vector3 normalRows[3];
        color uvRect;
        color tint;
#endif
    };

    //校验模型矩阵并准备位置、法线和材质参数
    bool PrepareVertexTransform(const matrix4x4& model, const color& tint, const color& uvRect,
        PreparedVertexTransform& transform)
    {
        for (uint32 element = 0; element < 16; ++element)
        {
            if (!std::isfinite(model.m[element])) return false;
        }

        vector3 column0 = { model.m[0], model.m[1], model.m[2] };
        vector3 column1 = { model.m[4], model.m[5], model.m[6] };
        vector3 column2 = { model.m[8], model.m[9], model.m[10] };
        float32 determinant = RenderMath::Dot(column0, RenderMath::Cross(column1, column2));
        if (!std::isfinite(determinant) || std::fabs(determinant) < 1.0e-8f) return false;

        //准备原法线算法使用的余子式矩阵
        vector3 row0 = { (column1.y * column2.z - column1.z * column2.y), (column2.y * column0.z - column2.z * column0.y),
            (column0.y * column1.z - column0.z * column1.y) };
        vector3 row1 = { (column1.z * column2.x - column1.x * column2.z), (column2.z * column0.x - column2.x * column0.z),
            (column0.z * column1.x - column0.x * column1.z) };
        vector3 row2 = { (column1.x * column2.y - column1.y * column2.x), (column2.x * column0.y - column2.y * column0.x),
            (column0.x * column1.y - column0.y * column1.x) };
#if ORBEDEN_EXPANDED_VERTEX_SSE2
        for (uint32 column = 0; column < 4; ++column)
            transform.modelColumns[column] = _mm_loadu_ps(model.m + column * 4);
        transform.normalColumns[0] = _mm_setr_ps(row0.x, row1.x, row2.x, 0.0f);
        transform.normalColumns[1] = _mm_setr_ps(row0.y, row1.y, row2.y, 0.0f);
        transform.normalColumns[2] = _mm_setr_ps(row0.z, row1.z, row2.z, 0.0f);
        transform.uvScale = _mm_setr_ps(uvRect.b, uvRect.a, 0.0f, 0.0f);
        transform.uvOffset = _mm_setr_ps(uvRect.r, uvRect.g, 0.0f, 0.0f);
        transform.tint = _mm_setr_ps(tint.r, tint.g, tint.b, tint.a);
#else
        transform.model = model;
        transform.normalRows[0] = row0;
        transform.normalRows[1] = row1;
        transform.normalRows[2] = row2;
        transform.uvRect = uvRect;
        transform.tint = tint;
#endif
        return true;
    }

    //把一个顶点写成世界空间的展开顶点
    GpuExpandedVertex BuildExpandedVertex(const vector3& position, const vector3& sourceNormal,
        const vector2& sourceUv, const vector3& tangentSource, const PreparedVertexTransform& transform)
    {
        GpuExpandedVertex expanded{};
#if ORBEDEN_EXPANDED_VERTEX_SSE2
        //并行计算位置分量并沿用齐次除法条件
        __m128 world = _mm_add_ps(_mm_add_ps(_mm_add_ps(
            _mm_mul_ps(transform.modelColumns[0], _mm_set1_ps(position.x)),
            _mm_mul_ps(transform.modelColumns[1], _mm_set1_ps(position.y))),
            _mm_mul_ps(transform.modelColumns[2], _mm_set1_ps(position.z))), transform.modelColumns[3]);
        alignas(16) float32 values[4];
        _mm_store_ps(values, world);
        float32 w = values[3];
        if (std::abs(w) > 0.000001f && std::abs(w - 1.0f) > 0.000001f)
        {
            world = _mm_mul_ps(world, _mm_set1_ps(1.0f / w));
            _mm_store_ps(values, world);
        }
        expanded.position[0] = values[0];
        expanded.position[1] = values[1];
        expanded.position[2] = values[2];

        //并行计算法线并使用原归一化阈值
        __m128 normal = _mm_add_ps(_mm_add_ps(
            _mm_mul_ps(transform.normalColumns[0], _mm_set1_ps(sourceNormal.x)),
            _mm_mul_ps(transform.normalColumns[1], _mm_set1_ps(sourceNormal.y))),
            _mm_mul_ps(transform.normalColumns[2], _mm_set1_ps(sourceNormal.z)));
        __m128 normalSquares = _mm_mul_ps(normal, normal);
        float32 normalLengthSquared = _mm_cvtss_f32(normalSquares)
            + _mm_cvtss_f32(_mm_shuffle_ps(normalSquares, normalSquares, _MM_SHUFFLE(1, 1, 1, 1)))
            + _mm_cvtss_f32(_mm_shuffle_ps(normalSquares, normalSquares, _MM_SHUFFLE(2, 2, 2, 2)));
        float32 normalLength = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(normalLengthSquared)));
        if (normalLength <= 0.000001f) normal = _mm_setzero_ps();
        else normal = _mm_mul_ps(normal, _mm_set1_ps(1.0f / normalLength));
        _mm_store_ps(values, normal);
        expanded.normal[0] = values[0];
        expanded.normal[1] = values[1];
        expanded.normal[2] = values[2];

        //变换切线并与法线正交化
        __m128 tangent = _mm_add_ps(_mm_add_ps(
            _mm_mul_ps(transform.modelColumns[0], _mm_set1_ps(tangentSource.x)),
            _mm_mul_ps(transform.modelColumns[1], _mm_set1_ps(tangentSource.y))),
            _mm_mul_ps(transform.modelColumns[2], _mm_set1_ps(tangentSource.z)));
        __m128 products = _mm_mul_ps(tangent, normal);
        float32 projection = _mm_cvtss_f32(products)
            + _mm_cvtss_f32(_mm_shuffle_ps(products, products, _MM_SHUFFLE(1, 1, 1, 1)))
            + _mm_cvtss_f32(_mm_shuffle_ps(products, products, _MM_SHUFFLE(2, 2, 2, 2)));
        tangent = _mm_sub_ps(tangent, _mm_mul_ps(normal, _mm_set1_ps(projection)));
        __m128 tangentSquares = _mm_mul_ps(tangent, tangent);
        float32 tangentLengthSquared = _mm_cvtss_f32(tangentSquares)
            + _mm_cvtss_f32(_mm_shuffle_ps(tangentSquares, tangentSquares, _MM_SHUFFLE(1, 1, 1, 1)))
            + _mm_cvtss_f32(_mm_shuffle_ps(tangentSquares, tangentSquares, _MM_SHUFFLE(2, 2, 2, 2)));
        float32 tangentLength = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(tangentLengthSquared)));
        if (tangentLength > 1.0e-6f) tangent = _mm_div_ps(tangent, _mm_set1_ps(tangentLength));
        else tangent = _mm_setzero_ps();
        _mm_store_ps(values, tangent);
        expanded.tangent[0] = values[0];
        expanded.tangent[1] = values[1];
        expanded.tangent[2] = values[2];

        //并行计算 UV 并写入颜色
        __m128 uv = _mm_add_ps(_mm_mul_ps(_mm_setr_ps(sourceUv.x, sourceUv.y, 0.0f, 0.0f),
            transform.uvScale), transform.uvOffset);
        _mm_store_ps(values, uv);
        expanded.uv[0] = values[0];
        expanded.uv[1] = values[1];
        _mm_storeu_ps(expanded.tint, transform.tint);
#else
        vector3 world = RenderMath::TransformPoint(transform.model, position);
        expanded.position[0] = world.x;
        expanded.position[1] = world.y;
        expanded.position[2] = world.z;

        vector3 normal = RenderMath::Normalize({ RenderMath::Dot(transform.normalRows[0], sourceNormal),
            RenderMath::Dot(transform.normalRows[1], sourceNormal), RenderMath::Dot(transform.normalRows[2], sourceNormal) });
        expanded.normal[0] = normal.x;
        expanded.normal[1] = normal.y;
        expanded.normal[2] = normal.z;

        expanded.uv[0] = sourceUv.x * transform.uvRect.b + transform.uvRect.r;
        expanded.uv[1] = sourceUv.y * transform.uvRect.a + transform.uvRect.g;

        vector3 tangent = RenderMath::TransformDirection(transform.model, tangentSource);
        float32 projection = RenderMath::Dot(tangent, normal);
        tangent = { tangent.x - normal.x * projection, tangent.y - normal.y * projection, tangent.z - normal.z * projection };
        float32 tangentLength = std::sqrt(RenderMath::Dot(tangent, tangent));
        if (tangentLength > 1.0e-6f)
            tangent = { tangent.x / tangentLength, tangent.y / tangentLength, tangent.z / tangentLength };
        else tangent = { 0.0f, 0.0f, 0.0f };
        expanded.tangent[0] = tangent.x;
        expanded.tangent[1] = tangent.y;
        expanded.tangent[2] = tangent.z;
        expanded.tint[0] = transform.tint.r;
        expanded.tint[1] = transform.tint.g;
        expanded.tint[2] = transform.tint.b;
        expanded.tint[3] = transform.tint.a;
#endif
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
    PreparedVertexTransform transform;
    {
        PROFILE("Render/Geometry/PrepareTransform");
        if (!PrepareVertexTransform(model, tint, uvRect, transform)) return false;
    }

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
        const vector3* positions = mesh.vertices.data();
        const vector3* normals = mesh.normals.data();
        const vector2* texcoords = mesh.texcoords.data();
        const vector3* tangents = mesh.tangents.data();
        usize normalCount = mesh.normals.size();
        usize texcoordCount = mesh.texcoords.size();
        usize tangentCount = mesh.tangents.size();
        for (usize vertex = 0; vertex < topology->triangleVertexCount; ++vertex)
        {
            uint32 source = sourceVertices[vertex];
            vector3 sourceNormal = source < normalCount ? normals[source] : vector3();
            vector2 sourceUv = source < texcoordCount ? texcoords[source] : vector2();
            vector3 tangentSource = source < tangentCount ? tangents[source] : vector3();
            GpuExpandedVertex expanded = BuildExpandedVertex(positions[source], sourceNormal, sourceUv, tangentSource, transform);
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
