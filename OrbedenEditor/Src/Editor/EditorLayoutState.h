#pragma once

#include "Runtime/EngineTypes.h"

#include <string>

//编辑器面板布局状态。
struct EditorPanelState
{
public:
    std::string id;
    bool visible = true;
    bool hasPosition = false;
    bool hasSize = false;
    vector2 position = { 0.0f, 0.0f };
    vector2 size = { 0.0f, 0.0f };
    int32 dockNode = -1;
};

//编辑器停靠节点布局状态。
struct EditorDockNodeState
{
public:
    int32 id = 0;
    int32 firstChild = -1;
    int32 secondChild = -1;
    bool vertical = true;
    float32 ratio = 0.5f;
    bool workspace = false;
    std::string activePanel;
};

//编辑器观察相机布局状态。
struct EditorCameraState
{
public:
    bool hasValue = false;
    vector3 position = { 5.0f, 3.2f, 7.0f };
    float32 yaw = 35.0f;
    float32 pitch = -22.0f;
};

//编辑器项目布局状态。
struct EditorLayoutState
{
public:
    List<EditorPanelState> panels;
    List<EditorDockNodeState> dockNodes;
    int32 dockRoot = -1;
    EditorCameraState editorCamera;
};
