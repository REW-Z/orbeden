#pragma once

#include "Defines/types.h"

//天空背景的来源。数值稳定，编辑器 ABI 与世界文件按数值保存。
enum class SkyMode : uint32
{
    //已加载的天空盒立方体贴图。
    Cubemap = 0,
    //按大气参数实时积分出的程序化天空。
    Atmosphere = 1,
};

//大气积分的质量档位。数值稳定，编辑器 ABI 与世界文件按数值保存。
enum class AtmosphereQuality : uint32
{
    Low = 0,
    Balanced = 1,
};

//贴地浓密介质，与球形大气的薄霾分开配置。
//能见度按 MOR 定义（5% 透射阈值）：sigmaT = -ln(0.05) / visibilityMeters。
//该值只描述浓雾自身的消光，不含背景大气，界面与实现口径一致。
//密度只随相对参考球面的高度变化，首版不做水平雾区、三维噪声与体积阴影。
struct DenseFogSettings
{
public:
    bool enabled = false;
    //浓雾参考高度处的能见度，单位米
    float32 visibilityMeters = 200.0f;
    //雾层底面高度，单位米
    float32 referenceHeightMeters = 0.0f;
    //雾层厚度，单位米
    float32 layerHeightMeters = 100.0f;
    //底面过渡宽度，单位米，在层内由零升到满密度
    float32 fadeMeters = 30.0f;
    //雾顶衰减尺度，单位米；名义顶部密度约为 5%，上方延伸稀薄尾部，不得超过层厚
    float32 topFadeMeters = 20.0f;
    //环境光散射倍率，颜色取自世界环境光
    float32 scatteringScale = 1.0f;
    //主光散射倍率；1 表示浓雾被主光完全照亮时与白色漫反射面同亮度
    float32 sunScatteringScale = 1.0f;
    //调试视图：0 关闭，1 命中与区间数，2 首次进入距离，3 穿雾总长，
    //4 光学厚度，5 透光率，6 散射贡献，7 入口前的大气贡献
    int32 debugView = 0;
};

//世界级大气参数，不挂在具体 Ens 上。
//密度只随距地心的高度变化，地形起伏不参与；长度单位统一为米。
struct AtmosphereSettings
{
public:
    //只控制物体表面的空气透视与浓雾，不控制程序化天空、太阳盘、灯光或反射。
    bool fogEnabled = false;
    AtmosphereQuality quality = AtmosphereQuality::Low;
    //浓雾介质，默认关闭，老场景画面不变
    DenseFogSettings denseFog;

    //地心在当前世界原点坐标系内的位置，单位为米。
    float64 planetCenterX = 0.0;
    float64 planetCenterY = -6371000.0;
    float64 planetCenterZ = 0.0;

    //一个世界单位对应的米数，允许世界用更小的尺度表达很长的距离。
    float32 metersPerWorldUnit = 1.0f;
    //Mie 散射与消光的倍率，0 只关闭 Mie，Rayleigh 保留。
    float32 aerosolDensity = 1.0f;
    //散射亮度与太阳盘的倍率，不改变消光、表面主光或环境光。
    float32 sunRadianceScale = 20.0f;
};
