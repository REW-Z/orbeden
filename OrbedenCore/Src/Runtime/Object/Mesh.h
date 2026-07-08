#pragma once

#include "Runtime/EngineTypes.h"
#include "Runtime/Object/Object.h"
#include "Runtime/Object/Material.h"

#include <string>

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
    uint64 revision = 1;

public:
    std::string name;
    List<vector3> vertices;
    List<vector2> texcoords;
    List<vector3> normals;
    List<vector3> tangents;
    List<uint32> indices;
    List<SubMesh> subMeshes;

    //获取网格版本，用于刷新 GPU 缓存
    uint64 GetRevision() const;

    //标记网格数据已修改
    void TouchRevision();

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
