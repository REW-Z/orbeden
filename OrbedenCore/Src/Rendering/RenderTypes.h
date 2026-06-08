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

