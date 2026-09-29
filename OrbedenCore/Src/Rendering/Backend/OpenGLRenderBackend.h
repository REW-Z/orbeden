#pragma once

#include "Rendering/Backend/OpenGLContext.h"
#include "Rendering/Backend/RenderBackend.h"

#include <string>
#include <unordered_map>

//OpenGL 渲染后端
class OpenGLRenderBackend : public RenderBackend
{
private:
    OpenGLContext context;
    struct DepthDistributionState
    {
        void* fence = nullptr;
        float32 nearPlane = 0.1f;
        float32 farPlane = 1000.0f;
    };
    GpuShaderProgramID depthDistributionShader;
    std::unordered_map<uint32, DepthDistributionState> depthDistributions;
    GpuShaderProgramID debugLineShader;
    uint32 debugLineVertexArray = 0;
    uint32 debugLineVertexBuffer = 0;
    GpuShaderProgramID currentShaderProgram;
    GpuVertexInputID currentVertexInput;
    uint32 currentTextureSlot = 0;
    bool depthTestEnabled = false;
    bool depthWriteEnabled = false;
    bool blendEnabled = false;
    DepthCompare depthCompare = DepthCompare::Less;
    bool polygonOffsetEnabled = false;
    CullMode cullMode = CullMode::None;
    //实例化能力，初始化时按 GL_MAX_VERTEX_ATTRIBS 判定一次
    bool instancingSupported = false;
    //片元纹理槽上限，初始化时按 GL_MAX_TEXTURE_IMAGE_UNITS 查询一次
    uint32 fragmentTextureUnitCount = 0;
    //当前混合方程，BeginFrame 重设为 Alpha 基线
    BlendMode blendMode = BlendMode::Alpha;
    //当前顶点输入绑定的实例缓冲及其字节偏移，用于绘制前校验范围
    GpuVertexBufferID boundInstanceBuffer;
    usize boundInstanceOffset = 0;
    std::unordered_map<uint32, uint32> renderTargetColorAttachments;
    std::unordered_map<uint32, GpuRenderTargetFormat> renderTargetFormats;
    std::unordered_map<uint32, uint32> indexBufferCounts;
    //顶点/索引缓冲的容量字节数，流式上传据此拒绝越界请求
    std::unordered_map<uint32, usize> vertexBufferCapacities;
    std::unordered_map<uint32, usize> indexBufferCapacities;
    std::unordered_map<uint32, uint32> vertexInputIndexBuffers;
    std::unordered_map<uint32, uint32> vertexInputIndexCounts;
    std::unordered_map<uint32, GpuVertexLayout> vertexInputLayouts;
    std::unordered_map<uint32, uint32> boundTexture2Ds;
    std::unordered_map<uint32, uint32> boundCubeTextures;
    std::unordered_map<uint32, std::unordered_map<std::string, int32>> uniformLocations;

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
    //创建异步深度统计资源
    GpuDepthDistributionID CreateDepthDistribution() override;
    //释放异步深度统计资源
    void DeleteDepthDistribution(GpuDepthDistributionID id) override;
    //提交冻结相机深度的对数直方图
    bool SubmitDepthDistribution(GpuDepthDistributionID id, GpuDepthTextureID depth,
        const matrix4x4& inverseProjection, float32 nearPlane, float32 farPlane) override;
    //读取已完成直方图，不等待 GPU
    bool TryReadDepthDistribution(GpuDepthDistributionID id, GpuDepthDistribution& distribution) override;
    GpuCubeTextureID CreateCubeTexture(const GpuCubeTextureDesc& desc) override;
    void DeleteCubeTexture(GpuCubeTextureID id) override;
    GpuRenderTargetID CreateRenderTarget(const GpuRenderTargetDesc& desc) override;
    void DeleteRenderTarget(GpuRenderTargetID id) override;
    GpuTextureID GetRenderTargetColorTexture(GpuRenderTargetID id) const override;
    bool CopyRenderTarget(const GpuRenderTargetCopyDesc& desc) override;
    GpuShaderProgramID CreateShaderProgram(const GpuShaderProgramDesc& desc) override;
    void DeleteShaderProgram(GpuShaderProgramID id) override;

    void BindShaderProgram(GpuShaderProgramID id) override;
    void BindVertexInput(GpuVertexInputID id) override;
    void BindTexture(uint32 slot, GpuTextureID id) override;
    void BindDepthTexture(uint32 slot, GpuDepthTextureID id) override;
    void BindCubeTexture(uint32 slot, GpuCubeTextureID id) override;
    void SetUniformMatrix4(const char* name, const matrix4x4& value) override;
    void SetUniformVector3(const char* name, const vector3& value) override;
    void SetUniformColor(const char* name, const color& value) override;
    void SetUniformInt(const char* name, int32 value) override;
    void SetUniformFloat(const char* name, float32 value) override;
    void SetDepthTest(bool enabled) override;
    void SetDepthCompare(DepthCompare compare) override;
    void SetDepthWrite(bool enabled) override;
    void SetPolygonOffset(bool enabled, float32 factor, float32 units) override;
    void SetBlend(bool enabled) override;
    void SetBlendMode(BlendMode mode) override;
    void SetCullMode(CullMode mode) override;
    void DrawIndexed(uint32 indexStart, uint32 indexCount) override;
    void DrawIndexedInstanced(uint32 indexStart, uint32 indexCount, uint32 instanceCount) override;

    bool SupportsInstancing() const override;
    uint32 GetFragmentTextureUnitCount() const override;
    bool UploadVertexBuffer(GpuVertexBufferID id, const void* data, usize size, usize capacity) override;
    bool UploadIndexBuffer(GpuIndexBufferID id, const uint32* data, uint32 count, uint32 capacity) override;
    bool BindInstanceBuffer(GpuVertexBufferID id, usize byteOffset) override;

    /// <summary>在当前相机目标绘制世界空间调试线。</summary>
    void DrawLines(const List<DebugLine>& lines, const matrix4x4& viewProjection, uint32 layerMask);

private:
    void ActivateTextureSlot(uint32 slot);
    void BindTexture2D(uint32 slot, uint32 texture);
    void InvalidateTexture2D(uint32 texture);
    int32 GetUniformLocation(const char* name);

    //按容量分配或覆盖顶点缓冲存储
    bool WriteVertexBuffer(uint32 buffer, const void* data, usize size, usize capacity);
    //按容量分配或覆盖索引缓冲存储，并刷新引用它的顶点输入缓存
    bool WriteIndexBuffer(uint32 buffer, const uint32* data, uint32 count, uint32 capacity);
    //实例布局的每条实例记录字节数，非实例布局返回 0
    static uint32 GetInstanceRecordSize(GpuVertexLayout layout);
    //在实例化绘制前校验索引范围、实例缓冲范围和 GL 上界，失败时写入日志
    bool ValidateInstancedRange(uint32 indexStart, uint32 indexCount, uint32 instanceCount);
};
