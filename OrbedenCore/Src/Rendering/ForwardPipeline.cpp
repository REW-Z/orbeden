#include "Rendering/ForwardPipeline.h"

#include "Log/Log.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/Object/Shader.h"

#include <algorithm>
#include <unordered_set>

namespace
{
    constexpr const char* ShadowDepthShaderKey = "Resource/Shader/shadow_depth.orbshader";
    constexpr const char* SkyboxShaderKey = "Resource/Shader/skybox.orbshader";

    Shader* ResolveBuiltinShader(Ref<Shader>& shader, const char* key)
    {
        Shader* result = shader.Get();
        if (result || !shader.GetInstanceId().IsValid()) return result;

        result = ResourceManager::Load<Shader>(key);
        shader.Set(result);
        return result;
    }

    vector3 Add(const vector3& a, const vector3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    vector3 Sub(const vector3& a, const vector3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    vector3 Scale(const vector3& value, float32 scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    bool IsZero(const vector3& value)
    {
        return RenderMath::Dot(value, value) <= 0.000001f;
    }

    const RenderDirectionalLight* FindMainLight(const RenderScene& scene)
    {
        return scene.directionalLights.empty() ? nullptr : &scene.directionalLights[0];
    }

    const RenderDirectionalLight* FindShadowLight(const RenderScene& scene)
    {
        for (const RenderDirectionalLight& light : scene.directionalLights)
        {
            if (light.castShadows) return &light;
        }

        return nullptr;
    }

    vector3 CalculateSceneCenter(const RenderScene& scene)
    {
        bool hasBounds = false;
        vector3 minValue;
        vector3 maxValue;
        for (const RenderItem& item : scene.items)
        {
            if (!item.worldBounds.valid) continue;

            vector3 itemMin = Sub(item.worldBounds.center, item.worldBounds.extents);
            vector3 itemMax = Add(item.worldBounds.center, item.worldBounds.extents);
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

        return hasBounds ? Scale(Add(minValue, maxValue), 0.5f) : vector3();
    }

    matrix4x4 CalculateLightViewProjection(const RenderScene& scene, const RenderDirectionalLight& light)
    {
        vector3 center = CalculateSceneCenter(scene);
        vector3 direction = RenderMath::Normalize(light.direction);
        if (IsZero(direction)) direction = RenderMath::Normalize({ -0.35f, -1.0f, -0.45f });

        float32 distance = std::max(light.shadowDistance, 4.0f);
        float32 halfSize = std::max(distance * 0.5f, 8.0f);
        vector3 eye = Sub(center, Scale(direction, distance));
        vector3 up = std::abs(RenderMath::Dot(direction, { 0.0f, 1.0f, 0.0f })) > 0.9f ? vector3{ 0.0f, 0.0f, 1.0f } : vector3{ 0.0f, 1.0f, 0.0f };

        matrix4x4 view = RenderMath::LookAt(eye, center, up);
        matrix4x4 projection = RenderMath::Orthographic(-halfSize, halfSize, -halfSize, halfSize, 0.1f, distance * 2.5f);
        return RenderMath::Mul(projection, view);
    }

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
    backend = renderBackend;
    shadowDepthShader.Set(ResourceManager::Load<Shader>(ShadowDepthShaderKey));
    skyboxShader.Set(ResourceManager::Load<Shader>(SkyboxShaderKey));
    if (!shadowDepthShader.Get())
    {
        Log::Error("ForwardPipeline initialize warning: shadow depth shader resource is missing.");
    }
    if (!skyboxShader.Get())
    {
        Log::Error("ForwardPipeline initialize warning: skybox shader resource is missing.");
    }
}

void ForwardPipeline::Shutdown()
{
    if (backend)
    {
        backend->DeleteRenderTarget(shadowRenderTarget);
        backend->DeleteDepthTexture(shadowDepthTexture);
        DeleteGpuMesh(backend, skyboxMesh);
    }

    shadowRenderTarget = GpuRenderTargetID();
    shadowDepthTexture = GpuDepthTextureID();
    shadowDepthShader.Set(nullptr);
    skyboxShader.Set(nullptr);
    lightViewProjection = matrix4x4();
    shadowReady = false;
    backend = nullptr;
}

void ForwardPipeline::PrepareFrame(const RenderScene& scene, GpuResourceManager& resources)
{
    shadowReady = false;
    lightViewProjection = matrix4x4();
    if (!backend) return;

    const RenderDirectionalLight* shadowLight = FindShadowLight(scene);
    if (!shadowLight) return;

    lightViewProjection = CalculateLightViewProjection(scene, *shadowLight);
    shadowReady = RenderShadowPass(scene, *shadowLight, lightViewProjection, resources);
}

void ForwardPipeline::Render(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& resources)
{
    if (!backend) return;

    const RenderCamera& camera = visibleSet.camera;
    const RenderDirectionalLight* shadowLight = FindShadowLight(scene);
    const RenderDirectionalLight* mainLight = shadowLight ? shadowLight : FindMainLight(scene);

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

    if (camera.clearMode == ClearMode::SolidColor)
    {
        RenderSkybox(scene, camera, resources);
    }

    std::unordered_set<uint32> configuredPrograms;
    Material* boundMaterial = nullptr;
    const GpuMaterial* boundGpuMaterial = nullptr;
    DrawQueue activeQueue = DrawQueue::Opaque;
    for (const VisibleItem& visibleItem : visibleSet.items)
    {
        if (visibleItem.itemIndex >= scene.items.size()) continue;
        const RenderItem& item = scene.items[visibleItem.itemIndex];
        if (item.drawQueue != activeQueue)
        {
            activeQueue = item.drawQueue;
            bool transparent = activeQueue == DrawQueue::Transparent;
            backend->SetDepthWrite(!transparent);
            backend->SetBlend(transparent);
            boundMaterial = nullptr;
            boundGpuMaterial = nullptr;
        }

        bool materialChanged = boundMaterial != item.material;
        const GpuMaterial* material = materialChanged ? resources.GetMaterial(item.material) : boundGpuMaterial;
        if (!material || !material->IsValid())
        {
            Log::Error("ForwardPipeline draw skipped: material GPU resources are invalid.");
            continue;
        }

        const GpuMesh* mesh = resources.GetMesh(item.mesh);
        if (!mesh || !mesh->IsValid())
        {
            Log::Error("ForwardPipeline draw skipped: mesh GPU resources are invalid.");
            continue;
        }

        backend->BindShaderProgram(material->shader.shaderProgram);
        backend->SetUniformMatrix4("u_Model", item.localToWorld);
        if (configuredPrograms.insert(material->shader.shaderProgram.id).second)
        {
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

        uint32 materialTextureSlot = static_cast<uint32>(material->textureBindings.size());
        if (materialChanged)
        {
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

            backend->SetUniformInt("u_ShadowMap", static_cast<int32>(materialTextureSlot));
            backend->SetUniformInt("u_UseShadowMap", shadowReady ? 1 : 0);
            backend->BindDepthTexture(materialTextureSlot, shadowReady ? shadowDepthTexture : GpuDepthTextureID());
            boundMaterial = item.material;
            boundGpuMaterial = material;
        }

        backend->SetUniformInt("u_ReceiveShadows", item.receiveShadows ? 1 : 0);
        backend->BindVertexInput(mesh->vertexInput);
        backend->DrawIndexed(item.indexStart, item.indexCount);
    }

    backend->SetBlend(false);
    backend->SetDepthWrite(true);
    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->EndPass();
}

bool ForwardPipeline::PrepareShadowResources()
{
    if (!backend) return false;
    if (shadowDepthTexture.IsValid() && shadowRenderTarget.IsValid()) return true;

    GpuDepthTextureDesc depthDesc;
    depthDesc.width = shadowMapSize;
    depthDesc.height = shadowMapSize;
    shadowDepthTexture = backend->CreateDepthTexture(depthDesc);
    if (!shadowDepthTexture.IsValid())
    {
        Log::Error("ForwardPipeline shadow setup failed: depth texture creation failed.");
        return false;
    }

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

    return true;
}

bool ForwardPipeline::PrepareSkyboxMesh()
{
    if (!backend) return false;
    if (skyboxMesh.IsValid()) return true;

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

    float32 vertexData[8 * vertexFloatCount] = {};
    for (uint32 vertex = 0; vertex < 8; ++vertex)
    {
        uint32 offset = vertex * vertexFloatCount;
        vertexData[offset + 0] = positions[vertex][0];
        vertexData[offset + 1] = positions[vertex][1];
        vertexData[offset + 2] = positions[vertex][2];
    }

    GpuBufferDesc vertexBufferDesc;
    vertexBufferDesc.data = vertexData;
    vertexBufferDesc.size = sizeof(vertexData);

    GpuBufferDesc indexBufferDesc;
    indexBufferDesc.data = indices;
    indexBufferDesc.size = sizeof(indices);

    skyboxMesh.vertexBuffer = backend->CreateVertexBuffer(vertexBufferDesc);
    skyboxMesh.indexBuffer = backend->CreateIndexBuffer(indexBufferDesc);
    skyboxMesh.indexCount = static_cast<uint32>(sizeof(indices) / sizeof(indices[0]));

    GpuVertexInputDesc inputDesc;
    inputDesc.vertexBuffer = skyboxMesh.vertexBuffer;
    inputDesc.indexBuffer = skyboxMesh.indexBuffer;
    inputDesc.stride = vertexStride;
    skyboxMesh.vertexInput = backend->CreateVertexInput(inputDesc);
    if (!skyboxMesh.IsValid())
    {
        Log::Error("ForwardPipeline skybox setup failed: cube mesh creation failed.");
        DeleteGpuMesh(backend, skyboxMesh);
        return false;
    }

    return true;
}

bool ForwardPipeline::RenderShadowPass(const RenderScene& scene, const RenderDirectionalLight& light, const matrix4x4& lightViewProjection, GpuResourceManager& resources)
{
    Shader* sourceShader = ResolveBuiltinShader(shadowDepthShader, ShadowDepthShaderKey);
    if (!PrepareShadowResources() || !sourceShader) return false;

    const GpuShader* shader = resources.GetShader(sourceShader);
    if (!shader || !shader->IsValid()) return false;

    RenderPassDesc passDesc;
    passDesc.width = shadowMapSize;
    passDesc.height = shadowMapSize;
    passDesc.renderTarget = shadowRenderTarget;
    passDesc.clearMode = ClearMode::DepthOnly;
    backend->BeginPass(passDesc);
    backend->SetDepthTest(true);
    backend->SetDepthWrite(true);
    backend->SetBlend(false);
    backend->BindShaderProgram(shader->shaderProgram);
    backend->SetUniformMatrix4("u_LightViewProjection", lightViewProjection);

    frustum lightFrustum = RenderMath::BuildFrustum(lightViewProjection);
    for (const RenderItem& item : scene.items)
    {
        if (!item.castShadows) continue;
        if (!RenderMath::Intersects(lightFrustum, item.worldBounds)) continue;

        const GpuMesh* mesh = resources.GetMesh(item.mesh);
        if (!mesh || !mesh->IsValid()) continue;

        backend->SetUniformMatrix4("u_Model", item.localToWorld);
        backend->BindVertexInput(mesh->vertexInput);
        backend->DrawIndexed(item.indexStart, item.indexCount);
    }

    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->EndPass();
    (void)light;
    return true;
}

void ForwardPipeline::RenderSkybox(const RenderScene& scene, const RenderCamera& camera, GpuResourceManager& resources)
{
    Shader* sourceShader = ResolveBuiltinShader(skyboxShader, SkyboxShaderKey);
    if (!scene.renderSettings.skyboxEnabled || !sourceShader) return;

    Skybox* skybox = scene.renderSettings.skybox.Get();
    if (!skybox || !PrepareSkyboxMesh()) return;

    GpuCubeTextureID cubeTexture = resources.GetSkybox(skybox);
    const GpuShader* shader = resources.GetShader(sourceShader);
    if (!cubeTexture.IsValid() || !shader || !shader->IsValid()) return;

    matrix4x4 view = camera.viewMatrix;
    view.m[12] = 0.0f;
    view.m[13] = 0.0f;
    view.m[14] = 0.0f;
    matrix4x4 viewProjection = RenderMath::Mul(camera.projectionMatrix, view);

    backend->SetDepthTest(false);
    backend->SetDepthWrite(false);
    backend->SetBlend(false);
    backend->BindShaderProgram(shader->shaderProgram);
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
