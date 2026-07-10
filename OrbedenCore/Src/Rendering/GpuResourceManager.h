#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Skybox.h"
#include "Runtime/Object/Texture2D.h"

#include <string>
#include <unordered_map>

//网格上传到 GPU 后持有的缓冲和顶点输入资源。
struct GpuMesh
{
public:
    GpuVertexInputID vertexInput;
    GpuVertexBufferID vertexBuffer;
    GpuIndexBufferID indexBuffer;
    uint32 indexCount = 0;
    uint64 meshRevision = 0;
    std::string sourceKey;

    bool IsValid() const { return vertexInput.IsValid() && indexBuffer.IsValid() && indexCount > 0; }
};

//shader 上传到 GPU 后持有的 shader program 资源。
struct GpuShader
{
public:
    GpuShaderProgramID shaderProgram;
    uint64 shaderRevision = 0;
    std::string sourceKey;

    bool IsValid() const { return shaderProgram.IsValid(); }
};

//材质纹理属性上传后的 GPU 绑定信息。
struct GpuMaterialTextureBinding
{
public:
    std::string uniformName;
    std::string presenceUniformName;
    Texture2D* sourceTexture = nullptr;
    GpuTextureID texture;
    bool hasTexture = false;
};

//材质颜色槽上传后的 GPU 绑定信息。
struct GpuMaterialColorBinding
{
public:
    std::string uniformName;
    color value;
};

//材质浮点槽上传后的 GPU 绑定信息。
struct GpuMaterialFloatBinding
{
public:
    std::string uniformName;
    float32 value = 0.0f;
};

//材质上传到 GPU 后持有的 shader、纹理和常量资源。
struct GpuMaterial
{
public:
    GpuShader shader;
    Shader* sourceShader = nullptr;
    std::string sourceKey;
    uint64 materialRevision = 0;
    uint64 shaderRevision = 0;
    List<GpuMaterialTextureBinding> textureBindings;
    List<GpuMaterialColorBinding> colorBindings;
    List<GpuMaterialFloatBinding> floatBindings;

    bool IsValid() const { return shader.IsValid(); }
};

//CPU 资源到 GPU 资源的上传缓存
class GpuResourceManager
{
private:
    //GPU 缓存统一记录，保存资源数据和对应 CPU 对象的稳定路径。
    template<typename T>
    struct CacheEntry
    {
    public:
        T resource;
        std::string sourceKey;
    };

    RenderBackend* backend = nullptr;
    std::unordered_map<Mesh*, CacheEntry<GpuMesh>> meshes;
    std::unordered_map<Texture2D*, CacheEntry<GpuTextureID>> textures;
    std::unordered_map<Skybox*, CacheEntry<GpuCubeTextureID>> skyboxes;
    std::unordered_map<Shader*, CacheEntry<GpuShader>> shaders;
    std::unordered_map<Material*, CacheEntry<GpuMaterial>> materials;

public:
    //初始化资源管理器
    void Initialize(RenderBackend* renderBackend);

    //释放所有 GPU 资源
    void Shutdown();

    //获取或上传网格
    const GpuMesh* GetMesh(Mesh* mesh);

    //获取或上传纹理
    GpuTextureID GetTexture(Texture2D* texture);

    //获取或上传天空盒立方体纹理
    GpuCubeTextureID GetSkybox(Skybox* skybox);

    //获取或上传 shader
    const GpuShader* GetShader(Shader* shader);

    //获取或上传材质
    const GpuMaterial* GetMaterial(Material* material);

    //清理 CPU 对象已经销毁的 GPU 缓存
    void CollectUnused();
};
