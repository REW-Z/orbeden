#pragma once

#include "Defines/types.h"
#include "Runtime/EngineTypes.h"

#include <cmath>

//sRGB 传输函数（IEC 61966-2-1 的分段曲线）。
//
//这是 C++ 侧唯一的颜色空间转换实现，着色器不得再自带副本：显示编码由输出 Pass
//统一完成，资源解码由 sRGB 纹理格式交给硬件。管线约定见 Docs/ColorPipeline.md。
namespace ColorSpace
{
    //sRGB 编码值解码到线性
    inline float32 SrgbToLinear(float32 value)
    {
        if (value <= 0.04045f) return value / 12.92f;
        return std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    //线性值编码为 sRGB
    inline float32 LinearToSrgb(float32 value)
    {
        if (value <= 0.0f) return 0.0f;
        if (value <= 0.0031308f) return value * 12.92f;
        return 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    }

    //只转换 RGB：alpha 是覆盖率，不属于颜色空间
    inline color SrgbToLinear(const color& value)
    {
        return color{ SrgbToLinear(value.r), SrgbToLinear(value.g), SrgbToLinear(value.b), value.a };
    }

    //只转换 RGB，alpha 原样保留
    inline color LinearToSrgb(const color& value)
    {
        return color{ LinearToSrgb(value.r), LinearToSrgb(value.g), LinearToSrgb(value.b), value.a };
    }
}
