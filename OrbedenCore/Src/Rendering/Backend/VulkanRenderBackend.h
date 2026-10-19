#pragma once

#include "Rendering/Backend/RenderBackend.h"

//Vulkan 1.3 渲染后端
class VulkanRenderBackend : public RenderBackend
{
private:
    struct State;
    std::unique_ptr<State> state;

    //选择着色器对象
    void OnBindShaderProgram(GpuShaderProgramID id) override;
    //写入当前渲染阶段的 GPU 时间检查点
    void RecordGpuCheckpoint(const char* name) override;

public:
    //创建后端状态
    VulkanRenderBackend();
    //释放后端状态
    ~VulkanRenderBackend() override;
    bool Initialize(IWindow* window) override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame() override;
    void Present() override;
    void WaitForIdle() override;
    GpuSurfaceID CreateSurface(IWindow* window) override;
    void DeleteSurface(GpuSurfaceID id) override;
    void SetSurface(GpuSurfaceID id) override;
    void BeginPass(const RenderPassDesc& desc) override;
    void EndPass() override;
    void SetScissor(bool enabled, int32 x, int32 y, int32 width, int32 height) override;
    GpuVertexBufferID CreateVertexBuffer(const GpuBufferDesc& desc) override;
    void DeleteVertexBuffer(GpuVertexBufferID id) override;
    GpuIndexBufferID CreateIndexBuffer(const GpuBufferDesc& desc) override;
    void DeleteIndexBuffer(GpuIndexBufferID id) override;
    GpuVertexInputID CreateVertexInput(const GpuVertexInputDesc& desc) override;
    void DeleteVertexInput(GpuVertexInputID id) override;
    GpuTextureID CreateTexture(const GpuTextureDesc& desc) override;
    void DeleteTexture(GpuTextureID id) override;
    bool UploadTextureRegion(GpuTextureID id, int32 x, int32 y, int32 width, int32 height,
        int32 channels, const uint8* pixels, int32 rowStride) override;
    GpuDepthTextureID CreateDepthTexture(const GpuDepthTextureDesc& desc) override;
    void DeleteDepthTexture(GpuDepthTextureID id) override;
    GpuDepthDistributionID CreateDepthDistribution() override;
    void DeleteDepthDistribution(GpuDepthDistributionID id) override;
    bool SubmitDepthDistribution(GpuDepthDistributionID id, GpuDepthTextureID depth,
        const matrix4x4& inverseProjection, float32 nearPlane, float32 farPlane) override;
    bool TryReadDepthDistribution(GpuDepthDistributionID id, GpuDepthDistribution& distribution) override;
    GpuCubeTextureID CreateCubeTexture(const GpuCubeTextureDesc& desc) override;
    void DeleteCubeTexture(GpuCubeTextureID id) override;
    GpuRenderTargetID CreateRenderTarget(const GpuRenderTargetDesc& desc) override;
    void AttachRenderTargetDepthTexture(GpuRenderTargetID target, GpuDepthTextureID depthTexture) override;
    void DeleteRenderTarget(GpuRenderTargetID id) override;
    GpuTextureID GetRenderTargetColorTexture(GpuRenderTargetID id) const override;
    bool CopyRenderTarget(const GpuRenderTargetCopyDesc& desc) override;
    GpuShaderProgramID CreateShaderProgram(const GpuShaderProgramDesc& desc) override;
    void DeleteShaderProgram(GpuShaderProgramID id) override;
    bool HasShaderParameter(GpuShaderProgramID program, const char* name) override;
    void BindVertexInput(GpuVertexInputID id) override;
    void BindTexture(uint32 slot, GpuTextureID id) override;
    void BindDepthTexture(uint32 slot, GpuDepthTextureID id) override;
    void BindCubeTexture(uint32 slot, GpuCubeTextureID id) override;
    void DrawIndexed(uint32 indexStart, uint32 indexCount) override;
    void DrawIndexedInstanced(uint32 indexStart, uint32 indexCount, uint32 instanceCount) override;
    bool SupportsInstancing() const override;
    uint32 GetFragmentTextureUnitCount() const override;
    bool UploadVertexBuffer(GpuVertexBufferID id, const void* data, usize size, usize capacity) override;
    bool UploadIndexBuffer(GpuIndexBufferID id, const uint32* data, uint32 count, uint32 capacity) override;
    bool ReadDepthPixel(GpuRenderTargetID target, int32 x, int32 y, float32& depth) override;
    bool BindInstanceBuffer(GpuVertexBufferID id, usize byteOffset) override;
};
