#pragma once

#include "Runtime/Object/Object.h"
#include "Runtime/Object/Skybox.h"

//世界级渲染环境参数，不挂在具体 Ens 上。
struct RenderSettings
{
public:
    Ref<Skybox> skybox;
    bool skyboxEnabled = false;
    //sRGB 颜色，生成渲染快照时转线性；默认颜色用于柔和的环境补光
    color ambientColor = { 0.34f, 0.37f, 0.42f, 1.0f };
    //线性美术强度，与方向光一致：0 关闭，1 使用基准亮度，允许大于 1
    float32 ambientIntensity = 1.0f;
};

