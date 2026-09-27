#pragma once

#include "Platform/Window.h"
#include "Rendering/Backend/GpuResourceIDs.h"
#include "Rendering/RenderTypes.h"

//渲染目标颜色格式。管线约定场景在 RGBA16F 线性空间计算，只有显示目标用 RGBA8。
enum class GpuRenderTargetFormat
{
    RGBA8 = 0,
    RGBA16F = 1,
};

//GPU 顶点布局种类。数值稳定，后端按它选择 attribute 绑定方案。
enum class GpuVertexLayout : uint32
{
    //现有网格顶点布局：位置、法线、uv、切线，步长由网格决定。
    Mesh = 0,
    //网格顶点 + 每实例属性 locations 5..13，实例 stride 为 144 字节。
    InstancedMesh = 1,
    //展开后的世界空间顶点，5 个属性，stride 为 60 字节。
    Expanded = 2,
    //共享四边形网格顶点 + 每实例属性 locations 5..11，实例 stride 为 112 字节。
    InstancedTrail = 3,
};

//GPU 缓冲用途。Stream 允许先按容量创建、之后重复流式覆盖。
enum class GpuBufferUsage : uint32
{
    Static = 0,
    Stream = 1,
};

//GPU 缓冲创建描述，用于顶点缓冲和索引缓冲。size 表示容量字节数。
struct GpuBufferDesc
{
public:
    const void* data = nullptr;
    usize size = 0;
    //Stream 用途允许 data 为空、size 非零，表示只按容量分配存储。
    GpuBufferUsage usage = GpuBufferUsage::Static;
};

//GPU 顶点输入创建描述，描述顶点/索引缓冲、顶点步长和属性布局。
struct GpuVertexInputDesc
{
public:
    GpuVertexBufferID vertexBuffer;
    GpuIndexBufferID indexBuffer;
    uint32 stride = 0;
    GpuVertexLayout layout = GpuVertexLayout::Mesh;
};

//GPU 纹理创建描述，描述纹理尺寸、通道和像素数据。
struct GpuTextureDesc
{
public:
    int32 width = 0;
    int32 height = 0;
    int32 channels = 0;
    const uint8* pixels = nullptr;
    //颜色贴图的像素是 sRGB 编码，用 sRGB 内部格式让采样自动解码；
    //数据贴图（法线、粗糙度、遮罩）保持线性，必须为 false。
    bool srgb = false;
};

//GPU 深度纹理创建描述，描述阴影图等深度贴图尺寸。
struct GpuDepthTextureDesc
{
public:
    int32 width = 0;
    int32 height = 0;
    bool floatingPoint = false;
};

//对数视深度分布，元数据对应提交统计时的相机
struct GpuDepthDistribution
{
    static constexpr int32 BinCount = 64;
    uint32 bins[BinCount] = {};
    float32 nearPlane = 0.1f;
    float32 farPlane = 1000.0f;
};

//GPU 立方体纹理创建描述，按 +X/-X/+Y/-Y/+Z/-Z 提供六面像素。
struct GpuCubeTextureDesc
{
public:
    int32 width = 0;
    int32 height = 0;
    int32 channels = 0;
    const uint8* faces[6] = {};
    bool generateMipmaps = false;
    //天空盒是颜色数据，通常为 true，采样时由硬件解码。
    bool srgb = false;
};

//GPU 渲染目标创建描述，可创建颜色+深度或纯深度目标。
struct GpuRenderTargetDesc
{
public:
    int32 width = 0;
    int32 height = 0;
    GpuDepthTextureID depthTexture;
    bool depthOnly = false;
    bool linearColorFilter = false;
    //默认 RGBA8：显示目标与掩码目标都在这个格式下工作，
    //只有场景缓冲需要显式声明 RGBA16F 承载线性 HDR。
    GpuRenderTargetFormat format = GpuRenderTargetFormat::RGBA8;
};

//GPU 渲染目标拷贝描述，将源 viewport 的颜色和深度复制到目标起点。
//源为 0 时表示默认窗口帧缓冲。
struct GpuRenderTargetCopyDesc
{
public:
    GpuRenderTargetID sourceRenderTarget;
    GpuRenderTargetID destinationRenderTarget;
    int32 sourceX = 0;
    int32 sourceY = 0;
    int32 width = 0;
    int32 height = 0;
    //只复制颜色。相机用 DepthOnly/None 清屏模式时用它把显示目标已有内容
    //搬进内部场景缓冲，此时不能覆盖深度。
    bool colorOnly = false;
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
    int32 x = 0;
    int32 y = 0;
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
    //创建异步深度统计资源
    virtual GpuDepthDistributionID CreateDepthDistribution() = 0;
    //释放异步深度统计资源
    virtual void DeleteDepthDistribution(GpuDepthDistributionID id) = 0;
    //提交冻结深度统计，在途任务未完成时不覆盖
    virtual bool SubmitDepthDistribution(GpuDepthDistributionID id, GpuDepthTextureID depth,
        const matrix4x4& inverseProjection, float32 nearPlane, float32 farPlane) = 0;
    //零超时读取已完成统计
    virtual bool TryReadDepthDistribution(GpuDepthDistributionID id, GpuDepthDistribution& distribution) = 0;
    virtual GpuCubeTextureID CreateCubeTexture(const GpuCubeTextureDesc& desc) = 0;
    virtual void DeleteCubeTexture(GpuCubeTextureID id) = 0;
    virtual GpuRenderTargetID CreateRenderTarget(const GpuRenderTargetDesc& desc) = 0;
    virtual void DeleteRenderTarget(GpuRenderTargetID id) = 0;
    virtual GpuTextureID GetRenderTargetColorTexture(GpuRenderTargetID id) const = 0;
    virtual bool CopyRenderTarget(const GpuRenderTargetCopyDesc& desc) = 0;
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
    virtual void SetDepthCompare(DepthCompare compare) = 0;
    virtual void SetDepthWrite(bool enabled) = 0;
    //多边形深度偏移：与已有表面同层绘制时用它压过浮点误差
    virtual void SetPolygonOffset(bool enabled, float32 factor, float32 units) = 0;
    virtual void SetBlend(bool enabled) = 0;
    //设置混合方程。Alpha 与 Additive 的 RGB/Alpha 因子成对切换，由后端缓存并去重。
    virtual void SetBlendMode(BlendMode mode) = 0;
    virtual void SetCullMode(CullMode mode) = 0;
    virtual void DrawIndexed(uint32 indexStart, uint32 indexCount) = 0;
    //实例化索引绘制，实例数据由 BindInstanceBuffer 提供。
    virtual void DrawIndexedInstanced(uint32 indexStart, uint32 indexCount, uint32 instanceCount) = 0;

    //实例化能力在初始化时确定，绘制期间不再查询 GL。
    virtual bool SupportsInstancing() const = 0;
    //把数据流式写入已有顶点缓冲，超出容量的请求被拒绝。
    virtual bool UploadVertexBuffer(GpuVertexBufferID id, const void* data, usize size, usize capacity) = 0;
    //把索引流式写入已有索引缓冲，同时刷新引用它的顶点输入的索引数量缓存。
    virtual bool UploadIndexBuffer(GpuIndexBufferID id, const uint32* data, uint32 count, uint32 capacity) = 0;
    //按当前顶点输入的实例布局设置实例 attribute 指针与 divisor。
    virtual bool BindInstanceBuffer(GpuVertexBufferID id, usize byteOffset) = 0;
};
