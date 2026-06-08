#pragma once

#include "Rendering/RenderTypes.h"
#include "Platform/Window.h"

enum class BufferKind : uint32
{
    Vertex = 0,
    Index = 1,
};

struct GpuBufferHandle
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

struct GpuVertexArrayHandle
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

struct GpuTextureHandle
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

struct GpuProgramHandle
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

struct BufferInfo
{
public:
    BufferKind kind = BufferKind::Vertex;
    const void* data = nullptr;
    usize size = 0;
};

struct VertexLayoutInfo
{
public:
    GpuBufferHandle vertexBuffer;
    GpuBufferHandle indexBuffer;
    uint32 stride = 0;
};

struct TextureInfo
{
public:
    int32 width = 0;
    int32 height = 0;
    int32 channels = 0;
    const uint8* pixels = nullptr;
};

struct ProgramInfo
{
public:
    const char* vertexSource = nullptr;
    const char* fragmentSource = nullptr;
};

struct RenderPassInfo
{
public:
    int32 width = 0;
    int32 height = 0;
    ClearMode clearMode = ClearMode::SolidColor;
    color4 clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
};

//渲染后端抽象
class RenderBackend
{
public:
    virtual ~RenderBackend() = default;

    virtual bool Initialize(IWindow* window) = 0;
    virtual void Shutdown() = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void BeginPass(const RenderPassInfo& info) = 0;
    virtual void EndPass() = 0;

    virtual GpuBufferHandle CreateBuffer(const BufferInfo& info) = 0;
    virtual void DeleteBuffer(GpuBufferHandle handle) = 0;
    virtual GpuVertexArrayHandle CreateVertexArray(const VertexLayoutInfo& info) = 0;
    virtual void DeleteVertexArray(GpuVertexArrayHandle handle) = 0;
    virtual GpuTextureHandle CreateTexture(const TextureInfo& info) = 0;
    virtual void DeleteTexture(GpuTextureHandle handle) = 0;
    virtual GpuProgramHandle CreateProgram(const ProgramInfo& info) = 0;
    virtual void DeleteProgram(GpuProgramHandle handle) = 0;

    virtual void BindProgram(GpuProgramHandle handle) = 0;
    virtual void BindVertexArray(GpuVertexArrayHandle handle) = 0;
    virtual void BindTexture(uint32 slot, GpuTextureHandle handle) = 0;
    virtual void SetUniformMatrix4(const char* name, const matrix4x4& value) = 0;
    virtual void SetUniformVector3(const char* name, const vector3& value) = 0;
    virtual void SetUniformColor4(const char* name, const color4& value) = 0;
    virtual void SetUniformInt(const char* name, int32 value) = 0;
    virtual void DrawIndexed(uint32 indexStart, uint32 indexCount) = 0;
};

