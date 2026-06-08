#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Runtime/Resources/Material.h"
#include "Runtime/Resources/MaterialShader.h"
#include "Runtime/Resources/Mesh.h"
#include "Runtime/Resources/Texture2D.h"

#include <unordered_map>

struct GpuMeshHandle
{
public:
    GpuVertexArrayHandle vertexArray;
    GpuBufferHandle vertexBuffer;
    GpuBufferHandle indexBuffer;
    uint32 indexCount = 0;

    bool IsValid() const { return vertexArray.IsValid() && indexBuffer.IsValid() && indexCount > 0; }
};

struct GpuShaderHandle
{
public:
    GpuProgramHandle program;

    bool IsValid() const { return program.IsValid(); }
};

struct GpuMaterialHandle
{
public:
    GpuShaderHandle shader;
    GpuTextureHandle diffuseTexture;
    vector3 diffuse = { 1.0f, 1.0f, 1.0f };
    bool hasDiffuseTexture = false;

    bool IsValid() const { return shader.IsValid(); }
};

//CPU 资源到 GPU 资源的上传缓存
class GpuResourceManager
{
private:
    RenderBackend* backend = nullptr;
    std::unordered_map<Mesh*, GpuMeshHandle> meshes;
    std::unordered_map<Texture2D*, GpuTextureHandle> textures;
    std::unordered_map<MaterialShader*, GpuShaderHandle> shaders;
    std::unordered_map<Material*, GpuMaterialHandle> materials;

public:
    //初始化资源管理器
    void Initialize(RenderBackend* renderBackend);

    //释放所有 GPU 资源
    void Shutdown();

    //获取或上传网格
    GpuMeshHandle GetMesh(Mesh* mesh);

    //获取或上传纹理
    GpuTextureHandle GetTexture(Texture2D* texture);

    //获取或上传 shader
    GpuShaderHandle GetShader(MaterialShader* shader);

    //获取或上传材质
    GpuMaterialHandle GetMaterial(Material* material);
};

