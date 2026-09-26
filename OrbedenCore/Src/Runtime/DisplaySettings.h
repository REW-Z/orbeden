#pragma once

#include "Defines/types.h"

//项目共享的显示参数，缺少配置时保持默认值。
//
//曝光描述的是观察方式，和描述场景光照的灯光强度是两件事，所以它是项目级设置而不是
//世界级渲染设置：同一份场景在不同项目里可以有不同曝光。相机可以通过
//Camera.overrideExposure 单独覆盖，见 Docs/ColorPipeline.md。
class DisplaySettings
{
public:
    static constexpr const char* FileName = "ProjectSettings.display";

    //线性曝光倍数，作用于色调映射之前
    static constexpr float32 DefaultExposure = 1.0f;

    //内容根或配置文件变化时重新读取。读取开销很低，按需调用即可。
    static void Refresh();

    //获取线性曝光倍数
    static float32 GetExposure();
};
