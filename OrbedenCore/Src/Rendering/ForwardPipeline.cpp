#include "Rendering/ForwardPipeline.h"

#include "Log/Log.h"
#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/Camera.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/CookedAssetSerializer.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/StaticMeshRenderer.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace
{
    //内置 Shader 按文件名在内容根内查找：内容根的目录结构完全自由，不能假定固定路径。
    constexpr const char* ShadowDepthShaderFileName = "shadow_depth.orbshader";
    constexpr const char* SkyboxShaderFileName = "skybox.orbshader";

    //解析结果缓存。内容根变化时随 InvalidateResourceCaches 一起作废。
    struct BuiltinShaderKeys
    {
    public:
        std::string shadowDepth;
        std::string skybox;
    };

    BuiltinShaderKeys& GetBuiltinShaderKeys()
    {
        static BuiltinShaderKeys keys;
        return keys;
    }

    //在内容根内按文件名查找资源 Key；找不到返回空串。
    //打包目录内只有产物与清单、没有源文件，因此存在清单时按清单匹配。
    std::string FindContentKeyByFileName(const std::string& fileName)
    {
        if (!PathDefines::HasContentRoot()) return std::string();

        List<std::string> matches;
        List<std::string> cookedKeys;
        if (CookedAssetSerializer::ReadIndex(cookedKeys))
        {
            for (const std::string& key : cookedKeys)
            {
                if (Utf8Path::ToUtf8(Utf8Path::FromUtf8(key).filename()) != fileName) continue;

                matches.push_back(key);
            }
        }
        else
        {
            //未打包的内容根按文件名递归扫描，目录结构完全自由。
            std::filesystem::path root = Utf8Path::FromUtf8(PathDefines::GetContentRoot());
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator iterator(root, error), end; !error && iterator != end; iterator.increment(error))
            {
                const std::filesystem::directory_entry& entry = *iterator;
                if (!entry.is_regular_file()) continue;
                if (Utf8Path::ToUtf8(entry.path().filename()) != fileName) continue;

                matches.push_back(Utf8Path::ToUtf8(entry.path().lexically_relative(root)));
            }
        }

        if (matches.empty()) return std::string();

        //同名多个时取字典序第一个，保证结果稳定。
        std::sort(matches.begin(), matches.end());
        if (matches.size() > 1)
        {
            Log::Warning(("Multiple files named '" + fileName + "' were found in the content root; using " + matches.front()).c_str());
        }

        return ResourceManager::ToResourceKey(matches.front());
    }

    //获取内置 Shader 的 Key，首次解析后缓存
    const std::string& ResolveBuiltinShaderKey(const char* fileName, std::string& cachedKey)
    {
        if (cachedKey.empty()) cachedKey = FindContentKeyByFileName(fileName);
        return cachedKey;
    }

    //获取内置 Shader
    Shader* GetOrLoadBuiltinShader(Ref<Shader>& shader, const std::string& key)
    {
        //读取已缓存的 Shader
        Shader* result = shader.Get();
        if (result || !shader.GetInstanceId().IsValid()) return result;

        //重新加载内置 Shader
        if (key.empty()) return nullptr;

        result = ResourceManager::Load<Shader>(key);
        shader.Set(result);
        return result;
    }

    //转换 Pass 三态开关
    bool ConvertPassToggleToBool(ShaderPassToggle value, bool baseline)
    {
        if (value == ShaderPassToggle::On) return true;
        if (value == ShaderPassToggle::Off) return false;
        return baseline;
    }

    //转换 Pass 剔除模式
    CullMode ConvertCullModeForBackend(CullMode value)
    {
        return value == CullMode::Auto ? CullMode::None : value;
    }

    //查找主方向光
    const RenderDirectionalLight* FindMainLight(const RenderScene& scene)
    {
        return scene.directionalLights.empty() ? nullptr : &scene.directionalLights[0];
    }

    //查找阴影方向光
    const RenderDirectionalLight* FindShadowLight(const RenderScene& scene)
    {
        for (const RenderDirectionalLight& light : scene.directionalLights)
        {
            if (light.castShadows) return &light;
        }

        return nullptr;
    }

    //释放天空盒 GPU 网格
    void DeleteGpuMesh(RenderBackend* backend, GpuMesh& mesh)
    {
        if (!backend) return;

        backend->DeleteVertexInput(mesh.vertexInput);
        backend->DeleteVertexBuffer(mesh.vertexBuffer);
        backend->DeleteIndexBuffer(mesh.indexBuffer);
        mesh = GpuMesh();
    }
}

void ForwardPipeline::Initialize(RenderBackend* renderBackend)
{
    //绑定渲染后端
    backend = renderBackend;
    shadows.Initialize(backend);
    builtinShadersInvalidated = true;
}

void ForwardPipeline::InvalidateResourceCaches()
{
    //释放管线 GPU 资源
    if (backend)
    {
        shadows.Shutdown();
        shadows.Initialize(backend);
        DeleteGpuMesh(backend, skyboxMesh);
    }

    //重置管线资源状态
    shadowDepthShader.Set(nullptr);
    skyboxShader.Set(nullptr);
    builtinShadersInvalidated = true;
    //内容根可能已经换了，内置 Shader 的解析结果作废。
    GetBuiltinShaderKeys() = BuiltinShaderKeys();
}

void ForwardPipeline::Shutdown()
{
    InvalidateResourceCaches();
    shadows.Shutdown();
    backend = nullptr;
}

void ForwardPipeline::PrepareFrame(const RenderScene& scene, GpuResourceManager& gpuResourceManager)
{
    if (!backend) return;
    LoadBuiltinShaders();
    shadows.BeginFrame(scene);
    (void)gpuResourceManager;
}

void ForwardPipeline::Render(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& gpuResourceManager)
{
    if (!backend) return;

    //选择相机和主方向光
    const RenderCamera& camera = visibleSet.camera;
    const RenderDirectionalLight* shadowLight = FindShadowLight(scene);
    const RenderDirectionalLight* mainLight = shadowLight ? shadowLight : FindMainLight(scene);

    //生成当前相机的级联阴影
    if (shadowLight)
    {
        Shader* depthShader = GetOrLoadBuiltinShader(shadowDepthShader,
            ResolveBuiltinShaderKey(ShadowDepthShaderFileName, GetBuiltinShaderKeys().shadowDepth));
        shadows.Render(scene, camera, *shadowLight, depthShader, gpuResourceManager);
    }

    //开始相机主 Pass。
    //目标是引擎分配的场景缓冲，尺寸就等于视口且原点在 0，视口原点只属于最终输出目标。
    RenderPassDesc passDesc;
    passDesc.x = 0;
    passDesc.y = 0;
    passDesc.width = camera.viewportWidth;
    passDesc.height = camera.viewportHeight;
    passDesc.renderTarget = camera.renderTarget;
    passDesc.clearMode = camera.clearMode;
    passDesc.clearColor = camera.clearColor;
    backend->BeginPass(passDesc);
    backend->SetDepthTest(true);
    backend->SetDepthWrite(true);
    backend->SetBlend(false);
    backend->SetCullMode(CullMode::None);

    //绘制 [天空盒]
    if (camera.clearMode == ClearMode::SolidColor)
    {
        RenderSkybox(scene, camera, gpuResourceManager);
    }

    //绘制 [不透明队列]
    RenderQueueItems(scene, visibleSet, gpuResourceManager, DrawQueue::Opaque, mainLight, false);

    //绘制 [普通透明队列]
    RenderQueueItems(scene, visibleSet, gpuResourceManager, DrawQueue::Transparent, mainLight, false);

    //复制相机颜色和深度纹理
    bool cameraTexturesReady = false;
    if (camera.cameraTextureTarget.IsValid() && camera.cameraColorTexture.IsValid() && camera.cameraDepthTexture.IsValid())
    {
        GpuRenderTargetCopyDesc copyDesc;
        copyDesc.sourceRenderTarget = camera.renderTarget;
        copyDesc.destinationRenderTarget = camera.cameraTextureTarget;
        //源与目标都是视口尺寸、原点为 0 的场景缓冲
        copyDesc.sourceX = 0;
        copyDesc.sourceY = 0;
        copyDesc.width = camera.viewportWidth;
        copyDesc.height = camera.viewportHeight;
        cameraTexturesReady = backend->CopyRenderTarget(copyDesc);
    }

    //绘制 [折射队列]
    RenderQueueItems(scene, visibleSet, gpuResourceManager, DrawQueue::Refraction, mainLight, cameraTexturesReady);

    //结束相机主 Pass
    backend->SetBlend(false);
    backend->SetDepthWrite(true);
    backend->SetDepthTest(true);
    backend->SetCullMode(CullMode::None);
    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->EndPass();
    if (cameraTexturesReady) shadows.CaptureDepth(camera);
}

//绘制指定队列的可见项
void ForwardPipeline::RenderQueueItems(
    const RenderScene& scene,
    const VisibleSet& visibleSet,
    GpuResourceManager& gpuResourceManager,
    DrawQueue drawQueue,
    const RenderDirectionalLight* mainLight,
    bool cameraTexturesReady)
{
    const RenderCamera& camera = visibleSet.camera;
    bool alphaBlended = drawQueue != DrawQueue::Opaque;

    //记录已配置的 Shader Program
    std::unordered_set<uint32> configuredPrograms;
    for (const RenderItem& item : visibleSet.renderItems)
    {
        if (item.drawQueue != drawQueue) continue;

        //获取绘制资源
        const GpuMaterial* material = gpuResourceManager.GetMaterial(item.material);
        if (!material)
        {
            Log::Error("ForwardPipeline draw skipped: material GPU resources are invalid.");
            continue;
        }

        const GpuMesh* mesh = gpuResourceManager.GetMesh(item.mesh);
        if (!mesh)
        {
            Log::Error("ForwardPipeline draw skipped: mesh GPU resources are invalid.");
            continue;
        }

        for (const GpuShaderPass& shaderPass : material->shader->passes)
        {
            //配置 Shader Pass 状态
            backend->SetDepthTest(ConvertPassToggleToBool(shaderPass.state.depthTest, true));
            backend->SetDepthWrite(ConvertPassToggleToBool(shaderPass.state.depthWrite, !alphaBlended));
            backend->SetBlend(ConvertPassToggleToBool(shaderPass.state.blend, alphaBlended));
            backend->SetCullMode(ConvertCullModeForBackend(shaderPass.state.cull));
            backend->BindShaderProgram(shaderPass.shaderProgram);
            backend->SetUniformMatrix4("u_Model", item.localToWorld);

            if (configuredPrograms.insert(shaderPass.shaderProgram.id).second)
            {
                //绑定全局渲染参数
                backend->SetUniformMatrix4("u_ViewProjection", camera.viewProjectionMatrix);
                shadows.BindUniforms(camera);
                backend->SetUniformVector3("u_CameraPosition", camera.position);
                backend->SetUniformFloat("u_CameraNearPlane", camera.nearPlane);
                backend->SetUniformFloat("u_CameraFarPlane", camera.farPlane);
                backend->SetUniformFloat("u_Time", camera.elapsedTime);
                backend->SetUniformInt("u_UseCameraTextures", drawQueue == DrawQueue::Refraction && cameraTexturesReady ? 1 : 0);
                backend->SetUniformColor("u_AmbientColor", scene.renderSettings.ambientColor);
                if (mainLight)
                {
                    backend->SetUniformVector3("u_LightDirection", mainLight->direction);
                    backend->SetUniformColor("u_LightColor", mainLight->color);
                    backend->SetUniformFloat("u_LightIntensity", mainLight->intensity);
                    backend->SetUniformFloat("u_ShadowStrength", std::clamp(mainLight->shadowStrength, 0.0f, 1.0f));
                }
                else
                {
                    backend->SetUniformVector3("u_LightDirection", { 0.0f, -1.0f, 0.0f });
                    backend->SetUniformColor("u_LightColor", { 1.0f, 1.0f, 1.0f, 1.0f });
                    backend->SetUniformFloat("u_LightIntensity", 0.0f);
                    backend->SetUniformFloat("u_ShadowStrength", 0.0f);
                }
            }

            //绑定材质参数
            for (const GpuMaterialColorBinding& binding : material->colorBindings)
            {
                backend->SetUniformColor(binding.uniformName.c_str(), binding.value);
            }
            for (const GpuMaterialFloatBinding& binding : material->floatBindings)
            {
                backend->SetUniformFloat(binding.uniformName.c_str(), binding.value);
            }

            for (uint32 slot = 0; slot < material->textureBindings.size(); ++slot)
            {
                const GpuMaterialTextureBinding& binding = material->textureBindings[slot];
                backend->SetUniformInt(binding.uniformName.c_str(), static_cast<int32>(slot));
                backend->SetUniformInt(binding.presenceUniformName.c_str(), binding.hasTexture ? 1 : 0);
                backend->BindTexture(slot, binding.hasTexture ? binding.texture : GpuTextureID());
            }

            //绑定内置渲染纹理
            uint32 shadowTextureSlot = static_cast<uint32>(material->textureBindings.size());
            shadows.BindTexture(shadowTextureSlot);
            backend->SetUniformInt("u_ReceiveShadows", item.receiveShadows ? 1 : 0);

            if (drawQueue == DrawQueue::Refraction)
            {
                uint32 cameraColorSlot = shadowTextureSlot + 1;
                uint32 cameraDepthSlot = shadowTextureSlot + 2;
                backend->SetUniformInt("u_CameraColorTexture", static_cast<int32>(cameraColorSlot));
                backend->SetUniformInt("u_CameraDepthTexture", static_cast<int32>(cameraDepthSlot));
                backend->BindTexture(cameraColorSlot, cameraTexturesReady ? camera.cameraColorTexture : GpuTextureID());
                backend->BindDepthTexture(cameraDepthSlot, cameraTexturesReady ? camera.cameraDepthTexture : GpuDepthTextureID());
            }

            backend->BindVertexInput(mesh->vertexInput);
            backend->DrawIndexed(item.indexStart, item.indexCount);
        }
    }
}

void ForwardPipeline::LoadBuiltinShaders()
{
    if (!builtinShadersInvalidated) return;

    //Editor 尚未打开项目时没有内容根；等项目加载后再解析项目内置 Shader。
    if (!PathDefines::HasContentRoot()) return;

    //阴影深度
    BuiltinShaderKeys& keys = GetBuiltinShaderKeys();
    shadowDepthShader.Set(ResourceManager::Load<Shader>(ResolveBuiltinShaderKey(ShadowDepthShaderFileName, keys.shadowDepth)));
    //天空盒
    skyboxShader.Set(ResourceManager::Load<Shader>(ResolveBuiltinShaderKey(SkyboxShaderFileName, keys.skybox)));
    builtinShadersInvalidated = false;

    //记录内置 Shader 加载错误
    if (!shadowDepthShader.Get())
    {
        Log::Error("ForwardPipeline: shadow_depth.orbshader was not found in the content root.");
    }
    if (!skyboxShader.Get())
    {
        Log::Error("ForwardPipeline: skybox.orbshader was not found in the content root.");
    }
}

bool ForwardPipeline::PrepareSkyboxMesh()
{
    if (!backend) return false;

    //复用天空盒网格
    if (skyboxMesh.IsValid()) return true;

    //定义天空盒立方体数据
    constexpr uint32 vertexFloatCount = 11;
    constexpr uint32 vertexStride = vertexFloatCount * sizeof(float32);
    const float32 positions[8][3] =
    {
        { -1.0f, -1.0f, -1.0f },
        { 1.0f, -1.0f, -1.0f },
        { 1.0f, 1.0f, -1.0f },
        { -1.0f, 1.0f, -1.0f },
        { -1.0f, -1.0f, 1.0f },
        { 1.0f, -1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f },
        { -1.0f, 1.0f, 1.0f },
    };
    const uint32 indices[] =
    {
        0, 1, 2, 2, 3, 0,
        4, 6, 5, 6, 4, 7,
        0, 4, 5, 5, 1, 0,
        3, 2, 6, 6, 7, 3,
        1, 5, 6, 6, 2, 1,
        0, 3, 7, 7, 4, 0,
    };

    //构造天空盒顶点数据
    float32 vertexData[8 * vertexFloatCount] = {};
    for (uint32 vertex = 0; vertex < 8; ++vertex)
    {
        uint32 offset = vertex * vertexFloatCount;
        vertexData[offset + 0] = positions[vertex][0];
        vertexData[offset + 1] = positions[vertex][1];
        vertexData[offset + 2] = positions[vertex][2];
    }

    //创建天空盒顶点和索引缓冲
    GpuBufferDesc vertexBufferDesc;
    vertexBufferDesc.data = vertexData;
    vertexBufferDesc.size = sizeof(vertexData);

    GpuBufferDesc indexBufferDesc;
    indexBufferDesc.data = indices;
    indexBufferDesc.size = sizeof(indices);

    skyboxMesh.vertexBuffer = backend->CreateVertexBuffer(vertexBufferDesc);
    skyboxMesh.indexBuffer = backend->CreateIndexBuffer(indexBufferDesc);
    skyboxMesh.indexCount = static_cast<uint32>(sizeof(indices) / sizeof(indices[0]));

    //创建天空盒顶点输入
    GpuVertexInputDesc inputDesc;
    inputDesc.vertexBuffer = skyboxMesh.vertexBuffer;
    inputDesc.indexBuffer = skyboxMesh.indexBuffer;
    inputDesc.stride = vertexStride;
    skyboxMesh.vertexInput = backend->CreateVertexInput(inputDesc);
    if (!skyboxMesh.IsValid())
    {
        //回收天空盒网格资源
        Log::Error("ForwardPipeline skybox setup failed: cube mesh creation failed.");
        DeleteGpuMesh(backend, skyboxMesh);
        return false;
    }

    return true;
}

void ForwardPipeline::RenderSkybox(const RenderScene& scene, const RenderCamera& camera, GpuResourceManager& gpuResourceManager)
{
    //获取天空盒 Shader
    Shader* sourceShader = GetOrLoadBuiltinShader(skyboxShader,
        ResolveBuiltinShaderKey(SkyboxShaderFileName, GetBuiltinShaderKeys().skybox));
    if (!scene.renderSettings.skyboxEnabled || !sourceShader) return;

    //获取天空盒资源
    Skybox* skybox = scene.renderSettings.skybox.Get();
    if (!skybox || !PrepareSkyboxMesh()) return;

    //上传天空盒 GPU 资源
    GpuCubeTextureID cubeTexture = gpuResourceManager.GetSkybox(skybox);
    const GpuShader* shader = gpuResourceManager.GetShader(sourceShader);
    if (!cubeTexture.IsValid() || !shader) return;

    //计算天空盒视图投影矩阵
    matrix4x4 view = camera.viewMatrix;
    view.m[12] = 0.0f;
    view.m[13] = 0.0f;
    view.m[14] = 0.0f;
    matrix4x4 viewProjection = RenderMath::Mul(camera.projectionMatrix, view);

    //绘制天空盒立方体
    backend->SetDepthTest(false);
    backend->SetDepthWrite(false);
    backend->SetBlend(false);
    const GpuShaderPass& shaderPass = shader->passes[0];
    backend->BindShaderProgram(shaderPass.shaderProgram);
    backend->SetUniformMatrix4("u_ViewProjection", viewProjection);
    backend->SetUniformInt("u_SkyboxTexture", 0);
    backend->BindCubeTexture(0, cubeTexture);
    backend->BindVertexInput(skyboxMesh.vertexInput);
    backend->DrawIndexed(0, skyboxMesh.indexCount);
    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->SetDepthWrite(true);
    backend->SetDepthTest(true);
}
