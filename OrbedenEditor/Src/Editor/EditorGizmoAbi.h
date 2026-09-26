#pragma once

#include "Runtime/EnsId.h"
#include "Runtime/Native/NativeApiAbi.h"

#include <type_traits>

#pragma pack(push, 4)

//Editor Gizmo 三维向量，布局与 C# Orbeden.vector3 一致。
struct EditorGizmoVector3
{
public:
    float32 x = 0.0f;
    float32 y = 0.0f;
    float32 z = 0.0f;
};

//Editor Gizmo 四元数，布局与 C# Orbeden.quaternion 一致。
struct EditorGizmoQuaternion
{
public:
    float32 x = 0.0f;
    float32 y = 0.0f;
    float32 z = 0.0f;
    float32 w = 1.0f;
};

//Editor Gizmo 颜色，布局与 C# Orbeden.color4 一致。
struct EditorGizmoColor
{
public:
    float32 r = 1.0f;
    float32 g = 1.0f;
    float32 b = 1.0f;
    float32 a = 1.0f;
};

//一次手柄拖拽对单个对象的局部变换改动，托管侧据此写撤销记录。
struct EditorGizmoEdit
{
public:
    EnsId ens;
    int32 mode = 0;                        //0 位移 / 1 旋转 / 2 缩放
    EditorGizmoVector3 startPosition;      //拖拽开始时的局部平移
    EditorGizmoQuaternion startRotation;   //拖拽开始时的局部旋转
    EditorGizmoVector3 startScale = { 1.0f, 1.0f, 1.0f };
    EditorGizmoVector3 endPosition;        //拖拽结束时的局部平移
    EditorGizmoQuaternion endRotation;     //拖拽结束时的局部旋转
    EditorGizmoVector3 endScale = { 1.0f, 1.0f, 1.0f };
};

#pragma pack(pop)

static_assert(std::is_standard_layout_v<EditorGizmoVector3> && std::is_trivially_copyable_v<EditorGizmoVector3>);
static_assert(sizeof(EditorGizmoVector3) == sizeof(float32) * 3 && alignof(EditorGizmoVector3) <= 4);
static_assert(offsetof(EditorGizmoVector3, x) == 0 && offsetof(EditorGizmoVector3, y) == sizeof(float32) && offsetof(EditorGizmoVector3, z) == sizeof(float32) * 2);

static_assert(std::is_standard_layout_v<EditorGizmoQuaternion> && std::is_trivially_copyable_v<EditorGizmoQuaternion>);
static_assert(sizeof(EditorGizmoQuaternion) == sizeof(float32) * 4 && alignof(EditorGizmoQuaternion) <= 4);
static_assert(offsetof(EditorGizmoQuaternion, x) == 0 && offsetof(EditorGizmoQuaternion, y) == sizeof(float32) && offsetof(EditorGizmoQuaternion, z) == sizeof(float32) * 2 && offsetof(EditorGizmoQuaternion, w) == sizeof(float32) * 3);

static_assert(std::is_standard_layout_v<EditorGizmoColor> && std::is_trivially_copyable_v<EditorGizmoColor>);
static_assert(sizeof(EditorGizmoColor) == sizeof(float32) * 4 && alignof(EditorGizmoColor) <= 4);
static_assert(offsetof(EditorGizmoColor, r) == 0 && offsetof(EditorGizmoColor, g) == sizeof(float32) && offsetof(EditorGizmoColor, b) == sizeof(float32) * 2 && offsetof(EditorGizmoColor, a) == sizeof(float32) * 3);

static_assert(std::is_standard_layout_v<EditorGizmoEdit> && std::is_trivially_copyable_v<EditorGizmoEdit>);
static_assert(sizeof(EditorGizmoEdit) == 92 && alignof(EditorGizmoEdit) <= 4);
static_assert(offsetof(EditorGizmoEdit, ens) == 0 && offsetof(EditorGizmoEdit, mode) == 8
    && offsetof(EditorGizmoEdit, startPosition) == 12 && offsetof(EditorGizmoEdit, startRotation) == 24
    && offsetof(EditorGizmoEdit, startScale) == 40 && offsetof(EditorGizmoEdit, endPosition) == 52
    && offsetof(EditorGizmoEdit, endRotation) == 64 && offsetof(EditorGizmoEdit, endScale) == 80);

//Editor Gizmo 原生函数表，传给 C# Editor 保存。
#pragma pack(push, 8)
struct EditorGizmoApi
{
public:
    void* Line3D = nullptr;
    void* Label3D = nullptr;
    void* TakeEdit = nullptr;              //取出一次待提交的手柄编辑，没有时返回 0
    void* IsSelected = nullptr;
    void* IsVisible = nullptr;
};
#pragma pack(pop)

ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorGizmoApi, 5);
