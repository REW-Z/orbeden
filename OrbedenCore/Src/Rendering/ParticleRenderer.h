#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/DrawBatchBuilder.h"
#include "Rendering/RenderScene.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Particles/ParticleSettings.h"

class GpuResourceManager;
class ParticleSimulationContext;
class World;

//一颗粒子的渲染记录。所有数据只在本次 Render 内有效。
struct ParticleRenderRecord
{
public:
    int32 sourceObjectId = 0;
    EnsId owner;
    uint64 birthId = 0;
    vector3 worldPosition;
    quaternion worldRotation;
    float32 size = 1.0f;
    float32 angle = 0.0f;
    //线性空间颜色，已乘过 startColor 与渐变
    color linearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    //图集 uv 变换，只作为四个浮点使用
    color uvRect = { 0.0f, 0.0f, 1.0f, 1.0f };
    //Mesh 模式的完整世界矩阵；Billboard 由相机在追加阶段构造
    matrix4x4 worldModel;
    bounds3 worldBounds;
    //资源引用快照，使用前重新解析
    Ref<Mesh> mesh;
    List<Ref<Material>> materials;
    uint32 drawLayer = 1u;
    bool castShadows = false;
    bool receiveShadows = false;
    ParticleRenderPath renderPath = ParticleRenderPath::Instanced;
    ParticleRenderMode renderMode = ParticleRenderMode::Billboard;
    BlendMode blendMode = BlendMode::Alpha;
};

//拖尾上的一个采样点，宽度与颜色已经求值
struct ParticleTrailPointSnapshot
{
public:
    vector3 position;
    float32 width = 1.0f;
    color linearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    //沿总弧长的归一化位置
    float32 length = 0.0f;
};

//一条拖尾的渲染记录
struct ParticleTrailRenderRecord
{
public:
    int32 sourceObjectId = 0;
    EnsId owner;
    uint64 trailId = 0;
    List<ParticleTrailPointSnapshot> points;
    Ref<Material> material;
    ParticleRenderPath renderPath = ParticleRenderPath::Instanced;
    BlendMode blendMode = BlendMode::Alpha;
    uint32 drawLayer = 1u;
    float32 textureTileLength = 1.0f;
};

//一次 Render 的粒子与拖尾几何快照
struct ParticleFrameSnapshot
{
public:
    World* world = nullptr;
    uint64 revision = 0;
    bool preview = false;
    List<ParticleRenderRecord> particles;
    List<ParticleTrailRenderRecord> trails;

    //是否没有任何粒子与拖尾
    bool IsEmpty() const { return particles.empty() && trails.empty(); }
};

//一段拖尾的相机相关几何。角点顺序固定为 startLeft、startRight、endRight、endLeft。
struct ParticleTrailSegment
{
public:
    vector3 corners[4];
    color startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    color endColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    //沿纹理的横向 uv 区间
    float32 u0 = 0.0f;
    float32 u1 = 1.0f;
};

//一批展开几何。一个源三角形必须完整落在同一个 chunk 里。
struct ExpandedGeometryChunk
{
public:
    List<GpuExpandedVertex> vertices;
    List<uint32> indices;
};

//粒子几何的来源：把 CPU 粒子快照翻译成绘制项，并生成两种路径需要的顶点数据。
class ParticleRenderer
{
private:
    //创建与释放 GPU 资源使用的后端
    RenderBackend* backend = nullptr;

    //共享 Billboard 四边形的顶点与索引缓冲，三种布局各一份顶点输入
    GpuVertexBufferID quadVertexBuffer;
    GpuIndexBufferID quadIndexBuffer;
    GpuVertexInputID quadUniformInput;
    GpuVertexInputID quadInstancedInput;
    GpuVertexInputID quadTrailInput;
    bool quadReady = false;

    //本相机的拖尾段几何，按 DrawItem::sourceIndex 索引
    List<ParticleTrailSegment> trailSegments;

public:
    /// <summary>保存后端引用；四边形延迟创建。</summary>
    bool Initialize(RenderBackend* backend);

    /// <summary>按 §13.2 的固定四顶点六索引上传共享四边形并建立三份顶点输入。</summary>
    bool PrepareQuad();

    /// <summary>把模拟状态翻译成只读渲染快照，不写入任何模拟状态。</summary>
    void CaptureFrame(World& world, const ParticleSimulationContext& context, const TransformCache& transformCache,
        ParticleFrameSnapshot& snapshot);

    /// <summary>按当前相机追加 Billboard、Mesh 与拖尾的绘制项。</summary>
    void AppendCameraItems(const ParticleFrameSnapshot& snapshot, const RenderCamera& camera, List<DrawItem>& items);

    /// <summary>追加级联阴影的粒子候选：只收开启投影的 Opaque Mesh 粒子，Billboard 与拖尾不投影。</summary>
    void AppendShadowItems(const ParticleFrameSnapshot& snapshot, const RenderCamera& camera,
        const frustum& lightFrustum, List<DrawItem>& items);

    /// <summary>从有序绘制项构造实例记录，顺序与透明序列一致；返回因变换非法被拒绝的项数。</summary>
    uint32 BuildMeshInstances(const DrawBatch& batch, const List<DrawItem>& items, List<GpuMeshInstance>& instances);

    /// <summary>按该相机的拖尾角点快照写 112 字节实例记录。</summary>
    void BuildTrailInstances(const DrawBatch& batch, const List<DrawItem>& items, List<GpuTrailInstance>& instances);

    /// <summary>把一批绘制项展开成世界空间顶点与索引，按上限切块。</summary>
    void ExpandBatch(const DrawBatch& batch, const List<DrawItem>& items, List<ExpandedGeometryChunk>& chunks,
        GpuResourceManager& resources);

    /// <summary>取共享四边形在指定几何模式下的顶点输入。</summary>
    GpuVertexInputID GetQuadVertexInput(GeometryMode mode) const;

    /// <summary>释放四边形的顶点输入与缓冲，不动模拟状态。</summary>
    void InvalidateResourceCaches();

    /// <summary>释放全部 GPU 资源并清空后端引用。</summary>
    void Shutdown();

    /// <summary>清空本相机的临时容器，保留容量。</summary>
    void ClearCameraScratch() { trailSegments.clear(); }
};
