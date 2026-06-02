#pragma once

#include "Defines/types.h"

//轻量向量
struct vector2
{
public:
    float32 x = 0.0f;
    float32 y = 0.0f;
};

//轻量向量
struct vector3
{
public:
    float32 x = 0.0f;
    float32 y = 0.0f;
    float32 z = 0.0f;
};

//轻量四元数
struct quaternion
{
public:
    float32 x = 0.0f;
    float32 y = 0.0f;
    float32 z = 0.0f;
    float32 w = 1.0f;
};
