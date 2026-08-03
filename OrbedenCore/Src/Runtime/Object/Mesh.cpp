#include "Runtime/Object/Mesh.h"

#include <algorithm>
#include <cmath>

OBJECT_TYPE_IMPLEMENT(Mesh, Object)

namespace
{
    //判断顶点通道数量是否可用于当前网格
    bool IsChannelCountValid(usize vertexCount, int32 count)
    {
        return count == 0 || (count > 0 && static_cast<usize>(count) == vertexCount);
    }

    //判断索引是否落在顶点范围内
    bool AreIndicesValid(const uint32* data, int32 count, usize vertexCount)
    {
        if (count < 0) return false;
        if (count == 0) return true;
        if (!data || vertexCount == 0) return false;

        for (int32 index = 0; index < count; ++index)
        {
            if (static_cast<usize>(data[index]) >= vertexCount) return false;
        }

        return true;
    }

    //计算向量叉积
    vector3 Cross(const vector3& a, const vector3& b)
    {
        return
        {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }

    //向量相减
    vector3 Subtract(const vector3& a, const vector3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    //累加向量
    void Add(vector3& target, const vector3& value)
    {
        target.x += value.x;
        target.y += value.y;
        target.z += value.z;
    }

    //归一化向量
    vector3 Normalize(const vector3& value)
    {
        float32 lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
        if (lengthSquared <= 0.000001f) return vector3();

        float32 invLength = 1.0f / std::sqrt(lengthSquared);
        return { value.x * invLength, value.y * invLength, value.z * invLength };
    }
}

const bounds3& Mesh::GetLocalBounds() const
{
    if (!IsDirty(MeshDirtyFlags::Bounds)) return localBounds;

    localBounds = bounds3();
    if (!vertices.empty())
    {
        vector3 minValue = vertices[0];
        vector3 maxValue = vertices[0];
        for (usize index = 1; index < vertices.size(); ++index)
        {
            const vector3& point = vertices[index];
            minValue.x = std::min(minValue.x, point.x);
            minValue.y = std::min(minValue.y, point.y);
            minValue.z = std::min(minValue.z, point.z);
            maxValue.x = std::max(maxValue.x, point.x);
            maxValue.y = std::max(maxValue.y, point.y);
            maxValue.z = std::max(maxValue.z, point.z);
        }

        localBounds.center =
        {
            (minValue.x + maxValue.x) * 0.5f,
            (minValue.y + maxValue.y) * 0.5f,
            (minValue.z + maxValue.z) * 0.5f,
        };
        localBounds.extents =
        {
            (maxValue.x - minValue.x) * 0.5f,
            (maxValue.y - minValue.y) * 0.5f,
            (maxValue.z - minValue.z) * 0.5f,
        };
        localBounds.valid = true;
    }

    dirtyFlags &= ~static_cast<uint32>(MeshDirtyFlags::Bounds);
    return localBounds;
}

bool Mesh::IsDirty(MeshDirtyFlags flags) const
{
    return (dirtyFlags & static_cast<uint32>(flags)) != 0;
}

void Mesh::MarkDirty(MeshDirtyFlags flags)
{
    dirtyFlags |= static_cast<uint32>(flags);
}

void Mesh::ClearDirty(MeshDirtyFlags flags)
{
    dirtyFlags &= ~static_cast<uint32>(flags);
}

void Mesh::MarkDirty()
{
    MarkDirty(MeshDirtyFlags::All);
}

void Mesh::ClearGeometry()
{
    vertices.clear();
    texcoords.clear();
    normals.clear();
    tangents.clear();
    indices.clear();
    subMeshes.clear();
    MarkDirty();
}

bool Mesh::SetVertexPositions(const vector3* data, int32 count)
{
    if (count < 0 || (count > 0 && !data)) return false;

    if (count == 0) vertices.clear();
    else vertices.assign(data, data + count);
    if (!texcoords.empty() && texcoords.size() != vertices.size()) texcoords.clear();
    if (!normals.empty() && normals.size() != vertices.size()) normals.clear();
    if (!tangents.empty() && tangents.size() != vertices.size()) tangents.clear();
    if (!AreIndicesValid(indices.empty() ? nullptr : indices.data(), static_cast<int32>(indices.size()), vertices.size())) indices.clear();
    for (SubMesh& subMesh : subMeshes)
    {
        usize indexStart = static_cast<usize>(subMesh.indexStart);
        usize indexCount = static_cast<usize>(subMesh.indexCount);
        if (indexStart > indices.size() || indexCount > indices.size() - indexStart)
        {
            subMesh.indexStart = 0;
            subMesh.indexCount = 0;
        }
    }

    MarkDirty();
    return true;
}

bool Mesh::SetVertexNormals(const vector3* data, int32 count)
{
    if (!IsChannelCountValid(vertices.size(), count) || (count > 0 && !data)) return false;

    if (count == 0) normals.clear();
    else normals.assign(data, data + count);
    MarkDirty(MeshDirtyFlags::Gpu);
    return true;
}

bool Mesh::SetVertexTexcoords(const vector2* data, int32 count)
{
    if (!IsChannelCountValid(vertices.size(), count) || (count > 0 && !data)) return false;

    if (count == 0) texcoords.clear();
    else texcoords.assign(data, data + count);
    MarkDirty(MeshDirtyFlags::Gpu);
    return true;
}

bool Mesh::SetVertexTangents(const vector3* data, int32 count)
{
    if (!IsChannelCountValid(vertices.size(), count) || (count > 0 && !data)) return false;

    if (count == 0) tangents.clear();
    else tangents.assign(data, data + count);
    MarkDirty(MeshDirtyFlags::Gpu);
    return true;
}

bool Mesh::SetIndexData(const uint32* data, int32 count)
{
    if (!AreIndicesValid(data, count, vertices.size())) return false;

    if (count == 0) indices.clear();
    else indices.assign(data, data + count);
    for (SubMesh& subMesh : subMeshes)
    {
        usize indexStart = static_cast<usize>(subMesh.indexStart);
        usize indexCount = static_cast<usize>(subMesh.indexCount);
        if (indexStart > indices.size() || indexCount > indices.size() - indexStart)
        {
            subMesh.indexStart = 0;
            subMesh.indexCount = 0;
        }
    }

    MarkDirty(MeshDirtyFlags::Gpu | MeshDirtyFlags::Physics | MeshDirtyFlags::Editor);
    return true;
}

bool Mesh::ResizeSubMeshes(int32 count)
{
    if (count < 0) return false;

    subMeshes.resize(static_cast<usize>(count));
    MarkDirty(MeshDirtyFlags::Editor);
    return true;
}

bool Mesh::ConfigureSubMesh(int32 index, const std::string& subMeshName, uint32 indexStart, uint32 indexCount, Material* material)
{
    if (index < 0 || static_cast<usize>(index) >= subMeshes.size()) return false;

    usize start = static_cast<usize>(indexStart);
    usize count = static_cast<usize>(indexCount);
    if (start > indices.size() || count > indices.size() - start) return false;

    SubMesh& subMesh = subMeshes[static_cast<usize>(index)];
    subMesh.name = subMeshName;
    subMesh.indexStart = indexStart;
    subMesh.indexCount = indexCount;
    subMesh.material.Set(material);
    MarkDirty(MeshDirtyFlags::Editor);
    return true;
}

bool Mesh::RefreshNormals()
{
    if (indices.size() % 3 != 0) return false;
    if (!AreIndicesValid(indices.empty() ? nullptr : indices.data(), static_cast<int32>(indices.size()), vertices.size())) return false;

    normals.clear();
    normals.resize(vertices.size());
    for (usize index = 0; index < indices.size(); index += 3)
    {
        uint32 ia = indices[index + 0];
        uint32 ib = indices[index + 1];
        uint32 ic = indices[index + 2];

        const vector3& a = vertices[ia];
        const vector3& b = vertices[ib];
        const vector3& c = vertices[ic];
        vector3 normal = Normalize(Cross(Subtract(b, a), Subtract(c, a)));
        Add(normals[ia], normal);
        Add(normals[ib], normal);
        Add(normals[ic], normal);
    }

    for (vector3& normal : normals)
    {
        normal = Normalize(normal);
    }

    MarkDirty(MeshDirtyFlags::Gpu);
    return true;
}
