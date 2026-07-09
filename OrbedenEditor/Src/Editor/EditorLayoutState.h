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
    EditorCameraState editorCamera;
};
