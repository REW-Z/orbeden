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

**没有 gamma / sRGB / 线性开关。** 早期讨论过要不要像 Unity 那样提供选择，结论是不提供：那类开关是历史包袱的产物（跨平台硬件能力、免费版分层、存量内容迁移），而本引擎不背这些包袱。详见 [渲染管线](RenderingPipeline.md)。

## 各路数据的颜色语义

| 数据 | 语义 | 转换点 |
| --- | --- | --- |
| 颜色贴图（albedo、自发光、天空盒） | sRGB | 纹理采样，硬件解码 |
| 数据贴图（法线、粗糙度、遮罩、高度、深度） | 线性 | 不转换 |
| 材质颜色槽、`.orbmat` 的 `color` 行、脚本 `SetColor` | sRGB | `GpuResourceManager::GetMaterial` 送 GPU 前转线性 |
| 环境光、清屏色、光源颜色 | sRGB | `RenderScene` 生成渲染快照时转线性 |
| 描边色、调试线色 | sRGB | `RenderSystem` 写入场景缓冲前转线性 |
| 阴影调试色板 | 显示色 | 不转换；输出 Pass 在调试视图下走 `Copy` 模式原样输出 |

**所有跨 API 边界的颜色都是 sRGB 语义。** 检视面板、`.world`、`.orbmat`、脚本 API 全部一致；线性值只存在于渲染内部。

## 唯一允许的转换实现

- C++：`Rendering/ColorSpace.h` 的 `ColorSpace::SrgbToLinear` / `LinearToSrgb`
- 显示编码：`Rendering/OutputPass.cpp` 内的输出 Pass 着色器

**着色器里不得再做任何颜色空间转换。** 采样已经由纹理格式解码，材质颜色已经由绑定层转好，输出编码由输出 Pass 统一完成。曾经 `pbs_metallic.orbshader` 自带一份 `LinearToSrgb`，那是过渡期的临时状态，已经移除；新写着色器时不要重复这个错误——多一次编码就是双重编码，画面会明显过亮发灰。

## 曝光

曝光描述的是**观察方式**，与描述场景光照的灯光强度互不影响，因此两者分开：

- 项目级默认值：`Content/ProjectSettings.display` 的 `exposure`（线性倍数），由 Rendering 面板编辑
- 相机级覆盖：`Camera.overrideExposure` + `Camera.exposure`，在 Inspector 里编辑
- 世界文件**不保存**曝光：同一份场景在不同项目里可以有不同曝光

曝光只作用于输出 Pass，不会乘进 `u_LightIntensity`。

## 输出 Pass 的三种模式

| 模式 | 行为 | 用途 |
| --- | --- | --- |
| `Display` | 曝光 + AgX + sRGB 编码 | 常规相机 |
| `EncodeOnly` | 跳过曝光与色调映射，只做 sRGB 编码 | 预留 |
| `Copy` | 原样复制 | 阴影调试视图；调色板写的就是显示色 |

## 已知限制

- **纹理级语义无法表达「同一张图两种用途」**。是否解码是 `Texture2D.colorSpace` 的属性，同一张图若既要当颜色贴图又要当数据贴图，必须拆成两个资源。glTF 导入遇到这种引用冲突会报错而不是静默取值。
- **资源颜色空间可以覆盖，但只对图片源开放**。在 Inspector 里选中一张图片即可编辑 Import Settings 的 Color Space；设置写进源文件旁的 `.resinfo`，重新导入时保留。复合资源（glTF/OBJ）的子贴图按语义自动判定（baseColor/emissive 判为 sRGB，normal 判为 Linear 等），没有独立的设置入口——一个 `.gltf` 里的多张贴图各有用途，一个文件级的键表达不了。需要单独控制时把贴图拆成独立图片资源。
- **描边与调试线会被色调映射影响**。它们画在线性场景缓冲上、与场景一起过输出 Pass，所以不是所见即所得。要做到精确控制需让显示目标与场景缓冲共享深度纹理，留作后续。
- **不做 HDR 显示输出**。工作空间已经是 HDR，以后要接 PQ / Rec.2020 只需替换输出 Pass 的最后一步。

## 相关

- [渲染管线](RenderingPipeline.md)：Pass 顺序与渲染目标
- [级联阴影](CascadedShadows.md)：阴影贴图与 SDSM
- 老项目的内置 Shader 迁移见 [构建与打包](BuildAndPackaging.md) 的版本 20 记录
