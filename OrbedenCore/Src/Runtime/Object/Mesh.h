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

public:
    std::string name;
    List<vector3> vertices;
    List<vector2> texcoords;
    List<vector3> normals;
    List<vector3> tangents;
    List<uint32> indices;
    List<SubMesh> subMeshes;
};
