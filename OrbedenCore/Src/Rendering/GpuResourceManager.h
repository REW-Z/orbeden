#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/MaterialShader.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Skybox.h"
#include "Runtime/Object/Texture2D.h"

#include <unordered_map>

//网格上传到 GPU 后持有的缓冲和顶点输入资源。
struct GpuMesh
{
public:
    GpuVertexInputID vertexInput;
    GpuVertexBufferID vertexBuffer;
    GpuIndexBufferID indexBuffer;
    uint32 indexCount = 0;

    bool IsValid() const { return vertexInput.IsValid() && indexBuffer.IsValid() && indexCount > 0; }
};

//shader 上传到 GPU 后持有的 shader program 资源。
struct GpuShader
{
public:
    GpuShaderProgramID shaderProgram;

    bool IsValid() const { return shaderProgram.IsValid(); }
};

//材质上传到 GPU 后持有的 shader、纹理和常量资源。
struct GpuMaterial
{
public:
    GpuShader shader;
    GpuTextureID diffuseTexture;
    vector3 ambient;
    vector3 diffuse = { 1.0f, 1.0f, 1.0f };
    vector3 specular;
    float32 shininess = 1.0f;
    bool hasDiffuseTexture = false;

    bool IsValid() const { return shader.IsValid(); }
};

//CPU 资源到 GPU 资源的上传缓存
class GpuResourceManager
{
private:
    RenderBackend* backend = nullptr;
    std::unordered_map<Mesh*, GpuMesh> meshes;
    std::unordered_map<Texture2D*, GpuTextureID> textures;
    std::unordered_map<Skybox*, GpuCubeTextureID> skyboxes;
    std::unordered_map<MaterialShader*, GpuShader> shaders;
    std::unordered_map<Material*, GpuMaterial> materials;

public:
    //初始化资源管理器
    void Initialize(RenderBackend* renderBackend);

    //释放所有 GPU 资源
    void Shutdown();

    //获取或上传网格
    GpuMesh GetMesh(Mesh* mesh);

    //获取或上传纹理
    GpuTextureID GetTexture(Texture2D* texture);

    //获取或上传天空盒立方体纹理
    GpuCubeTextureID GetSkybox(Skybox* skybox);

    //获取或上传 shader
    GpuShader GetShader(MaterialShader* shader);

    //获取或上传材质
    GpuMaterial GetMaterial(Material* material);
};
