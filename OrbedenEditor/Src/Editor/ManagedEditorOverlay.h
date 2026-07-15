#pragma once

#include "Editor/EditorClrHost.h"
#include "Editor/EditorGizmoBridge.h"
#include "Runtime/EnsId.h"

#include <string>

// Editor 托管覆盖层，负责调用 C# Editor Panel 和 Gizmos。
class ManagedEditorOverlay
{
private:
    EditorClrHost* clrHost = nullptr;
    EditorGizmoBridge gizmoBridge;
    void* DrawPanelsFunction = nullptr;
    void* DrawSceneGizmosFunction = nullptr;
    void* LoadGameAssemblyFunction = nullptr;
    void* UnloadGameAssemblyFunction = nullptr;
    void* DrawInspectorFunction = nullptr;
    void* DrawInspectorContentFunction = nullptr;
    void* DrawEditorPanelContentFunction = nullptr;
    void* PublishGameAotFunction = nullptr;
    bool initialized = false;

public:
    // 初始化 Editor 托管覆盖层。
    bool Initialize(EditorClrHost& host, const std::string& executablePath);

    // 关闭 Editor 托管覆盖层。
    void Shutdown();

    // 绘制 C# Editor panels。
    void DrawPanels();

    // 加载用户游戏程序集。
    void LoadGameAssembly(const std::string& assemblyPath, const std::string& sidecarPath);

    // 卸载用户游戏程序集引用。
    void UnloadGameAssembly();

    // 绘制 C# Inspector。
    void DrawInspector(EnsId selectedEns, const std::string& stableId);

    // 绘制 C# Inspector 内容。
    void DrawInspectorContent(EnsId selectedEns, const std::string& stableId);

    // 绘制 C# Editor 面板内容。
    void DrawEditorPanelContent();

    // 绘制 C# SceneView Gizmos。
    void DrawSceneGizmos(const matrix4x4& viewProjection, int32 viewportWidth, int32 viewportHeight);

    // 使用 Editor C# 发布用户游戏 NativeAOT 静态库。
    bool PublishGameAot(const std::string& repositoryRoot,
        const std::string& projectRoot,
        const std::string& scriptProject,
        const std::string& configuration,
        const std::string& targetPlatform,
        std::string& error);

    // 判断 Editor 托管覆盖层是否可用。
    bool IsInitialized() const;
};
