#pragma once

#include "Rendering/Backend/RenderBackend.h"

#include <span>

//每实例网格数据：世界矩阵、法线矩阵、tint 与图集 uv 变换。
//布局是后端 ABI，只允许内部使用，不暴露成生成 Span 的元素类型。
struct GpuMeshInstance
{
public:
    //列主序世界矩阵，attribute locations 5..8，每列一个 vec4。
    float32 model[16] = {};
    //法线矩阵的三列，每列补齐成 vec4，locations 9..11。
    float32 normalColumns[12] = {};
    //线性 tint，Alpha 是覆盖率，location 12。
    float32 tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    //offsetU/offsetV/scaleU/scaleV，location 13。
    float32 uvRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
};

//展开后的世界空间顶点：位置、法线、已完成图集变换的 uv、切线、tint。
//顶点已经是世界空间，着色器不再乘模型矩阵。
struct GpuExpandedVertex
{
public:
    float32 position[3] = {};
    float32 normal[3] = {};
    float32 uv[2] = {};
    float32 tangent[3] = {};
    float32 tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
};

//每段拖尾的实例数据：四个角点、两端颜色与沿长度的 uv 区间。
struct GpuTrailInstance
{
public:
    //角点顺序固定为 startLeft、startRight、endRight、endLeft，locations 5..8。
    float32 corners[16] = {};
    float32 startColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float32 endColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    //(u0, u1, 0, 1)，location 11。
    float32 uvRange[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
};

//GPU 布局是后端 ABI，偏移和大小必须与顶点属性的硬编码偏移一致。
static_assert(sizeof(GpuMeshInstance) == 144);
static_assert(offsetof(GpuMeshInstance, model) == 0);
static_assert(offsetof(GpuMeshInstance, normalColumns) == 64);
static_assert(offsetof(GpuMeshInstance, tint) == 112);
static_assert(offsetof(GpuMeshInstance, uvRect) == 128);

static_assert(sizeof(GpuExpandedVertex) == 60);
static_assert(offsetof(GpuExpandedVertex, position) == 0);
static_assert(offsetof(GpuExpandedVertex, normal) == 12);
static_assert(offsetof(GpuExpandedVertex, uv) == 24);
static_assert(offsetof(GpuExpandedVertex, tangent) == 32);
static_assert(offsetof(GpuExpandedVertex, tint) == 44);

static_assert(sizeof(GpuTrailInstance) == 112);
static_assert(offsetof(GpuTrailInstance, corners) == 0);
static_assert(offsetof(GpuTrailInstance, startColor) == 64);
static_assert(offsetof(GpuTrailInstance, endColor) == 80);
static_assert(offsetof(GpuTrailInstance, uvRange) == 96);

//按世界矩阵与每实例绘制参数填充一条实例记录。
//矩阵非有限或奇异时返回 false，该实例不提交。
bool BuildGpuMeshInstance(const matrix4x4& model, const color& linearTint, const color& uvRect, GpuMeshInstance& instance);

//流式绘制缓冲：一份实例 VBO、一份展开顶点 VBO 与其索引 IBO、一份展开顶点输入。
//每次上传都写偏移 0，上传完立即绘制该批，驱动负责在途旧存储的寿命。
class GpuDrawStream
{
private:
    //缓冲容量增长的下限，从 64 KiB 起按 2 倍增长
    static constexpr usize MinimumStreamCapacity = 64u * 1024u;
    //初始索引缓冲的元素数量，必须非零，否则顶点输入会被零索引数检查拒绝
    static constexpr uint32 MinimumIndexCapacity = 3u;
    //单批实例数量上限
    static constexpr usize MaximumBatchInstances = 65536u;
    //单批展开顶点与索引数量上限
    static constexpr usize MaximumExpandedVertices = 262144u;
    static constexpr usize MaximumExpandedIndices = 786432u;

    RenderBackend* backend = nullptr;
    GpuVertexBufferID instanceBuffer;
    GpuVertexBufferID expandedVertexBuffer;
    GpuIndexBufferID expandedIndexBuffer;
    GpuVertexInputID expandedVertexInput;
    usize instanceCapacity = 0;
    usize expandedVertexCapacity = 0;
    usize expandedIndexCapacity = 0;
    bool initialized = false;

public:
    //创建三份流式缓冲和展开顶点输入，复用时先释放旧资源
    bool Initialize(RenderBackend* renderBackend);

    //释放全部流式缓冲和顶点输入
    void Shutdown();

    //上传一批网格或拖尾实例，写偏移 0
    bool UploadMeshInstances(std::span<const GpuMeshInstance> instances);
    bool UploadTrailInstances(std::span<const GpuTrailInstance> instances);

    //上传一批展开顶点与其索引，索引从 0 起算
    bool UploadExpanded(std::span<const GpuExpandedVertex> vertices, std::span<const uint32> indices);

    //实例缓冲句柄，供实例绘制前绑定
    GpuVertexBufferID GetInstanceBuffer() const { return instanceBuffer; }

    //展开顶点的顶点输入句柄
    GpuVertexInputID GetExpandedVertexInput() const { return expandedVertexInput; }

    //流式缓冲是否可用
    bool IsValid() const { return initialized; }
};

#include "Rendering/InstanceDrawData.h"
#include "Rendering/RenderTypes.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"

class GpuResourceManager;
class ParticleRenderer;
class RenderScene;
struct ParticleFrameSnapshot;
struct GpuShader;
struct RenderBatchStats;
struct RenderCamera;
struct VisibleSet;

//绘制项来源。数值参与稳定顺序键，序号固定。
enum class DrawSource : uint32
{
    StaticMesh = 0,
    ExplicitInstance = 1,
    Particle = 2,
    Trail = 3,
};

//绘制几何种类。四边形来源的 mesh 为空，必须靠这里区分，不能把空指针交给资源解析。
enum class DrawGeometry : uint32
{
    Mesh = 0,
    BillboardQuad = 1,
    TrailQuad = 2,
};

//统一绘制项：静态网格、显式实例、粒子与拖尾共用一种记录，
//保证三种来源进入同一个剔除、排序与批次序列。
struct DrawItem
{
public:
    DrawSource source = DrawSource::StaticMesh;
    DrawGeometry geometry = DrawGeometry::Mesh;
    EnsId owner;
    //创建来源的运行时对象 ID，用于稳定顺序与诊断
    int32 sourceObjectId = 0;
    //来源内的稳定元素编号：静态为 0，显式为提交序号与实例序号，粒子为 birthId
    uint64 elementId = 0;
    uint32 subMeshIndex = 0;
    uint32 indexStart = 0;
    uint32 indexCount = 0;
    uint32 drawLayer = 1u;
    Mesh* mesh = nullptr;
    Material* material = nullptr;
    DrawQueue queue = DrawQueue::Opaque;
    GeometryMode mode = GeometryMode::Uniform;
    BlendMode blendMode = BlendMode::Alpha;
    matrix4x4 model;
    bounds3 worldBounds;
    //相机到对象位置的平方距离，与剔除使用同一度量
    float32 cameraDistance = 0.0f;
    //线性空间 tint
    color linearTint = { 1.0f, 1.0f, 1.0f, 1.0f };
    //图集 uv 变换，只作为四个浮点使用，禁止颜色空间转换
    color uvRect = { 0.0f, 0.0f, 1.0f, 1.0f };
    bool castShadows = true;
    bool receiveShadows = true;
    //来源是否允许参与自动合批；关闭时该项永远是普通绘制与排序屏障
    bool instancingEnabled = true;
    //只有最终状态为深度测试开、深度写开、混合关的单 Pass Standard 项才允许重排
    bool reorderable = false;
    //指向本帧粒子或拖尾几何记录的下标，不能跨 Render 保存
    uint32 sourceIndex = 0;
};

//批次键：键相同的连续项才可以合并成一个批次
struct DrawBatchKey
{
public:
    DrawQueue queue = DrawQueue::Opaque;
    GeometryMode mode = GeometryMode::Uniform;
    DrawGeometry geometry = DrawGeometry::Mesh;
    BlendMode blendMode = BlendMode::Alpha;
    int32 meshObjectId = 0;
    uint32 subMeshIndex = 0;
    uint32 indexStart = 0;
    uint32 indexCount = 0;
    int32 materialObjectId = 0;
    uint32 programId = 0;
    //解析后的固定功能状态，取值见 ShaderPassToggle 与 CullMode
    uint32 depthTest = 0;
    uint32 depthWrite = 0;
    uint32 blend = 0;
    uint32 cull = 0;
    bool receiveShadows = true;

    //必须逐字段比较，不能用 ObjectId 的大小关系代替
    bool operator==(const DrawBatchKey& other) const;
    bool operator!=(const DrawBatchKey& other) const { return !(*this == other); }
};

//一个可执行批次：同一键的连续绘制项，外加它需要的实例与展开几何数量
struct DrawBatch
{
public:
    DrawBatchKey key;
    //连续输入索引列表，按排序后的绘制项下标给出
    List<uint32> items;
    uint32 instanceCount = 0;
    uint32 expandedVertexCount = 0;
    uint32 expandedIndexCount = 0;
};

//把可绘制项排成批次。临时容器按相机重用，不持有跨帧指针。
class DrawBatchBuilder
{
private:
    //GPU 资源解析由外部注入，批次键需要在构建期就拿到 program ID
    GpuResourceManager* gpuResources = nullptr;
    //后端实例能力，初始化时确定；不支持时自动来源全部退回普通绘制
    bool instancingAvailable = false;
    //粒子几何由渲染器产生，builder 只负责按相机追加
    ParticleRenderer* particleRenderer = nullptr;

public:
    //注入本帧使用的 GPU 资源管理器、后端实例能力与粒子渲染器
    void Initialize(GpuResourceManager* resources, bool instancing, ParticleRenderer* particles);

    //把当前相机的可见静态项、显式实例与粒子展开为统一绘制项，并完成绘制状态解析
    void BuildCameraItems(const RenderScene& scene, const VisibleSet& visibleSet,
        const List<InstanceSubmission>& submissions, const ParticleFrameSnapshot& particles, List<DrawItem>& items);

    //按队列、距离与稳定键排序；不透明只在连续可重排区间内按批次键分组
    void SortItems(List<DrawItem>& items);

    //按排序结果顺序扫描生成批次，同时决定每个批次使用哪种几何模式
    void BuildBatches(const List<DrawItem>& items, List<DrawBatch>& batches, RenderBatchStats& stats);

    //从完整场景、全部显式实例收集阴影候选，不使用主相机的可见集合
    void BuildShadowItems(const RenderScene& scene, const List<InstanceSubmission>& submissions,
        const RenderCamera& camera, const frustum& lightFrustum, List<DrawItem>& items);

    //按阴影键分组生成批次，键不含材质，深度 program 由外部给定
    void BuildShadowBatches(const List<DrawItem>& items, const GpuShader& depthShader,
        List<DrawBatch>& batches, RenderBatchStats& stats);

    //清空本相机的临时容器，保留容量
    void Clear();

private:
    //生成批次。depthShader 非空时按阴影语义构造键并统一使用深度 program。
    void EmitBatches(const List<DrawItem>& items, const GpuShader* depthShader,
        List<DrawBatch>& batches, RenderBatchStats& stats);
};
