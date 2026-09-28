#include "Rendering/GeometryExpander.h"

#include "Rendering/RenderMath.h"
#include "Runtime/Object/Mesh.h"

#include <algorithm>
#include <cmath>

namespace
{
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

    //索引区间是否落在网格范围内，且每个索引都指向存在的顶点
    bool IsIndexRangeValid(const Mesh& mesh, usize start, usize count)
    {
        if (count == 0 || start > mesh.indices.size() || count > mesh.indices.size() - start) return false;
        for (usize index = start; index < start + count; ++index)
        {
            if (mesh.indices[index] >= mesh.vertices.size()) return false;
        }

        return true;
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

uint32 GeometryExpander::CountReferencedVertices(const Mesh& mesh, uint32 indexStart, uint32 indexCount)
{
    usize start = std::min<usize>(indexStart, mesh.indices.size());
    usize count = std::min<usize>(indexCount, mesh.indices.size() - start);
    if (!IsIndexRangeValid(mesh, start, count)) return 0;

    //只统计被索引引用到的顶点，重复引用只算一次
    uint32* stamp = PrepareVertexMapping(mesh.vertices.size());
    uint32 referenced = 0;
    for (usize index = start; index < start + count; ++index)
    {
        usize vertex = mesh.indices[index];
        if (stamp[vertex] == vertexGeneration) continue;
        stamp[vertex] = vertexGeneration;
        ++referenced;
    }

    return referenced;
}

bool GeometryExpander::AppendExpandedMesh(const Mesh& mesh, uint32 subMeshIndex, const matrix4x4& model,
    const color& tint, const color& uvRect, List<GpuExpandedVertex>& vertices, List<uint32>& indices)
{
    if (subMeshIndex >= mesh.subMeshes.size()) return false;
    if (!IsTransformInvertible(model)) return false;

    const SubMesh& subMesh = mesh.subMeshes[subMeshIndex];
    usize start = static_cast<usize>(subMesh.indexStart);
    usize count = static_cast<usize>(subMesh.indexCount);
    if (!IsIndexRangeValid(mesh, start, count)) return false;

    usize indexCount = count - (count % 3);
    if (indexCount == 0) return false;

    //失败时回到调用前的长度，不留下部分追加数据
    usize vertexBase = vertices.size();
    usize indexBase = indices.size();

    //每个被引用顶点只变换一次，索引重定位到本次追加的局部编号
    uint32* stamp = PrepareVertexMapping(mesh.vertices.size());
    for (usize index = start; index < start + indexCount; ++index)
    {
        usize vertex = mesh.indices[index];
        if (stamp[vertex] == vertexGeneration) continue;
        stamp[vertex] = vertexGeneration;

        GpuExpandedVertex expanded = BuildExpandedVertex(mesh, vertex, model, tint, uvRect);
        if (!std::isfinite(expanded.position[0]) || !std::isfinite(expanded.position[1]) || !std::isfinite(expanded.position[2]))
        {
            vertices.resize(vertexBase);
            indices.resize(indexBase);
            return false;
        }

        vertexLocalIndex[vertex] = static_cast<uint32>(vertices.size() - vertexBase);
        vertices.push_back(expanded);
    }

    //重定位：标记表覆盖的正是同一段索引，直接按顶点号取局部编号
    for (usize index = start; index < start + indexCount; ++index)
    {
        indices.push_back(static_cast<uint32>(vertexBase) + vertexLocalIndex[mesh.indices[index]]);
    }

    return true;
}
