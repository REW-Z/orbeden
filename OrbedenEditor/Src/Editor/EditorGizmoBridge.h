#pragma once

#include "Defines/types.h"
#include "Rendering/RenderTypes.h"

// Editor Gizmo 三维向量，布局与 C# Orbeden.Vector3 一致。
struct EditorGizmoVector3
{
public:
    float32 x = 0.0f;
    float32 y = 0.0f;
    float32 z = 0.0f;
};

// Editor Gizmo 颜色，布局与 C# Orbeden.Color 一致。
struct EditorGizmoColor
{
public:
    float32 r = 1.0f;
    float32 g = 1.0f;
    float32 b = 1.0f;
    float32 a = 1.0f;
};

// Editor Gizmo 原生函数表，传给 C# Editor 保存。
struct EditorGizmoApi
{
public:
    void* Line3D = nullptr;
    void* Label3D = nullptr;
};

// Editor Gizmo 桥接层，负责把三维 Gizmo 投影到 ImGui 绘制。
class EditorGizmoBridge
{
private:
    matrix4x4 viewProjection;
    int32 viewportWidth = 0;
    int32 viewportHeight = 0;

public:
    // 获取 Editor Gizmo 原生函数表。
    EditorGizmoApi GetApi();

    // 获取当前 ViewProjection 矩阵。
    const matrix4x4& GetViewProjection() const;

    // 获取当前视口宽度。
    int32 GetViewportWidth() const;

    // 获取当前视口高度。
    int32 GetViewportHeight() const;

    // 设置当前 SceneView 投影上下文。
    void BeginFrame(const matrix4x4& newViewProjection, int32 width, int32 height);

    // 清理当前 SceneView 投影上下文。
    void EndFrame();
};
