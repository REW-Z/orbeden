#pragma once

#include "Defines/types.h"

//通用对象函数表。
struct ObjectBind
{
public:
    void* GetInstanceId = nullptr;
    void* IsAlive = nullptr;
    void* GetManagedWrapper = nullptr;
    void* SetManagedWrapper = nullptr;
    void* Destroy = nullptr;
    void* UnloadUnusedObjects = nullptr;

    //创建 Object 函数表。
    static ObjectBind Create();
};

//通用对象增量函数表。
struct ObjectExtensionBind
{
public:
    void* GetResourceKey = nullptr;

    //创建 Object 增量函数表。
    static ObjectExtensionBind Create();
};

//Mesh 资源函数表。
struct MeshBind
{
public:
    void* Load = nullptr;
    void* IsValid = nullptr;
    void* GetName = nullptr;
    void* GetVertexCount = nullptr;
    void* GetIndexCount = nullptr;
    void* GetSubMeshCount = nullptr;
    void* GetSubMeshName = nullptr;
    void* GetSubMeshIndexStart = nullptr;
    void* GetSubMeshIndexCount = nullptr;
    void* GetSubMeshMaterial = nullptr;
    void* CreateInstance = nullptr;
    void* SetName = nullptr;
    void* GetRevision = nullptr;
    void* GetVertexPositions = nullptr;
    void* SetVertexPositions = nullptr;
    void* GetVertexNormals = nullptr;
    void* SetVertexNormals = nullptr;
    void* GetVertexTexcoords = nullptr;
    void* SetVertexTexcoords = nullptr;
    void* GetVertexTangents = nullptr;
    void* SetVertexTangents = nullptr;
    void* GetIndexData = nullptr;
    void* SetIndexData = nullptr;
    void* ClearGeometry = nullptr;
    void* RefreshNormals = nullptr;
    void* ResizeSubMeshes = nullptr;
    void* ConfigureSubMesh = nullptr;

    //创建 Mesh 函数表。
    static MeshBind Create();
};

//Material 资源函数表。
struct MaterialBind
{
public:
    void* Load = nullptr;
    void* IsValid = nullptr;
    void* GetName = nullptr;
    void* GetShader = nullptr;
    void* SetShader = nullptr;
    void* HasTexture = nullptr;
    void* GetTexture = nullptr;
    void* SetTexture = nullptr;
    void* ClearTexture = nullptr;
    void* HasColor = nullptr;
    void* GetColor = nullptr;
    void* SetColor = nullptr;
    void* ClearColor = nullptr;
    void* HasFloat = nullptr;
    void* GetFloat = nullptr;
    void* SetFloat = nullptr;
    void* ClearFloat = nullptr;
    void* GetRevision = nullptr;
    void* CreateInstance = nullptr;

    //创建 Material 函数表。
    static MaterialBind Create();
};

//Shader 资源函数表。
struct ShaderBind
{
public:
    void* Load = nullptr;
    void* IsValid = nullptr;
    void* GetName = nullptr;
    void* GetVertexPath = nullptr;
    void* GetFragmentPath = nullptr;
    void* GetTextureSlotCount = nullptr;
    void* GetTextureSlotName = nullptr;
    void* GetTextureSlotDisplayName = nullptr;
    void* GetTextureSlotDimension = nullptr;
    void* GetColorSlotCount = nullptr;
    void* GetColorSlotName = nullptr;
    void* GetColorSlotDisplayName = nullptr;
    void* GetColorSlotDefault = nullptr;
    void* GetFloatSlotCount = nullptr;
    void* GetFloatSlotName = nullptr;
    void* GetFloatSlotDisplayName = nullptr;
    void* GetFloatSlotDefault = nullptr;
    void* GetPassCount = nullptr;
    void* GetPassName = nullptr;
    void* GetPassDepthTest = nullptr;
    void* GetPassDepthWrite = nullptr;
    void* GetPassBlend = nullptr;
    void* GetPassCull = nullptr;
    void* CreateFromSource = nullptr;
    void* ReplaceSource = nullptr;
    void* GetRevision = nullptr;

    //创建 Shader 函数表。
    static ShaderBind Create();
};
