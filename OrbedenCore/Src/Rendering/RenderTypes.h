#pragma once

#include "Runtime/EngineTypes.h"
#include "Runtime/EnsId.h"

class Camera;
class Material;
class Mesh;
class StaticMeshRenderer;

//当前帧的世界空间调试线。
struct DebugLine
{
    vector3 start;
    vector3 end;
    color tint;
    bool depthTest = false;
    uint32 drawLayer = 1u;
};

//相机清屏方式
enum class ClearMode : uint32
{
    None = 0,
    DepthOnly = 1,
    SolidColor = 2,
    //只清颜色，用于与其它目标共享深度缓冲的中间 pass
    ColorOnly = 3,
};

//深度比较函数：默认严格小于，需要与已有深度同层绘制时用 LessEqual
enum class DepthCompare : uint32
{
    Less = 0,
    LessEqual = 1,
};

//绘制队列
enum class DrawQueue : uint32
{
    Opaque = 0,
    Transparent = 1,
    Refraction = 2,
};

//光栅化剔除模式，Auto 由渲染管线解析为基线状态
enum class CullMode : uint32
{
    Auto = 0,
    None = 1,
    Front = 2,
    Back = 3,
};

//混合模式。数值参与批次键比较，序号固定。
enum class BlendMode : uint32
{
    Alpha = 0,
    Additive = 1,
};

//几何提交模式，决定一次绘制走哪条顶点路径。数值参与批次键比较，序号固定。
enum class GeometryMode : uint32
{
    Uniform = 0,
    Instanced = 1,
    Expanded = 2,
    TrailInstanced = 3,
};

//渲染器允许的绘制策略。数值参与批次键比较，序号固定。
enum class DrawStrategy : uint32
{
    //自动：能用静态批就用，其余按实例批、动态批、单绘制的顺序退化
    Auto = 0,
    //仅单绘制：不参与任何合批，作为排序屏障
    Individual = 1,
    //只允许实例批，条件不足时退回单绘制
    GpuInstancing = 2,
    //只允许动态批，条件不足时退回单绘制
    DynamicBatching = 3,
};

//渲染系统创建的离屏目标句柄，0 表示默认窗口帧缓冲
struct RenderTargetID
{
public:
    uint32 id = 0;

    bool IsValid() const { return id != 0; }
    bool operator==(const RenderTargetID& other) const { return id == other.id; }
    bool operator!=(const RenderTargetID& other) const { return id != other.id; }
};

//一帧的渲染批次统计。全部为累计计数，每次 Render 开始清零。
struct RenderBatchStats
{
public:
    uint64 sourceItems = 0;
    uint64 visibleItems = 0;
    uint64 ordinaryDraws = 0;
    uint64 instancedDraws = 0;
    uint64 dynamicBatchDraws = 0;
    uint64 submittedInstances = 0;
    uint64 expandedVertices = 0;
    uint64 expandedIndices = 0;
    uint64 uploadedBytes = 0;
    uint64 shadowDraws = 0;
    uint64 invalidTransforms = 0;
    uint64 invalidResources = 0;
    uint64 failedUploads = 0;
    uint64 multiPassItems = 0;
    uint64 transparentBatchBreaks = 0;
};

//轻量矩阵，按 OpenGL 习惯使用列主序
struct matrix4x4
{
public:
    float32 m[16] =
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
};

//轻量平面
struct plane3
{
public:
    vector3 normal;
    float32 distance = 0.0f;
};

//轻量包围盒
struct bounds3
{
public:
    vector3 center;
    vector3 extents;
    bool valid = false;
};

//轻量视锥
struct frustum
{
public:
    plane3 planes[6];
};
