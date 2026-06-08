#include "Rendering/ForwardPipeline.h"

#include "Log/Log.h"
#include "Runtime/Camera.h"

void ForwardPipeline::Initialize(RenderBackend* renderBackend)
{
    backend = renderBackend;
}

void ForwardPipeline::Render(const VisibleSet& visibleSet, GpuResourceManager& resources)
{
    if (!backend) return;

    const RenderCamera& camera = visibleSet.camera;
    RenderPassInfo passInfo;
    passInfo.width = camera.viewportWidth;
    passInfo.height = camera.viewportHeight;
    passInfo.clearMode = camera.camera ? camera.camera->clearMode : ClearMode::SolidColor;
    passInfo.clearColor = camera.camera ? camera.camera->clearColor : color4();

    backend->BeginPass(passInfo);

    for (const RenderItem& item : visibleSet.items)
    {
        GpuMaterialHandle material = resources.GetMaterial(item.material);
        if (!material.IsValid())
        {
            Log::Error("ForwardPipeline draw skipped: material GPU handle is invalid.");
            continue;
        }

        GpuMeshHandle mesh = resources.GetMesh(item.mesh);
        if (!mesh.IsValid())
        {
            Log::Error("ForwardPipeline draw skipped: mesh GPU handle is invalid.");
            continue;
        }

        backend->BindProgram(material.shader.program);
        backend->SetUniformMatrix4("u_Model", item.localToWorld);
        backend->SetUniformMatrix4("u_ViewProjection", camera.viewProjectionMatrix);
        backend->SetUniformVector3("u_DiffuseColor", material.diffuse);
        backend->SetUniformInt("u_HasDiffuseTexture", material.hasDiffuseTexture ? 1 : 0);
        backend->SetUniformInt("u_DiffuseTexture", 0);
        if (material.hasDiffuseTexture)
        {
            backend->BindTexture(0, material.diffuseTexture);
        }

        backend->BindVertexArray(mesh.vertexArray);
        backend->DrawIndexed(item.indexStart, item.indexCount);
    }

    backend->BindVertexArray(GpuVertexArrayHandle());
    backend->BindProgram(GpuProgramHandle());
    backend->EndPass();
}

