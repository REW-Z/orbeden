#pragma once

#include "Runtime/Object/Object.h"
#include "Runtime/Object/Skybox.h"

//世界级渲染环境参数，不挂在具体 Ens 上。
struct RenderSettings
{
public:
    Ref<Skybox> skybox;
    bool skyboxEnabled = false;
    color ambientColor = { 0.08f, 0.09f, 0.1f, 1.0f };
};

