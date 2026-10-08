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

//浓雾的密度模型。数值稳定，编辑器 ABI 与世界文件按数值保存。
enum class DenseFogMode : uint32
{
    //高度雾：密度只随高度变化，满密度到雾顶、顶部平滑衰减。
    //近地天气与穿云用它，代价是要配雾顶与衰减尺度。
    Height = 0,
    //距离雾：密度与高度无关，透光率只由到相机的距离决定（exp(-消光 × 距离)）。
    //没有高度参数，脚本按飞行高度改能见度即可；但同一帧里画面各处的雾浓度相同，
    //做不出"谷地被雾埋住、山脊露在外面"。
    Distance = 1,
};

//贴地浓密介质，与球形大气的薄霾分开配置。
//能见度按 MOR 定义（5% 透射阈值）：sigmaT = -ln(0.05) / visibilityMeters。
//该值只描述浓雾自身的消光，不含背景大气，界面与实现口径一致。
//密度只随相对参考球面的高度变化，首版不做水平雾区、三维噪声与体积阴影。
//雾层没有底面：低于雾顶高度的所有高度都保持满密度，只在雾顶一侧衰减。
//高度沿射线按平地近似积分：介质尺度（能见度几十米到几公里、雾顶几十米到几公里）远小于地球半径，
//整条剖面有初等原函数，逐像素只做一次求值，不做求积也不求交。
struct DenseFogSettings
{
public:
    bool enabled = false;
    //密度模型。距离雾下雾顶与衰减尺度不参与计算，界面也不展示
    DenseFogMode mode = DenseFogMode::Height;
    //浓雾参考高度处的能见度，单位米
    float32 visibilityMeters = 200.0f;
    //雾顶高度，单位米，相对参考球面
    float32 topHeightMeters = 100.0f;
    //雾顶衰减尺度，单位米；名义顶部密度约为 5%，上方延伸稀薄尾部，不得超过雾顶高度
    float32 topFadeMeters = 20.0f;
    //环境光散射倍率，颜色取自世界环境光
    float32 scatteringScale = 1.0f;
    //主光散射倍率；1 表示浓雾被主光完全照亮时与白色漫反射面同亮度
    float32 sunScatteringScale = 1.0f;
};

//世界级大气参数，不挂在具体 Ens 上。
//密度只随距地心的高度变化，地形起伏不参与；长度单位统一为米。
struct AtmosphereSettings
{
public:
    //只控制物体表面的空气透视，不控制浓雾（浓雾有自己的开关）、程序化天空、太阳盘、灯光或反射。
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
