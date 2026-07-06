#pragma once

#include "Runtime/Gui/RuntimeGuiBridge.h"
#include "Runtime/Native/RuntimeComponentBinds.h"

//传给 AOT GameModule 的引擎原生 API。
struct OrbedenNativeApi
{
public:
    RuntimeGuiApi Gui;
    WorldBind World;
    EnsBind Ens;
    SpaceComponentBind SpaceComponent;
    StaticMeshRendererBind StaticMeshRenderer;

    //创建完整原生 API 函数表。
    static OrbedenNativeApi Create();
};
