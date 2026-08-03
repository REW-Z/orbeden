#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Skybox.h"
#include "Runtime/Object/Texture2D.h"

#include <memory>
#include <string>

//网格上传到 GPU 后持有的缓冲和顶点输入。
struct GpuMesh
{
public:
    //反向记录来源和活动容器位置，CPU Mesh 只保存非拥有型指针。
    Mesh* source = nullptr;
    usize storageIndex = static_cast<usize>(-1);

    //顶点输入布局以及顶点/索引缓冲。
    GpuVertexInputID vertexInput;
    GpuVertexBufferID vertexBuffer;
    GpuIndexBufferID indexBuffer;

    //本次上传可供绘制的索引数量。
    uint32 indexCount = 0;

    //检查绘制所需的 GPU 句柄和索引数量是否齐全。
    bool IsValid() const
    {
        return vertexInput.IsValid() && vertexBuffer.IsValid() && indexBuffer.IsValid() && indexCount > 0;
    }
};

//单个 Shader Pass 上传后的 GPU program 和固定功能状态
struct GpuShaderPass
{
public:
    GpuShaderProgramID shaderProgram;
    std::string name;
    ShaderPassState state;
};

//Shader 上传到 GPU 后持有的有序 Pass
struct GpuShader
{
public:
    //反向记录来源和活动容器位置，CPU Shader 只保存非拥有型指针。
    Shader* source = nullptr;
    usize storageIndex = static_cast<usize>(-1);

    List<GpuShaderPass> passes;

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

    //保留 CPU 纹理指针，用于依赖失效和诊断来源。
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
    //反向记录来源和活动容器位置，CPU Material 只保存非拥有型指针。
    Material* source = nullptr;
    usize storageIndex = static_cast<usize>(-1);

    const GpuShader* shader = nullptr;

    //保留 CPU shader，用于依赖失效。
    Shader* sourceShader = nullptr;

    //按材质属性类型保存 shader uniform 绑定。
    List<GpuMaterialTextureBinding> textureBindings;
    List<GpuMaterialColorBinding> colorBindings;
    List<GpuMaterialFloatBinding> floatBindings;

};

//CPU 资源到 GPU 资源的上传缓存，CPU 对象通过稳定指针或轻量 ID 直接关联 GPU 状态。
class GpuResourceManager : private IObjectDestroyListener
{
private:
    //所有资源创建和销毁操作使用同一个渲染后端。
    RenderBackend* backend = nullptr;

    //活动包装对象拥有稳定地址，CPU 资源只保存指向它们的非拥有型指针。
    List<std::unique_ptr<GpuMesh>> meshes;
    List<std::unique_ptr<GpuShader>> shaders;
    List<std::unique_ptr<GpuMaterial>> materials;

    //纹理和天空盒直接把轻量 GPU ID 保存在 CPU 资源中，此处只跟踪活动来源。
    List<Texture2D*> textures;
    List<Skybox*> skyboxes;

    //对象析构前摘下的 GPU 状态，在下一帧渲染安全点真正释放。
    List<std::unique_ptr<GpuMesh>> pendingMeshes;
    List<std::unique_ptr<GpuShader>> pendingShaders;
    List<std::unique_ptr<GpuMaterial>> pendingMaterials;
    List<GpuTextureID> pendingTextures;
    List<GpuCubeTextureID> pendingSkyboxes;

    //记录即将销毁的渲染资源对象
    void OnObjectDestroyed(Object* object) override;

    //使依赖指定 Shader 的材质缓存失效
    void InvalidateMaterialsUsingShader(Shader* shader);

    //把依赖指定 Shader 的活动材质标记为脏
    void MarkMaterialsUsingShaderDirty(Shader* shader);

    //使依赖指定纹理的材质缓存失效
    void InvalidateMaterialsUsingTexture(Texture2D* texture);

    //摘下 CPU Mesh 的 GPU 包装并延迟释放
    void QueueMeshRelease(Mesh* mesh);

    //摘下 CPU Shader 的 GPU 包装并延迟释放
    void QueueShaderRelease(Shader* shader);

    //摘下 CPU Material 的 GPU 包装并延迟释放
    void QueueMaterialRelease(Material* material);

    //摘下 CPU Texture 的 GPU ID 并延迟释放
    void QueueTextureRelease(Texture2D* texture);

    //摘下 CPU Skybox 的 GPU ID 并延迟释放
    void QueueSkyboxRelease(Skybox* skybox);

public:
    //注销对象事件并释放仍由管理器持有的 GPU 资源
    ~GpuResourceManager() override;

    //初始化资源管理器并记录渲染后端。
    void Initialize(RenderBackend* renderBackend);

    //释放全部 GPU 缓存并保留当前渲染后端。
    void InvalidateCaches();

    //释放缓存中的所有 GPU 资源并清空索引。
    void Shutdown();

    //获取缓存中的网格；缺少或标记为脏时执行上传。
    const GpuMesh* GetMesh(Mesh* mesh);

    //获取缓存中的纹理；缺少时执行上传。
    GpuTextureID GetTexture(Texture2D* texture);

    //获取缓存中的天空盒立方体纹理；缺少时执行上传。
    GpuCubeTextureID GetSkybox(Skybox* skybox);

    //获取缓存中的 shader；缺少或标记为脏时执行上传。
    const GpuShader* GetShader(Shader* shader);

    //获取缓存中的材质及其 shader、纹理和常量绑定。
    const GpuMaterial* GetMaterial(Material* material);

    //释放对象销毁事件对应的 GPU 缓存
    void ReleaseDestroyedResources();
};
