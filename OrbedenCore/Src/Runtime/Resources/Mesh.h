#pragma once

#include "Runtime/EngineTypes.h"
#include "Runtime/Object.h"
#include "Runtime/Resources/Material.h"

#include <string>

//网格子集，对应一个连续索引范围和可选材质
struct SubMesh
{
public:
    std::string name;
    uint32 indexStart = 0;
    uint32 indexCount = 0;
    Ref<Material> material;
};

//CPU网格资源，不持有VAO/VBO等渲染API对象
class Mesh : public Object
{
    OBJECT_TYPE_DECLARE(Mesh)

public:
    std::string name;
    List<vector3> vertices;
    List<vector2> texcoords;
    List<vector3> normals;
    List<vector3> tangents;
    List<uint32> indices;
    List<SubMesh> subMeshes;
};
