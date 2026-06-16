#pragma once

#include "Editor/EditorGizmoBridge.h"
#include "Runtime/Managed/ScriptSystem.h"

#include <string>

// Editor 托管覆盖层，负责调用 C# Editor Panel 和 Gizmos。
class ManagedEditorOverlay
{
private:
    ScriptSystem* scriptSystem = nullptr;
    EditorGizmoBridge gizmoBridge;
    void* DrawPanelsFunction = nullptr;
    void* DrawSceneGizmosFunction = nullptr;
    bool initialized = false;

public:
    // 初始化 Editor 托管覆盖层。
    bool Initialize(ScriptSystem& runtime, const std::string& executablePath);

    // 关闭 Editor 托管覆盖层。
    void Shutdown();

    // 绘制 C# Editor panels。
    void DrawPanels();

    // 绘制 C# SceneView Gizmos。
    void DrawSceneGizmos(const matrix4x4& viewProjection, int32 viewportWidth, int32 viewportHeight);

    // 判断 Editor 托管覆盖层是否可用。
    bool IsInitialized() const;
};
