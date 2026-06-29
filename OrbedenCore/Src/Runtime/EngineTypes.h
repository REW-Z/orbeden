#pragma once

#include "Defines/types.h"

//2维向量
struct vector2
{
public:
    float32 x = 0.0f;
    float32 y = 0.0f;
};

//3维向量
struct vector3
{
public:
    float32 x = 0.0f;
    float32 y = 0.0f;
    float32 z = 0.0f;
};

//颜色
struct color
{
public:
    float32 r = 0.0f;
    float32 g = 0.0f;
    float32 b = 0.0f;
    float32 a = 1.0f;
};

//四元数
struct quaternion
{
public:
    float32 x = 0.0f;
    float32 y = 0.0f;
    float32 z = 0.0f;
    float32 w = 1.0f;
};
