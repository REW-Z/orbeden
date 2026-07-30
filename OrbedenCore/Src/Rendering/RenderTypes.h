#pragma once

#include "Runtime/EngineTypes.h"
#include "Runtime/EnsId.h"

class Camera;
class Material;
class Mesh;
class StaticMeshRenderer;

//相机清屏方式
enum class ClearMode : uint32
{
    None = 0,
    DepthOnly = 1,
    SolidColor = 2,
};

//绘制队列
enum class DrawQueue : uint32
{
    Opaque = 0,
    Transparent = 1,
};

//光栅化剔除模式，Auto 由渲染管线解析为基线状态
enum class CullMode : uint32
{
    Auto = 0,
    None = 1,
    Front = 2,
    Back = 3,
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

//持久渲染场景句柄，版本用于阻止延迟删除命中新复用的槽位
struct RenderSceneHandle
{
public:
    uint32 id = EnsId::InvalidId;
    uint32 version = 0;

    //判断句柄是否有效
    bool IsValid() const { return id != EnsId::InvalidId && version != 0; }

    bool operator==(const RenderSceneHandle& other) const { return id == other.id && version == other.version; }
    bool operator!=(const RenderSceneHandle& other) const { return !(*this == other); }
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
