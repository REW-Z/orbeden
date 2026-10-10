# 颜色管线

Orbeden 的颜色空间是**定死**的，不提供用户开关。本文记录定死了什么、每一处转换发生在哪里、以及为什么这样定。

## 管线全貌

```
资源（sRGB 编码字节）
   │  采样时由 sRGB 纹理格式硬件解码
   ▼
线性工作空间 ──────────── 全部光照计算在这里，HDR、float
   │
   │  场景缓冲 RGBA16F（每相机一块，引擎分配）
   │  大气查找表与空气透视在线性空间计算并合成在这里
   ▼
输出 Pass：曝光 → AgX 色调映射 → sRGB 编码
   │
   ▼
最终目标：默认窗口帧缓冲（游戏）/ 编辑器显示目标（编辑器）
```

## 定死的内容

| 项 | 取值 | 说明 |
| --- | --- | --- |
| 工作空间 | 线性 sRGB / Rec.709 | 光照、混合、mipmap 全部在线性空间 |
| 场景缓冲 | `RGBA16F` | 线性 HDR，容得下大于 1 的亮度 |
| 显示编码 | sRGB（IEC 61966-2-1 分段曲线） | 只有这一种 |
| 色调映射 | AgX | 在输出 Pass 内，参数固定 |
| 输出位置 | 输出 Pass 一处 | 全引擎唯一的显示编码点 |
| 光照强度刻度 | 线性美术强度，默认 1.0 | 直接传入 Shader，漫反射单位响应、镜面配套归一化 |

**没有 gamma / sRGB / 线性开关。** 引擎统一使用线性工作空间与 sRGB 显示编码。详见 [渲染管线](RenderingPipeline.md)。

## 各路数据的颜色语义

| 数据 | 语义 | 转换点 |
| --- | --- | --- |
| 颜色贴图（albedo、自发光、天空盒） | sRGB | 纹理采样，硬件解码 |
| 数据贴图（法线、粗糙度、遮罩、高度、深度） | 线性 | 不转换 |
| 材质颜色槽、`.orbmat` 的 `color` 行、脚本 `SetColor` | sRGB | `GpuResourceManager::GetMaterial` 送 GPU 前转线性 |
| 环境光、清屏色、光源颜色 | sRGB | `RenderScene` 生成渲染快照时转线性 |
| 描边色、调试线色 | sRGB | `RenderSystem` 写入场景缓冲前转线性 |

**所有跨 API 边界的颜色都是 sRGB 语义。** 检视面板、`.world`、`.orbmat`、脚本 API 全部一致；线性值只存在于渲染内部。

## 唯一允许的转换实现

- C++：`Rendering/ColorSpace.h` 的 `ColorSpace::SrgbToLinear` / `LinearToSrgb`
- 显示编码：`Rendering/OutputPass.cpp` 内的输出 Pass 着色器

**着色器里不得再做任何颜色空间转换。** 采样已经由纹理格式解码，材质颜色已经由绑定层转好，输出编码由输出 Pass 统一完成。曾经 `pbs_metallic.orbshader` 自带一份 `LinearToSrgb`，那是过渡期的临时状态，已经移除；新写着色器时不要重复这个错误——多一次编码就是双重编码，画面会明显过亮发灰。

## 光照强度

`DirectionalLight.intensity` 使用 **美术强度刻度**，默认 `1.0`，通常从 `0~1` 调节，允许大于 `1`。它是无量纲的亮度倍率，不是 lux。

面板、`.world`、脚本 API、渲染快照和 `u_LightIntensity` 都使用同一刻度，不在 CPU 侧换算。Shader 中直接光漫反射不除 π，镜面采用物理 BRDF 乘 π 的配套归一化；Blinn-Phong 的镜面系数直接约去 π。以理想 Lambert 漫反射为例：

```
u_LightIntensity = intensity
direct = albedo · nDotL · lightColor · intensity
```

白色理想漫反射表面正对白光时，`intensity = 1` 得到 1.0 的线性直接光结果。实际 PBS 还包含菲涅耳、金属度和镜面项；最终显示还受环境光、阴影、曝光与 AgX 影响，因此不能把 `1` 当作过曝阈值。

该归一化同时作用于漫反射和镜面，保持二者的能量比例。GGX 法线分布函数仍保留自己的数学归一化，不能将 Shader 中所有 π 不加区分地删除。环境反射与特效 Shader 按各自模型使用线性美术强度。

## 环境光强度

环境光同样将颜色与强度分开：`RenderSettings.ambientColor` 是 sRGB 颜色，`ambientIntensity` 是线性美术强度，默认 `1`、`0` 关闭、允许大于 `1`。Rendering 面板分别编辑两者，均保存在 `.world` 中。

环境漫反射使用 `SrgbToLinear(ambientColor) · ambientIntensity · albedo`。这里已采用积分后的近似环境光形式，不需要再乘 π。方向光和环境光都不对强度做 sRGB 解码，强度减半都会让各自贡献的线性光照减半；二者的颜色仍统一按 sRGB 解码。

## 环境镜面反射

`RenderSettings.reflectionEnvironment` 指定全局反射环境；为空时使用 `skybox`。`reflectionIntensity` 是默认 `1` 的线性倍率，`0` 关闭反射，不受 `skyboxEnabled` 和环境漫反射强度影响。颜色 Cubemap 由 sRGB 格式硬件解码，mip 采样和反射合成在线性空间进行，不对反射强度乘 π 或做 sRGB 解码。

第一版使用普通 Cubemap mip 近似粗糙度模糊，叠加带粗糙度修正的 Schlick 环境菲涅耳；PBS 使用金属度混合后的 F0 与材质遮蔽，Blinn-Phong 和普通透明材质使用镜面颜色并从光泽指数估算粗糙度。环境反射不乘方向光的 NdotL、强度或阴影。

这不是完整的预过滤 IBL：没有 GGX 卷积、BRDF LUT、天空辐照度卷积和局部场景反射，最高 mip 仍可能存在面间差异。环境图片目前是 LDR 输入，场景合成仍为 HDR。内置资源与设置方法见 [Builtin 说明](../OrbedenEditor/Templates/Builtin/README.md)。

## 大气与空气透视

大气查找表存的是线性量：太阳透光率、散射亮度与光学厚度都不做任何编码转换，图集与天空查找表用 `RGBA16F`、线性过滤、无 mip。表面合成 `rgb × T + L` 在线性场景缓冲内完成，因此空气透视与场景光照共用同一条曝光与色调映射路径；输出 Pass 不需要为它做任何处理。

大气参数里的太阳辐射按 `mainLight.color × intensity × sunRadianceScale` 在线性空间合成，`sunRadianceScale` 不改变消光、表面主光与环境光，也不表示 lux。地面的颜色底板取自相机已线性化的清屏色，不是实体地面。

浓雾的散射亮度是两项之和：世界环境光（已含 `ambientIntensity`）乘 `fogScatteringScale`，加上主方向光的 `color × intensity` 乘 `fogSunScatteringScale`；再按 `散射色 × (1 − 透光率)` 计入，全程在线性空间。

只用环境光时浓雾会明显偏暗——环境光是柔和的补光，而实际浓雾的亮度主要由被多次散射的太阳光决定，`Fog Sun Scattering` 默认 `1` 让浓雾被主光完全照亮时与白色漫反射面同亮度。两项都是美术照明量，夜间同时趋零，因此不会出现自发光的白色覆盖层。

## 曝光

曝光描述的是**观察方式**，与描述场景光照的灯光强度互不影响，因此两者分开：

- 项目级默认值：项目根下 `GameSettings.ini` 的 `[Rendering]` 分块里的 `exposure`（线性倍数），由 Rendering 面板编辑
- 相机级覆盖：`Camera.overrideExposure` + `Camera.exposure`，在 Inspector 里编辑
- 世界文件**不保存**曝光：同一份场景在不同项目里可以有不同曝光

曝光只作用于输出 Pass，不会乘进 `u_LightIntensity`。

## 输出 Pass 的两种模式

| 模式 | 行为 | 用途 |
| --- | --- | --- |
| `Display` | 曝光 + AgX + sRGB 编码 | 常规相机 |
| `EncodeOnly` | 跳过曝光与色调映射，只做 sRGB 编码 | 预留 |

## 已知限制

- **纹理级语义无法表达「同一张图两种用途」**。是否解码是 `Texture2D.colorSpace` 的属性，同一张图若既要当颜色贴图又要当数据贴图，必须拆成两个资源。glTF 导入遇到这种引用冲突会报错而不是静默取值。
- **资源颜色空间可以覆盖，但只对图片源开放**。在 Inspector 里选中一张图片即可编辑 Import Settings 的 Color Space；设置写进源文件旁的 `.resinfo`，重新导入时保留。**复合资源（glTF/OBJ）的子贴图仍按语义自动判定**（baseColor/emissive 判为 sRGB，normal 判为 Linear 等），源级的 `colorSpace` 键对它们无效——一个 `.gltf` 里的多张贴图各有用途，一个文件级的键表达不了。需要单独控制时把贴图拆成独立图片资源。
- **独立导入的图片默认 sRGB**，导入器无从判断用途。法线、粗糙度、遮罩这类数据贴图必须显式改成 `Linear`，否则会被错误解码（平坦法线会偏 39°，表面光照明显变形）。
- **描边与调试线会被色调映射影响**。它们画在线性场景缓冲上、与场景一起过输出 Pass，所以不是所见即所得。要做到精确控制需让显示目标与场景缓冲共享深度纹理，留作后续。
- **不做 HDR 显示输出**。工作空间已经是 HDR，以后要接 PQ / Rec.2020 只需替换输出 Pass 的最后一步。

## 相关

- [渲染管线](RenderingPipeline.md)：Pass 顺序与渲染目标
- [级联阴影](CascadedShadows.md)：阴影贴图与 SDSM
- 已有项目的内置 Shader 同步见 [构建与打包](BuildAndPackaging.md) 的「引擎更新与项目同步」一节
