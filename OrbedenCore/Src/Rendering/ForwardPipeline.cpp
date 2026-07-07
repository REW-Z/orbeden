#include "Rendering/ForwardPipeline.h"

#include "Log/Log.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/Object/Shader.h"

#include <algorithm>

namespace
{
    constexpr const char* ShadowDepthShaderKey = "Resource/Shader/shadow_depth.orbshader";
    constexpr const char* SkyboxShaderKey = "Resource/Shader/skybox.orbshader";

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
    shadowDepthShader = ResourceManager::Load<Shader>(ShadowDepthShaderKey);
    skyboxShader = ResourceManager::Load<Shader>(SkyboxShaderKey);
    if (!shadowDepthShader)
    {
        Log::Error("ForwardPipeline initialize warning: shadow depth shader resource is missing.");
    }
    if (!skyboxShader)
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
    shadowDepthShader = nullptr;
    skyboxShader = nullptr;
    ResourceManager::Unload(ShadowDepthShaderKey);
    ResourceManager::Unload(SkyboxShaderKey);
    backend = nullptr;
}

void ForwardPipeline::Render(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& resources)
{
    if (!backend) return;

    const RenderCamera& camera = visibleSet.camera;
    const RenderDirectionalLight* shadowLight = FindShadowLight(scene);
    const RenderDirectionalLight* mainLight = shadowLight ? shadowLight : FindMainLight(scene);
    matrix4x4 lightViewProjection;
    bool shadowReady = false;
    if (shadowLight)
    {
        lightViewProjection = CalculateLightViewProjection(scene, *shadowLight);
        shadowReady = RenderShadowPass(scene, *shadowLight, lightViewProjection, resources);
    }

    RenderPassDesc passDesc;
    passDesc.width = camera.viewportWidth;
    passDesc.height = camera.viewportHeight;
    passDesc.clearMode = camera.camera ? camera.camera->clearMode : ClearMode::SolidColor;
    passDesc.clearColor = camera.camera ? camera.camera->clearColor : color();
    backend->BeginPass(passDesc);
    backend->SetDepthTest(true);
    backend->SetDepthWrite(true);

    RenderSkybox(scene, camera, resources);

    for (const RenderItem& item : visibleSet.items)
    {
        GpuMaterial material = resources.GetMaterial(item.material);
        if (!material.IsValid())
        {
            Log::Error("ForwardPipeline draw skipped: material GPU resources are invalid.");
            continue;
        }

        GpuMesh mesh = resources.GetMesh(item.mesh);
        if (!mesh.IsValid())
        {
            Log::Error("ForwardPipeline draw skipped: mesh GPU resources are invalid.");
            continue;
        }

        backend->BindShaderProgram(material.shader.shaderProgram);
        backend->SetUniformMatrix4("u_Model", item.localToWorld);
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

        for (const GpuMaterialColorBinding& binding : material.colorBindings)
        {
            backend->SetUniformColor(binding.uniformName.c_str(), binding.value);
        }

        for (const GpuMaterialFloatBinding& binding : material.floatBindings)
        {
            backend->SetUniformFloat(binding.uniformName.c_str(), binding.value);
        }

        uint32 materialTextureSlot = 0;
        for (const GpuMaterialTextureBinding& binding : material.textureBindings)
        {
            backend->SetUniformInt(binding.uniformName.c_str(), static_cast<int32>(materialTextureSlot));
            backend->SetUniformInt(binding.presenceUniformName.c_str(), binding.hasTexture ? 1 : 0);
            if (binding.hasTexture)
            {
                backend->BindTexture(materialTextureSlot, binding.texture);
            }
            else
            {
                backend->BindTexture(materialTextureSlot, GpuTextureID());
            }

            materialTextureSlot++;
        }

        uint32 shadowTextureSlot = materialTextureSlot;
        backend->SetUniformInt("u_ShadowMap", static_cast<int32>(shadowTextureSlot));
        backend->SetUniformInt("u_UseShadowMap", shadowReady ? 1 : 0);
        backend->SetUniformInt("u_ReceiveShadows", item.receiveShadows ? 1 : 0);
        if (shadowReady)
        {
            backend->BindDepthTexture(shadowTextureSlot, shadowDepthTexture);
        }
        else
        {
            backend->BindDepthTexture(shadowTextureSlot, GpuDepthTextureID());
        }

        backend->BindVertexInput(mesh.vertexInput);
        backend->DrawIndexed(item.indexStart, item.indexCount);
    }

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
    if (!PrepareShadowResources() || !shadowDepthShader) return false;

    GpuShader shader = resources.GetShader(shadowDepthShader);
    if (!shader.IsValid()) return false;

    RenderPassDesc passDesc;
    passDesc.width = shadowMapSize;
    passDesc.height = shadowMapSize;
    passDesc.renderTarget = shadowRenderTarget;
    passDesc.clearMode = ClearMode::DepthOnly;
    backend->BeginPass(passDesc);
    backend->SetDepthTest(true);
    backend->SetDepthWrite(true);
    backend->BindShaderProgram(shader.shaderProgram);
    backend->SetUniformMatrix4("u_LightViewProjection", lightViewProjection);

    for (const RenderItem& item : scene.items)
    {
        if (!item.castShadows) continue;

        GpuMesh mesh = resources.GetMesh(item.mesh);
        if (!mesh.IsValid()) continue;

        backend->SetUniformMatrix4("u_Model", item.localToWorld);
        backend->BindVertexInput(mesh.vertexInput);
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
    if (!scene.renderSettings.skyboxEnabled || !skyboxShader) return;

    Skybox* skybox = scene.renderSettings.skybox.Get();
    if (!skybox || !PrepareSkyboxMesh()) return;

    GpuCubeTextureID cubeTexture = resources.GetSkybox(skybox);
    GpuShader shader = resources.GetShader(skyboxShader);
    if (!cubeTexture.IsValid() || !shader.IsValid()) return;

    matrix4x4 view = camera.viewMatrix;
    view.m[12] = 0.0f;
    view.m[13] = 0.0f;
    view.m[14] = 0.0f;
    matrix4x4 viewProjection = RenderMath::Mul(camera.projectionMatrix, view);

    backend->SetDepthTest(false);
    backend->SetDepthWrite(false);
    backend->BindShaderProgram(shader.shaderProgram);
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
