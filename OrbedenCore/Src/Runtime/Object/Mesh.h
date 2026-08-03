#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EngineTypes.h"
#include "Runtime/Object/Object.h"
#include "Runtime/Object/Material.h"

#include <string>

struct GpuMesh;
class GpuResourceManager;

//网格数据变更的独立消费方
enum class MeshDirtyFlags : uint32
{
    None = 0,
    Gpu = 1u << 0,
    Bounds = 1u << 1,
    Render = 1u << 2,
    Physics = 1u << 3,
    Editor = 1u << 4,
    All = (1u << 5) - 1u,
};

constexpr MeshDirtyFlags operator|(MeshDirtyFlags left, MeshDirtyFlags right)
{
    return static_cast<MeshDirtyFlags>(static_cast<uint32>(left) | static_cast<uint32>(right));
}

//子网格
struct SubMesh
{
public:
    std::string name;
    uint32 indexStart = 0;
    uint32 indexCount = 0;
    Ref<Material> material;
};

//CPU网格资源
class Mesh : public Object
{
    OBJECT_TYPE_DECLARE(Mesh)

private:
    friend class GpuResourceManager;

    //GPU 网格由资源管理器持有。
    GpuMesh* gpuMesh = nullptr;
    mutable uint32 dirtyFlags = static_cast<uint32>(MeshDirtyFlags::All);
    mutable bounds3 localBounds;

public:
    std::string name;
    List<vector3> vertices;
    List<vector2> texcoords;
    List<vector3> normals;
    List<vector3> tangents;
    List<uint32> indices;
    List<SubMesh> subMeshes;

    //获取按脏标记缓存的本地包围盒
    const bounds3& GetLocalBounds() const;

    //判断指定消费方是否需要刷新
    bool IsDirty(MeshDirtyFlags flags) const;

    //标记指定消费方需要刷新
    void MarkDirty(MeshDirtyFlags flags);

    //清除指定消费方的刷新标记
    void ClearDirty(MeshDirtyFlags flags);

    //标记所有消费方需要刷新
    void MarkDirty();

    //清空所有几何数据
    void ClearGeometry();

    //写入顶点位置
    bool SetVertexPositions(const vector3* data, int32 count);

    //写入顶点法线
    bool SetVertexNormals(const vector3* data, int32 count);

    //写入顶点 UV
    bool SetVertexTexcoords(const vector2* data, int32 count);

    //写入顶点切线
    bool SetVertexTangents(const vector3* data, int32 count);

    //写入索引数据
    bool SetIndexData(const uint32* data, int32 count);

    //调整子网格数量
    bool ResizeSubMeshes(int32 count);

    //配置子网格
    bool ConfigureSubMesh(int32 index, const std::string& subMeshName, uint32 indexStart, uint32 indexCount, Material* material);

    //根据三角形索引重新计算法线
    bool RefreshNormals();
};
