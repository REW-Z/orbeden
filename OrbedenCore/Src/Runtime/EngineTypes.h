#pragma once

#include <cstddef>
#include <type_traits>

#include "Defines/types.h"

#pragma pack(push, 4)

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

#pragma pack(pop)

static_assert(std::is_standard_layout_v<vector2> && std::is_trivially_copyable_v<vector2>);
static_assert(sizeof(vector2) == sizeof(float32) * 2 && alignof(vector2) <= 4);
static_assert(offsetof(vector2, x) == 0 && offsetof(vector2, y) == sizeof(float32));

static_assert(std::is_standard_layout_v<vector3> && std::is_trivially_copyable_v<vector3>);
static_assert(sizeof(vector3) == sizeof(float32) * 3 && alignof(vector3) <= 4);
static_assert(offsetof(vector3, x) == 0 && offsetof(vector3, y) == sizeof(float32) && offsetof(vector3, z) == sizeof(float32) * 2);

static_assert(std::is_standard_layout_v<color> && std::is_trivially_copyable_v<color>);
static_assert(sizeof(color) == sizeof(float32) * 4 && alignof(color) <= 4);
static_assert(offsetof(color, r) == 0 && offsetof(color, g) == sizeof(float32) && offsetof(color, b) == sizeof(float32) * 2 && offsetof(color, a) == sizeof(float32) * 3);

static_assert(std::is_standard_layout_v<quaternion> && std::is_trivially_copyable_v<quaternion>);
static_assert(sizeof(quaternion) == sizeof(float32) * 4 && alignof(quaternion) <= 4);
static_assert(offsetof(quaternion, x) == 0 && offsetof(quaternion, y) == sizeof(float32) && offsetof(quaternion, z) == sizeof(float32) * 2 && offsetof(quaternion, w) == sizeof(float32) * 3);
