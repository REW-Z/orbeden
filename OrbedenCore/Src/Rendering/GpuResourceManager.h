#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Skybox.h"
#include "Runtime/Object/Texture2D.h"

#include <string>
#include <unordered_map>

//网格上传到 GPU 后持有的缓冲、顶点输入和版本信息。
struct GpuMesh
{
public:
    //顶点输入布局以及顶点/索引缓冲。
    GpuVertexInputID vertexInput;
    GpuVertexBufferID vertexBuffer;
    GpuIndexBufferID indexBuffer;

    //本次上传可供绘制的索引数量。
    uint32 indexCount = 0;

    //用于判断 CPU 网格是否发生变化的版本号。
    uint64 meshRevision = 0;

    //检查绘制所需的 GPU 句柄和索引数量是否齐全。
    bool IsValid() const { return vertexInput.IsValid() && indexBuffer.IsValid() && indexCount > 0; }
};

//单个 Shader Pass 上传后的 GPU program 和固定功能状态
struct GpuShaderPass
{
public:
    GpuShaderProgramID shaderProgram;
    std::string name;
    ShaderPassState state;
};

//Shader 上传到 GPU 后持有的有序 Pass 和版本信息
struct GpuShader
{
public:
    List<GpuShaderPass> passes;

    //用于判断 CPU shader 是否需要重新上传的版本号。
    uint64 shaderRevision = 0;

    //检查所有 Pass 的 shader program 是否有效
    bool IsValid() const
    {
        if (passes.empty()) return false;
        for (const GpuShaderPass& pass : passes)
        {
            if (!pass.shaderProgram.IsValid()) return false;
        }
        return true;
    }
};

//材质纹理属性上传后的 GPU 绑定信息。
struct GpuMaterialTextureBinding
{
public:
    //材质属性对应的 sampler 名称及可选纹理存在标记名称。
    std::string uniformName;
    std::string presenceUniformName;

    //保留 CPU 纹理指针，用于检测资源版本和诊断来源。
    Texture2D* sourceTexture = nullptr;
    GpuTextureID texture;
    bool hasTexture = false;
};

//材质颜色槽上传后的 GPU 绑定信息。
struct GpuMaterialColorBinding
{
public:
    //材质属性对应的 uniform 名称和值。
    std::string uniformName;
    color value;
};

//材质浮点槽上传后的 GPU 绑定信息。
struct GpuMaterialFloatBinding
{
public:
    //材质属性对应的 uniform 名称和值。
    std::string uniformName;
    float32 value = 0.0f;
};

//材质上传到 GPU 后持有的 shader、纹理和常量绑定。
struct GpuMaterial
{
public:
    GpuShader shader;

    //保留 CPU shader，用于版本检查。
    Shader* sourceShader = nullptr;
    uint64 materialRevision = 0;
    uint64 shaderRevision = 0;

    //按材质属性类型保存 shader uniform 绑定。
    List<GpuMaterialTextureBinding> textureBindings;
    List<GpuMaterialColorBinding> colorBindings;
    List<GpuMaterialFloatBinding> floatBindings;

    //材质至少需要拥有有效的 shader program 才能参与绘制。
    bool IsValid() const { return shader.IsValid(); }
};

//CPU 资源到 GPU 资源的上传缓存，按对象销毁事件精确释放缓存。
class GpuResourceManager : private IObjectDestroyListener
{
private:
    struct PendingResourceRelease
    {
        int32 objectId = 0;
        Type* type = nullptr;
    };

    //所有资源创建和销毁操作使用同一个渲染后端。
    RenderBackend* backend = nullptr;

    //按运行时对象 ID 缓存已经上传的 GPU 资源。
    std::unordered_map<int32, GpuMesh> meshes;
    std::unordered_map<int32, GpuTextureID> textures;
    std::unordered_map<int32, GpuCubeTextureID> skyboxes;
    std::unordered_map<int32, GpuShader> shaders;
    std::unordered_map<int32, GpuMaterial> materials;

    //等待下一个渲染安全点释放的对象身份。
    List<PendingResourceRelease> pendingReleases;

    //记录即将销毁的渲染资源对象
    void OnObjectDestroyed(Object* object) override;

public:
    //注销对象事件并释放仍由管理器持有的 GPU 资源
    ~GpuResourceManager() override;

    //初始化资源管理器并记录渲染后端。
    void Initialize(RenderBackend* renderBackend);

    //释放全部 GPU 缓存并保留当前渲染后端。
    void InvalidateCaches();

    //释放缓存中的所有 GPU 资源并清空索引。
    void Shutdown();

    //获取缓存中的网格；缺少或版本过期时执行上传。
    const GpuMesh* GetMesh(Mesh* mesh);

    //获取缓存中的纹理；缺少或版本过期时执行上传。
    GpuTextureID GetTexture(Texture2D* texture);

    //获取缓存中的天空盒立方体纹理；缺少或版本过期时执行上传。
    GpuCubeTextureID GetSkybox(Skybox* skybox);

    //获取缓存中的 shader；缺少或版本过期时执行上传。
    const GpuShader* GetShader(Shader* shader);

    //获取缓存中的材质及其 shader、纹理和常量绑定。
    const GpuMaterial* GetMaterial(Material* material);

    //释放对象销毁事件对应的 GPU 缓存
    void ReleaseDestroyedResources();
};
