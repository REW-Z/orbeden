#pragma once

#include "Defines/types.h"

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

    //创建 Shader 函数表。
    static ShaderBind Create();
};
