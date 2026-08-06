#include "Rendering/ForwardPipeline.h"

#include "Log/Log.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/StaticMeshRenderer.h"

#include <algorithm>
#include <unordered_set>

namespace
{
    constexpr const char* ShadowDepthShaderKey = "Resource/Shader/shadow_depth.orbshader";
    constexpr const char* SkyboxShaderKey = "Resource/Shader/skybox.orbshader";

    //获取内置 Shader
    Shader* GetOrLoadBuiltinShader(Ref<Shader>& shader, const char* key)
    {
        //读取已缓存的 Shader
        Shader* result = shader.Get();
        if (result || !shader.GetInstanceId().IsValid()) return result;

        //重新加载内置 Shader
        result = ResourceManager::Load<Shader>(key);
        shader.Set(result);
        return result;
    }

    //计算向量和
    vector3 Add(const vector3& a, const vector3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    //计算向量差
    vector3 Sub(const vector3& a, const vector3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    //缩放向量
    vector3 Scale(const vector3& value, float32 scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    //判断向量是否接近零
    bool IsZero(const vector3& value)
    {
        return RenderMath::Dot(value, value) <= 0.000001f;
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

//计算场景包围盒中心
vector3 ForwardPipeline::CalculateSceneCenter(const RenderScene& scene) const
{
    bool hasBounds = false;
    vector3 minValue;
    vector3 maxValue;

    //合并渲染器包围盒
    for (StaticMeshRenderer* renderer : scene.renderers)
    {
        if (!renderer || !renderer->IsRenderSceneEligible()) continue;

        const StaticMeshRendererRenderState& state = renderer->renderState;
        if (!state.worldBounds.valid) continue;

        vector3 itemMin = Sub(state.worldBounds.center, state.worldBounds.extents);
        vector3 itemMax = Add(state.worldBounds.center, state.worldBounds.extents);
        if (!hasBounds)
        {
            minValue = itemMin;
            maxValue = itemMax;
            hasBounds = true;
            continue;
        }

        minValue.x = std::min(minValue.x, itemMin.x);
        minValue.y = std::min(minValue.y, itemMin.y);
        minValue.z = std::min(minValue.z, itemMin.z);
        maxValue.x = std::max(maxValue.x, itemMax.x);
        maxValue.y = std::max(maxValue.y, itemMax.y);
        maxValue.z = std::max(maxValue.z, itemMax.z);
    }

    //返回场景中心
    return hasBounds ? Scale(Add(minValue, maxValue), 0.5f) : vector3();
}

//计算阴影视图投影矩阵
matrix4x4 ForwardPipeline::CalculateLightViewProjection(const RenderScene& scene, const RenderDirectionalLight& light) const
{
    //计算光源观察参数
    vector3 center = CalculateSceneCenter(scene);
    vector3 direction = RenderMath::Normalize(light.direction);
    if (IsZero(direction)) direction = RenderMath::Normalize({ -0.35f, -1.0f, -0.45f });

    //计算阴影覆盖范围
    float32 distance = std::max(light.shadowDistance, 4.0f);
    float32 halfSize = std::max(distance * 0.5f, 8.0f);
    vector3 eye = Sub(center, Scale(direction, distance));
    vector3 up = std::abs(RenderMath::Dot(direction, { 0.0f, 1.0f, 0.0f })) > 0.9f ? vector3{ 0.0f, 0.0f, 1.0f } : vector3{ 0.0f, 1.0f, 0.0f };

    //构造正交光源矩阵
    matrix4x4 view = RenderMath::LookAt(eye, center, up);
    matrix4x4 projection = RenderMath::Orthographic(-halfSize, halfSize, -halfSize, halfSize, 0.1f, distance * 2.5f);
    return RenderMath::Mul(projection, view);
}

void ForwardPipeline::Initialize(RenderBackend* renderBackend)
{
    //绑定渲染后端
    backend = renderBackend;
    builtinShadersInvalidated = true;
}

void ForwardPipeline::InvalidateResourceCaches()
{
    //释放管线 GPU 资源
    if (backend)
    {
        backend->DeleteRenderTarget(shadowRenderTarget);
        backend->DeleteDepthTexture(shadowDepthTexture);
        DeleteGpuMesh(backend, skyboxMesh);
    }

    //重置管线资源状态
    shadowRenderTarget = GpuRenderTargetID();
    shadowDepthTexture = GpuDepthTextureID();
    shadowDepthShader.Set(nullptr);
    skyboxShader.Set(nullptr);
    lightViewProjection = matrix4x4();
    shadowReady = false;
    builtinShadersInvalidated = true;
}

void ForwardPipeline::Shutdown()
{
    InvalidateResourceCaches();
    backend = nullptr;
}

void ForwardPipeline::PrepareFrame(const RenderScene& scene, GpuResourceManager& gpuResourceManager)
{
    //重置阴影帧状态
    shadowReady = false;
    lightViewProjection = matrix4x4();
    if (!backend) return;

    //加载内置 Shader
    LoadBuiltinShaders();

    //选择阴影方向光
    const RenderDirectionalLight* shadowLight = FindShadowLight(scene);
    if (!shadowLight) return;

    //渲染阴影贴图
    lightViewProjection = CalculateLightViewProjection(scene, *shadowLight);
    shadowReady = RenderShadowPass(scene, *shadowLight, lightViewProjection, gpuResourceManager);
}

void ForwardPipeline::Render(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& gpuResourceManager)
{
    if (!backend) return;

    //选择相机和主方向光
    const RenderCamera& camera = visibleSet.camera;
    const RenderDirectionalLight* shadowLight = FindShadowLight(scene);
    const RenderDirectionalLight* mainLight = shadowLight ? shadowLight : FindMainLight(scene);

    //开始相机主 Pass
    RenderPassDesc passDesc;
    passDesc.x = camera.viewportX;
    passDesc.y = camera.viewportY;
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

    //绘制天空盒
    if (camera.clearMode == ClearMode::SolidColor)
    {
        RenderSkybox(scene, camera, gpuResourceManager);
    }

    //绘制不透明队列
    RenderQueueItems(scene, visibleSet, gpuResourceManager, DrawQueue::Opaque, mainLight, false);

    //绘制普通透明队列
    RenderQueueItems(scene, visibleSet, gpuResourceManager, DrawQueue::Transparent, mainLight, false);

    //复制相机颜色和深度纹理
    bool cameraTexturesReady = false;
    if (camera.cameraTextureTarget.IsValid() && camera.cameraColorTexture.IsValid() && camera.cameraDepthTexture.IsValid())
    {
        GpuRenderTargetCopyDesc copyDesc;
        copyDesc.sourceRenderTarget = camera.renderTarget;
        copyDesc.destinationRenderTarget = camera.cameraTextureTarget;
        copyDesc.sourceX = camera.viewportX;
        copyDesc.sourceY = camera.viewportY;
        copyDesc.width = camera.viewportWidth;
        copyDesc.height = camera.viewportHeight;
        cameraTexturesReady = backend->CopyRenderTargetColorAndDepth(copyDesc);
    }

    //绘制折射队列
    RenderQueueItems(scene, visibleSet, gpuResourceManager, DrawQueue::Refraction, mainLight, cameraTexturesReady);

    //结束相机主 Pass
    backend->SetBlend(false);
    backend->SetDepthWrite(true);
    backend->SetDepthTest(true);
    backend->SetCullMode(CullMode::None);
    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->EndPass();
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
                backend->SetUniformMatrix4("u_LightViewProjection", lightViewProjection);
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
                    backend->SetUniformFloat("u_ShadowBias", mainLight->shadowBias);
                    backend->SetUniformFloat("u_ShadowStrength", mainLight->shadowStrength);
                }
                else
                {
                    backend->SetUniformVector3("u_LightDirection", { 0.0f, -1.0f, 0.0f });
                    backend->SetUniformColor("u_LightColor", { 1.0f, 1.0f, 1.0f, 1.0f });
                    backend->SetUniformFloat("u_LightIntensity", 0.0f);
                    backend->SetUniformFloat("u_ShadowBias", 0.004f);
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
            backend->SetUniformInt("u_ShadowMap", static_cast<int32>(shadowTextureSlot));
            backend->SetUniformInt("u_UseShadowMap", shadowReady ? 1 : 0);
            backend->BindDepthTexture(shadowTextureSlot, shadowReady ? shadowDepthTexture : GpuDepthTextureID());
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

    //阴影深度
    shadowDepthShader.Set(ResourceManager::Load<Shader>(ShadowDepthShaderKey));
    //天空盒
    skyboxShader.Set(ResourceManager::Load<Shader>(SkyboxShaderKey));
    builtinShadersInvalidated = false;

    //记录内置 Shader 加载错误
    if (!shadowDepthShader.Get())
    {
        Log::Error("ForwardPipeline resource resolve warning: shadow depth shader resource is missing.");
    }
    if (!skyboxShader.Get())
    {
        Log::Error("ForwardPipeline resource resolve warning: skybox shader resource is missing.");
    }
}

bool ForwardPipeline::PrepareShadowResources()
{
    if (!backend) return false;

    //复用阴影资源
    if (shadowDepthTexture.IsValid() && shadowRenderTarget.IsValid()) return true;

    //创建阴影深度纹理
    GpuDepthTextureDesc depthDesc;
    depthDesc.width = shadowMapSize;
    depthDesc.height = shadowMapSize;
    shadowDepthTexture = backend->CreateDepthTexture(depthDesc);
    if (!shadowDepthTexture.IsValid())
    {
        Log::Error("ForwardPipeline shadow setup failed: depth texture creation failed.");
        return false;
    }

    //创建阴影渲染目标
    GpuRenderTargetDesc targetDesc;
    targetDesc.width = shadowMapSize;
    targetDesc.height = shadowMapSize;
    targetDesc.depthTexture = shadowDepthTexture;
    targetDesc.depthOnly = true;
    shadowRenderTarget = backend->CreateRenderTarget(targetDesc);
    if (!shadowRenderTarget.IsValid())
    {
        Log::Error("ForwardPipeline shadow setup failed: render target creation failed.");
        backend->DeleteDepthTexture(shadowDepthTexture);
        shadowDepthTexture = GpuDepthTextureID();
        return false;
    }

    //完成阴影资源创建
    return true;
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

bool ForwardPipeline::RenderShadowPass(const RenderScene& scene, const RenderDirectionalLight& light, const matrix4x4& lightViewProjection, GpuResourceManager& gpuResourceManager)
{
    //准备阴影 Shader 和渲染目标
    Shader* sourceShader = GetOrLoadBuiltinShader(shadowDepthShader, ShadowDepthShaderKey);
    if (!PrepareShadowResources() || !sourceShader) return false;

    //上传阴影 Shader
    const GpuShader* shader = gpuResourceManager.GetShader(sourceShader);
    if (!shader) return false;

    //开始阴影 Pass
    RenderPassDesc passDesc;
    passDesc.width = shadowMapSize;
    passDesc.height = shadowMapSize;
    passDesc.renderTarget = shadowRenderTarget;
    passDesc.clearMode = ClearMode::DepthOnly;
    backend->BeginPass(passDesc);
    backend->SetDepthTest(true);
    backend->SetDepthWrite(true);
    backend->SetBlend(false);
    const GpuShaderPass& shaderPass = shader->passes[0];
    backend->BindShaderProgram(shaderPass.shaderProgram);
    backend->SetUniformMatrix4("u_LightViewProjection", lightViewProjection);

    //筛选阴影投射物
    frustum lightFrustum = RenderMath::BuildFrustum(lightViewProjection);
    for (StaticMeshRenderer* renderer : scene.renderers)
    {
        if (!renderer || !renderer->IsRenderSceneEligible() || renderer->drawQueue != DrawQueue::Opaque) continue;

        const StaticMeshRendererRenderState& state = renderer->renderState;
        Mesh* sourceMesh = state.mesh;
        if (!renderer->castShadows || !sourceMesh) continue;
        if (!RenderMath::Intersects(lightFrustum, state.worldBounds)) continue;

        //绘制阴影投射物
        const GpuMesh* mesh = gpuResourceManager.GetMesh(sourceMesh);
        if (!mesh) continue;

        backend->SetUniformMatrix4("u_Model", state.localToWorld);
        backend->BindVertexInput(mesh->vertexInput);
        for (const SubMesh& subMesh : sourceMesh->subMeshes)
        {
            usize start = static_cast<usize>(subMesh.indexStart);
            usize count = static_cast<usize>(subMesh.indexCount);
            if (!subMesh.material.Get() || count == 0 ||
                start > sourceMesh->indices.size() || count > sourceMesh->indices.size() - start) continue;

            backend->DrawIndexed(subMesh.indexStart, subMesh.indexCount);
        }
    }

    //结束阴影 Pass
    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->EndPass();
    (void)light;
    return true;
}

void ForwardPipeline::RenderSkybox(const RenderScene& scene, const RenderCamera& camera, GpuResourceManager& gpuResourceManager)
{
    //获取天空盒 Shader
    Shader* sourceShader = GetOrLoadBuiltinShader(skyboxShader, SkyboxShaderKey);
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
