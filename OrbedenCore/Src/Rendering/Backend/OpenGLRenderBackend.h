#pragma once

#include "Rendering/Backend/OpenGLContext.h"
#include "Rendering/Backend/RenderBackend.h"

#include <unordered_map>

//OpenGL 渲染后端
class OpenGLRenderBackend : public RenderBackend
{
private:
    OpenGLContext context;
    GpuShaderProgramID currentShaderProgram;
    GpuVertexInputID currentVertexInput;
    std::unordered_map<uint32, uint32> renderTargetColorAttachments;
    std::unordered_map<uint32, uint32> indexBufferCounts;
    std::unordered_map<uint32, uint32> vertexInputIndexBuffers;
    std::unordered_map<uint32, uint32> vertexInputIndexCounts;

public:
    bool Initialize(IWindow* window) override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame() override;
    void BeginPass(const RenderPassDesc& desc) override;
    void EndPass() override;

    GpuVertexBufferID CreateVertexBuffer(const GpuBufferDesc& desc) override;
    void DeleteVertexBuffer(GpuVertexBufferID id) override;
    GpuIndexBufferID CreateIndexBuffer(const GpuBufferDesc& desc) override;
    void DeleteIndexBuffer(GpuIndexBufferID id) override;
    GpuVertexInputID CreateVertexInput(const GpuVertexInputDesc& desc) override;
    void DeleteVertexInput(GpuVertexInputID id) override;
    GpuTextureID CreateTexture(const GpuTextureDesc& desc) override;
    void DeleteTexture(GpuTextureID id) override;
    GpuDepthTextureID CreateDepthTexture(const GpuDepthTextureDesc& desc) override;
    void DeleteDepthTexture(GpuDepthTextureID id) override;
    GpuCubeTextureID CreateCubeTexture(const GpuCubeTextureDesc& desc) override;
    void DeleteCubeTexture(GpuCubeTextureID id) override;
    GpuRenderTargetID CreateRenderTarget(const GpuRenderTargetDesc& desc) override;
    void DeleteRenderTarget(GpuRenderTargetID id) override;
    GpuShaderProgramID CreateShaderProgram(const GpuShaderProgramDesc& desc) override;
    void DeleteShaderProgram(GpuShaderProgramID id) override;

    void BindShaderProgram(GpuShaderProgramID id) override;
    void BindVertexInput(GpuVertexInputID id) override;
    void BindTexture(uint32 slot, GpuTextureID id) override;
    void BindDepthTexture(uint32 slot, GpuDepthTextureID id) override;
    void BindCubeTexture(uint32 slot, GpuCubeTextureID id) override;
    void SetUniformMatrix4(const char* name, const matrix4x4& value) override;
    void SetUniformVector3(const char* name, const vector3& value) override;
    void SetUniformColor4(const char* name, const color4& value) override;
    void SetUniformInt(const char* name, int32 value) override;
    void SetUniformFloat(const char* name, float32 value) override;
    void SetDepthTest(bool enabled) override;
    void SetDepthWrite(bool enabled) override;
    void DrawIndexed(uint32 indexStart, uint32 indexCount) override;

private:
    int32 GetUniformLocation(const char* name) const;
};
