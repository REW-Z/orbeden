#pragma once

#include "Runtime/Object/Object.h"
#include "Runtime/Object/Skybox.h"

//世界级渲染环境参数，不挂在具体 Ens 上。
struct RenderSettings
{
public:
    Ref<Skybox> skybox;
    bool skyboxEnabled = false;
    //sRGB 语义（与检视面板一致），生成渲染快照时转线性。这是晴天下天光散射的量级：
    //配合 DirectionalLight.intensity = π，受光面与暗部约 10:1。属于需要目视校准的起点值。
    color ambientColor = { 0.34f, 0.37f, 0.42f, 1.0f };
};

