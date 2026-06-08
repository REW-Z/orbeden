#pragma once

#include "Rendering/Backend/OpenGLContext.h"
#include "Rendering/Backend/RenderBackend.h"

//OpenGL 渲染后端
class OpenGLRenderBackend : public RenderBackend
{
private:
    OpenGLContext context;
    GpuProgramHandle currentProgram;

public:
    bool Initialize(IWindow* window) override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame() override;
    void BeginPass(const RenderPassInfo& info) override;
    void EndPass() override;

    GpuBufferHandle CreateBuffer(const BufferInfo& info) override;
    void DeleteBuffer(GpuBufferHandle handle) override;
    GpuVertexArrayHandle CreateVertexArray(const VertexLayoutInfo& info) override;
    void DeleteVertexArray(GpuVertexArrayHandle handle) override;
    GpuTextureHandle CreateTexture(const TextureInfo& info) override;
    void DeleteTexture(GpuTextureHandle handle) override;
    GpuProgramHandle CreateProgram(const ProgramInfo& info) override;
    void DeleteProgram(GpuProgramHandle handle) override;

    void BindProgram(GpuProgramHandle handle) override;
    void BindVertexArray(GpuVertexArrayHandle handle) override;
    void BindTexture(uint32 slot, GpuTextureHandle handle) override;
    void SetUniformMatrix4(const char* name, const matrix4x4& value) override;
    void SetUniformVector3(const char* name, const vector3& value) override;
    void SetUniformColor4(const char* name, const color4& value) override;
    void SetUniformInt(const char* name, int32 value) override;
    void DrawIndexed(uint32 indexStart, uint32 indexCount) override;

private:
    int32 GetUniformLocation(const char* name) const;
};

