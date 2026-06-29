#pragma once

#include "Platform/Window.h"
#include "Rendering/RenderTypes.h"

//GPU 顶点缓冲对象，在 OpenGL 后端对应 VBO。
struct GpuVertexBufferID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 索引缓冲对象，在 OpenGL 后端对应 EBO/IBO。
struct GpuIndexBufferID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 顶点输入对象，在 OpenGL 后端对应 VAO。
struct GpuVertexInputID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 纹理对象，在 OpenGL 后端对应 texture object。
struct GpuTextureID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 深度纹理对象，在 OpenGL 后端对应 depth texture。
struct GpuDepthTextureID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 立方体纹理对象，在 OpenGL 后端对应 cube map texture。
struct GpuCubeTextureID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 渲染目标对象，在 OpenGL 后端对应 framebuffer object。
struct GpuRenderTargetID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU shader 程序对象，在 OpenGL 后端对应 program object。
struct GpuShaderProgramID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 缓冲创建描述，用于顶点缓冲和索引缓冲。
struct GpuBufferDesc
{
public:
    const void* data = nullptr;
    usize size = 0;
};

//GPU 顶点输入创建描述，描述顶点/索引缓冲和顶点步长。
struct GpuVertexInputDesc
{
public:
    GpuVertexBufferID vertexBuffer;
    GpuIndexBufferID indexBuffer;
    uint32 stride = 0;
};

//GPU 纹理创建描述，描述纹理尺寸、通道和像素数据。
struct GpuTextureDesc
{
public:
    int32 width = 0;
    int32 height = 0;
    int32 channels = 0;
    const uint8* pixels = nullptr;
};

//GPU 深度纹理创建描述，描述阴影图等深度贴图尺寸。
struct GpuDepthTextureDesc
{
public:
    int32 width = 0;
    int32 height = 0;
};

//GPU 立方体纹理创建描述，按 +X/-X/+Y/-Y/+Z/-Z 提供六面像素。
struct GpuCubeTextureDesc
{
public:
    int32 width = 0;
    int32 height = 0;
    int32 channels = 0;
    const uint8* faces[6] = {};
};

//GPU 渲染目标创建描述，当前 v1 用于只有深度附件的 shadow target。
struct GpuRenderTargetDesc
{
public:
    int32 width = 0;
    int32 height = 0;
    GpuDepthTextureID depthTexture;
};

//GPU shader 程序创建描述，描述顶点和片元 shader 源码。
struct GpuShaderProgramDesc
{
public:
    const char* vertexSource = nullptr;
    const char* fragmentSource = nullptr;
};

//渲染 pass 描述，描述视口尺寸和清屏设置。
struct RenderPassDesc
{
public:
    int32 width = 0;
    int32 height = 0;
    GpuRenderTargetID renderTarget;
    ClearMode clearMode = ClearMode::SolidColor;
    color clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
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
    virtual void BeginPass(const RenderPassDesc& desc) = 0;
    virtual void EndPass() = 0;

    virtual GpuVertexBufferID CreateVertexBuffer(const GpuBufferDesc& desc) = 0;
    virtual void DeleteVertexBuffer(GpuVertexBufferID id) = 0;
    virtual GpuIndexBufferID CreateIndexBuffer(const GpuBufferDesc& desc) = 0;
    virtual void DeleteIndexBuffer(GpuIndexBufferID id) = 0;
    virtual GpuVertexInputID CreateVertexInput(const GpuVertexInputDesc& desc) = 0;
    virtual void DeleteVertexInput(GpuVertexInputID id) = 0;
    virtual GpuTextureID CreateTexture(const GpuTextureDesc& desc) = 0;
    virtual void DeleteTexture(GpuTextureID id) = 0;
    virtual GpuDepthTextureID CreateDepthTexture(const GpuDepthTextureDesc& desc) = 0;
    virtual void DeleteDepthTexture(GpuDepthTextureID id) = 0;
    virtual GpuCubeTextureID CreateCubeTexture(const GpuCubeTextureDesc& desc) = 0;
    virtual void DeleteCubeTexture(GpuCubeTextureID id) = 0;
    virtual GpuRenderTargetID CreateRenderTarget(const GpuRenderTargetDesc& desc) = 0;
    virtual void DeleteRenderTarget(GpuRenderTargetID id) = 0;
    virtual GpuShaderProgramID CreateShaderProgram(const GpuShaderProgramDesc& desc) = 0;
    virtual void DeleteShaderProgram(GpuShaderProgramID id) = 0;

    virtual void BindShaderProgram(GpuShaderProgramID id) = 0;
    virtual void BindVertexInput(GpuVertexInputID id) = 0;
    virtual void BindTexture(uint32 slot, GpuTextureID id) = 0;
    virtual void BindDepthTexture(uint32 slot, GpuDepthTextureID id) = 0;
    virtual void BindCubeTexture(uint32 slot, GpuCubeTextureID id) = 0;
    virtual void SetUniformMatrix4(const char* name, const matrix4x4& value) = 0;
    virtual void SetUniformVector3(const char* name, const vector3& value) = 0;
    virtual void SetUniformColor(const char* name, const color& value) = 0;
    virtual void SetUniformInt(const char* name, int32 value) = 0;
    virtual void SetUniformFloat(const char* name, float32 value) = 0;
    virtual void SetDepthTest(bool enabled) = 0;
    virtual void SetDepthWrite(bool enabled) = 0;
    virtual void DrawIndexed(uint32 indexStart, uint32 indexCount) = 0;
};
