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

    //从资源系统解析管线内置 shader，并保留到下一次使用。
    Shader* ResolveBuiltinShader(Ref<Shader>& shader, const char* key)
    {
        //优先使用已经解析过的资源；无效引用不再重复尝试加载。
        Shader* result = shader.Get();
        if (result || !shader.GetInstanceId().IsValid()) return result;

        //内容资源失效后引用可能只剩实例 ID，此时按固定资源路径重新加载。
        result = ResourceManager::Load<Shader>(key);
        shader.Set(result);
        return result;
    }

    //计算三个分量的向量和。
    vector3 Add(const vector3& a, const vector3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    //计算三个分量的向量差。
    vector3 Sub(const vector3& a, const vector3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    //按标量缩放向量。
    vector3 Scale(const vector3& value, float32 scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    //判断向量长度是否接近零，避免将无效方向送入矩阵计算。
    bool IsZero(const vector3& value)
    {
        return RenderMath::Dot(value, value) <= 0.000001f;
    }

    //从 Pass 三态值解析本次绘制的最终布尔状态
    bool ResolvePassToggle(ShaderPassToggle value, bool baseline)
    {
        if (value == ShaderPassToggle::On) return true;
        if (value == ShaderPassToggle::Off) return false;
        return baseline;
    }

    //从 Pass 剔除设置解析最终模式
    CullMode ResolveCullMode(CullMode value)
    {
        return value == CullMode::Auto ? CullMode::None : value;
    }

    //Forward 光照默认使用场景中的第一盏方向光。
    const RenderDirectionalLight* FindMainLight(const RenderScene& scene)
    {
        return scene.directionalLights.empty() ? nullptr : &scene.directionalLights[0];
    }

    //查找第一盏启用阴影的方向光作为阴影 pass 光源。
    const RenderDirectionalLight* FindShadowLight(const RenderScene& scene)
    {
        for (const RenderDirectionalLight& light : scene.directionalLights)
        {
            if (light.castShadows) return &light;
        }

        return nullptr;
    }

    //计算所有有效渲染项世界包围盒的整体中心。
    vector3 CalculateSceneCenter(const RenderScene& scene)
    {
        bool hasBounds = false;
        vector3 minValue;
        vector3 maxValue;

        //合并每个有效渲染器包围盒的最小点和最大点。
        for (const RendererEntry& entry : scene.renderers)
        {
            if (!entry.active || !entry.worldBounds.valid) continue;

            vector3 itemMin = Sub(entry.worldBounds.center, entry.worldBounds.extents);
            vector3 itemMax = Add(entry.worldBounds.center, entry.worldBounds.extents);
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

        //没有有效几何时回退到世界原点。
        return hasBounds ? Scale(Add(minValue, maxValue), 0.5f) : vector3();
    }

    //根据阴影光源和场景范围构造稳定的正交光源视图投影矩阵。
    matrix4x4 CalculateLightViewProjection(const RenderScene& scene, const RenderDirectionalLight& light)
    {
        //确定光源观察中心，并为无效光源方向提供默认值。
        vector3 center = CalculateSceneCenter(scene);
        vector3 direction = RenderMath::Normalize(light.direction);
        if (IsZero(direction)) direction = RenderMath::Normalize({ -0.35f, -1.0f, -0.45f });

        //让阴影范围至少覆盖一个合理区域，并避免 up 与光线方向平行。
        float32 distance = std::max(light.shadowDistance, 4.0f);
        float32 halfSize = std::max(distance * 0.5f, 8.0f);
        vector3 eye = Sub(center, Scale(direction, distance));
        vector3 up = std::abs(RenderMath::Dot(direction, { 0.0f, 1.0f, 0.0f })) > 0.9f ? vector3{ 0.0f, 0.0f, 1.0f } : vector3{ 0.0f, 1.0f, 0.0f };

        //方向光使用正交投影，覆盖以场景中心为基准的阴影区域。
        matrix4x4 view = RenderMath::LookAt(eye, center, up);
        matrix4x4 projection = RenderMath::Orthographic(-halfSize, halfSize, -halfSize, halfSize, 0.1f, distance * 2.5f);
        return RenderMath::Mul(projection, view);
    }

    //释放天空盒网格的顶点输入和顶点/索引缓冲。
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
    //记录后端；内置资源在首次绘制前按当前内容根目录解析。
    backend = renderBackend;
    builtinResourcesInvalidated = true;
}

void ForwardPipeline::InvalidateResourceCaches()
{
    //先释放管线直接持有的后端资源。
    if (backend)
    {
        backend->DeleteRenderTarget(shadowRenderTarget);
        backend->DeleteDepthTexture(shadowDepthTexture);
        DeleteGpuMesh(backend, skyboxMesh);
    }

    //清空资源引用和当前帧状态，允许后续从新的内容根目录解析。
    shadowRenderTarget = GpuRenderTargetID();
    shadowDepthTexture = GpuDepthTextureID();
    shadowDepthShader.Set(nullptr);
    skyboxShader.Set(nullptr);
    lightViewProjection = matrix4x4();
    shadowReady = false;
    builtinResourcesInvalidated = true;
}

void ForwardPipeline::Shutdown()
{
    InvalidateResourceCaches();
    backend = nullptr;
}

void ForwardPipeline::PrepareFrame(const RenderScene& scene, GpuResourceManager& resources)
{
    //每帧先清除上一帧的阴影状态，避免资源失效后继续采样旧句柄。
    shadowReady = false;
    lightViewProjection = matrix4x4();
    if (!backend) return;

    ResolveBuiltinResources();

    //没有启用阴影的方向光时，不执行阴影资源创建和深度绘制。
    const RenderDirectionalLight* shadowLight = FindShadowLight(scene);
    if (!shadowLight) return;

    //为当前场景计算光源矩阵，并准备可供所有相机共享的深度贴图。
    lightViewProjection = CalculateLightViewProjection(scene, *shadowLight);
    shadowReady = RenderShadowPass(scene, *shadowLight, lightViewProjection, resources);
}

void ForwardPipeline::Render(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& resources)
{
    if (!backend) return;

    //选择当前相机和本帧使用的主光源；没有阴影光源时回退到第一盏方向光。
    const RenderCamera& camera = visibleSet.camera;
    const RenderDirectionalLight* shadowLight = FindShadowLight(scene);
    const RenderDirectionalLight* mainLight = shadowLight ? shadowLight : FindMainLight(scene);

    //建立当前相机的 color pass，并设置默认深度和混合状态。
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

    //仅在 pass 负责清屏时绘制天空盒，避免覆盖保留内容的目标。
    if (camera.clearMode == ClearMode::SolidColor)
    {
        RenderSkybox(scene, camera, resources);
    }

    //按 shader program 记录全局 uniform，避免同一 pass 内重复写入。
    std::unordered_set<uint32> configuredPrograms;

    //遍历已剔除并排序的绘制项，同一对象的 Pass 按声明顺序连续提交。
    for (const RenderItem& item : visibleSet.renderItems)
    {
        //获取或上传 GPU 材质；任一 Pass 无效时跳过整个绘制项。
        const GpuMaterial* material = resources.GetMaterial(item.material);
        if (!material || !material->IsValid())
        {
            Log::Error("ForwardPipeline draw skipped: material GPU resources are invalid.");
            continue;
        }

        //材质有效后再获取网格，避免向后端绑定不完整的顶点资源。
        const GpuMesh* mesh = resources.GetMesh(item.mesh);
        if (!mesh || !mesh->IsValid())
        {
            Log::Error("ForwardPipeline draw skipped: mesh GPU resources are invalid.");
            continue;
        }

        bool transparent = item.drawQueue == DrawQueue::Transparent;
        for (const GpuShaderPass& shaderPass : material->shader.passes)
        {
            //Auto 每次从管线基线解析，绝不继承上一个 Pass 的状态。
            backend->SetDepthTest(ResolvePassToggle(shaderPass.state.depthTest, true));
            backend->SetDepthWrite(ResolvePassToggle(shaderPass.state.depthWrite, !transparent));
            backend->SetBlend(ResolvePassToggle(shaderPass.state.blend, transparent));
            backend->SetCullMode(ResolveCullMode(shaderPass.state.cull));
            backend->BindShaderProgram(shaderPass.shaderProgram);
            backend->SetUniformMatrix4("u_Model", item.localToWorld);

            if (configuredPrograms.insert(shaderPass.shaderProgram.id).second)
            {
                //写入相机、环境光以及阴影光源参数。
                backend->SetUniformMatrix4("u_ViewProjection", camera.viewProjectionMatrix);
                backend->SetUniformMatrix4("u_LightViewProjection", lightViewProjection);
                backend->SetUniformVector3("u_CameraPosition", camera.position);
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

            //每个 program 都有独立 uniform 状态，因此逐 Pass 写入材质绑定。
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

            uint32 materialTextureSlot = static_cast<uint32>(material->textureBindings.size());
            backend->SetUniformInt("u_ShadowMap", static_cast<int32>(materialTextureSlot));
            backend->SetUniformInt("u_UseShadowMap", shadowReady ? 1 : 0);
            backend->BindDepthTexture(materialTextureSlot, shadowReady ? shadowDepthTexture : GpuDepthTextureID());
            backend->SetUniformInt("u_ReceiveShadows", item.receiveShadows ? 1 : 0);
            backend->BindVertexInput(mesh->vertexInput);
            backend->DrawIndexed(item.indexStart, item.indexCount);
        }
    }

    //清理 pass 结束时的绑定和状态，避免影响下一个相机或覆盖层。
    backend->SetBlend(false);
    backend->SetDepthWrite(true);
    backend->SetDepthTest(true);
    backend->SetCullMode(CullMode::None);
    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->EndPass();
}

void ForwardPipeline::ResolveBuiltinResources()
{
    if (!builtinResourcesInvalidated) return;

    shadowDepthShader.Set(ResourceManager::Load<Shader>(ShadowDepthShaderKey));
    skyboxShader.Set(ResourceManager::Load<Shader>(SkyboxShaderKey));
    builtinResourcesInvalidated = false;

    //内置 shader 缺失不会阻止系统运行，但对应 pass 会被跳过。
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

    //已有完整阴影资源时直接复用，避免每帧重新创建。
    if (shadowDepthTexture.IsValid() && shadowRenderTarget.IsValid()) return true;

    //先创建固定尺寸的深度纹理。
    GpuDepthTextureDesc depthDesc;
    depthDesc.width = shadowMapSize;
    depthDesc.height = shadowMapSize;
    shadowDepthTexture = backend->CreateDepthTexture(depthDesc);
    if (!shadowDepthTexture.IsValid())
    {
        Log::Error("ForwardPipeline shadow setup failed: depth texture creation failed.");
        return false;
    }

    //再创建只包含深度附件的阴影渲染目标。
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

    //两项资源均有效后，阴影 pass 才可以开始绘制。
    return true;
}

bool ForwardPipeline::PrepareSkyboxMesh()
{
    if (!backend) return false;

    //天空盒网格是管线内置资源，只在第一次使用时创建。
    if (skyboxMesh.IsValid()) return true;

    //立方体位置占用统一的顶点布局，其他属性填充为默认值。
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

    //将八个立方体顶点转换为后端统一的交错顶点格式。
    float32 vertexData[8 * vertexFloatCount] = {};
    for (uint32 vertex = 0; vertex < 8; ++vertex)
    {
        uint32 offset = vertex * vertexFloatCount;
        vertexData[offset + 0] = positions[vertex][0];
        vertexData[offset + 1] = positions[vertex][1];
        vertexData[offset + 2] = positions[vertex][2];
    }

    //创建顶点和索引缓冲，并记录天空盒的索引数量。
    GpuBufferDesc vertexBufferDesc;
    vertexBufferDesc.data = vertexData;
    vertexBufferDesc.size = sizeof(vertexData);

    GpuBufferDesc indexBufferDesc;
    indexBufferDesc.data = indices;
    indexBufferDesc.size = sizeof(indices);

    skyboxMesh.vertexBuffer = backend->CreateVertexBuffer(vertexBufferDesc);
    skyboxMesh.indexBuffer = backend->CreateIndexBuffer(indexBufferDesc);
    skyboxMesh.indexCount = static_cast<uint32>(sizeof(indices) / sizeof(indices[0]));

    //根据缓冲布局创建顶点输入对象。
    GpuVertexInputDesc inputDesc;
    inputDesc.vertexBuffer = skyboxMesh.vertexBuffer;
    inputDesc.indexBuffer = skyboxMesh.indexBuffer;
    inputDesc.stride = vertexStride;
    skyboxMesh.vertexInput = backend->CreateVertexInput(inputDesc);
    if (!skyboxMesh.IsValid())
    {
        //任一后端句柄创建失败时，回收已经创建的部分资源。
        Log::Error("ForwardPipeline skybox setup failed: cube mesh creation failed.");
        DeleteGpuMesh(backend, skyboxMesh);
        return false;
    }

    return true;
}

bool ForwardPipeline::RenderShadowPass(const RenderScene& scene, const RenderDirectionalLight& light, const matrix4x4& lightViewProjection, GpuResourceManager& resources)
{
    //解析阴影 shader，并确保阴影目标已经准备完成。
    Shader* sourceShader = ResolveBuiltinShader(shadowDepthShader, ShadowDepthShaderKey);
    if (!PrepareShadowResources() || !sourceShader) return false;

    //阴影 shader 必须先上传到 GPU，之后才能开始深度 pass。
    const GpuShader* shader = resources.GetShader(sourceShader);
    if (!shader || !shader->IsValid()) return false;

    //建立只写深度的光源视角 pass。
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

    //使用光源视锥剔除不可能投射到阴影贴图中的对象。
    frustum lightFrustum = RenderMath::BuildFrustum(lightViewProjection);
    for (const RendererEntry& entry : scene.renderers)
    {
        if (!entry.active) continue;
        StaticMeshRenderer* renderer = entry.renderer;
        Mesh* sourceMesh = entry.mesh;
        if (!renderer || !renderer->GetEnabled() || !renderer->castShadows || !sourceMesh) continue;
        if (!RenderMath::Intersects(lightFrustum, entry.worldBounds)) continue;

        //阴影 pass 只需要网格和模型矩阵，不绑定材质纹理。
        const GpuMesh* mesh = resources.GetMesh(sourceMesh);
        if (!mesh || !mesh->IsValid()) continue;

        backend->SetUniformMatrix4("u_Model", entry.localToWorld);
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

    //解除阴影 pass 的绑定，避免深度 shader 泄漏到后续 color pass。
    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->EndPass();
    (void)light;
    return true;
}

void ForwardPipeline::RenderSkybox(const RenderScene& scene, const RenderCamera& camera, GpuResourceManager& resources)
{
    //天空盒开关或内置 shader 缺失时直接跳过天空盒绘制。
    Shader* sourceShader = ResolveBuiltinShader(skyboxShader, SkyboxShaderKey);
    if (!scene.renderSettings.skyboxEnabled || !sourceShader) return;

    //天空盒资源和内置立方体网格必须同时有效。
    Skybox* skybox = scene.renderSettings.skybox.Get();
    if (!skybox || !PrepareSkyboxMesh()) return;

    //上传天空盒立方体纹理和 shader，任一资源无效时跳过绘制。
    GpuCubeTextureID cubeTexture = resources.GetSkybox(skybox);
    const GpuShader* shader = resources.GetShader(sourceShader);
    if (!cubeTexture.IsValid() || !shader || !shader->IsValid()) return;

    //移除相机平移，只保留旋转，使天空盒始终围绕相机绘制。
    matrix4x4 view = camera.viewMatrix;
    view.m[12] = 0.0f;
    view.m[13] = 0.0f;
    view.m[14] = 0.0f;
    matrix4x4 viewProjection = RenderMath::Mul(camera.projectionMatrix, view);

    //关闭深度写入后绘制立方体，再恢复主 pass 的默认状态。
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
