#pragma once

#include "Runtime/AtmosphereSettings.h"
#include "Runtime/Object/Object.h"
#include "Runtime/Object/Skybox.h"

//世界级渲染环境参数，不挂在具体 Ens 上。
struct RenderSettings
{
public:
    Ref<Skybox> skybox;
    bool skyboxEnabled = false;
    //背景来源，与 skyboxEnabled 相互独立：开关决定画不画背景，模式决定画哪一种。
    SkyMode skyMode = SkyMode::Cubemap;
    //大气参数：天空模式选程序化天空、空气透视开关都会读这里的字段。
    AtmosphereSettings atmosphere;
    //sRGB 颜色，生成渲染快照时转线性；默认颜色用于柔和的环境补光
    color ambientColor = { 0.34f, 0.37f, 0.42f, 1.0f };
    //线性美术强度，与方向光一致：0 关闭，1 使用基准亮度，允许大于 1
    float32 ambientIntensity = 1.0f;
    //独立反射环境；未指定时使用 skybox，不受背景显示开关影响
    Ref<Skybox> reflectionEnvironment;
    //环境镜面反射的线性强度，0 关闭，1 使用环境原始亮度
    float32 reflectionIntensity = 1.0f;
};

