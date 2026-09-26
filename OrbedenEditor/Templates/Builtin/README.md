# Builtin Shader / Material 示例

这些资源使用现有 Opaque、Transparent、Refraction 队列。Material 默认继承 Shader 的队列，无需设置 Renderer。新建项目自动复制到 `Content/Builtin/`；已有项目升级保留用户内容，需要手动复制新增文件及 include，保留同名自定义文件，然后重新导入资源。Dev 面板的 `Reset Builtin` 可以整目录从模板回退：它按镜像语义执行，会覆盖同名自定义文件、删掉模板里没有的文件，只想要新增文件时仍按上面手工复制。

## 选择材质

Project 面板选择 `.orbmat` 材质资产，放入 `StaticMeshRenderer.materials` 对应子网格槽位。一个 `.orbmat` 文件就是一个 Material，对象名称取自文件名，资源 Key 直接使用内容根相对路径，例如 `Builtin/Materials/rain_glass.orbmat`。`.mtl` 仅是 OBJ 的附属原始文件；通用对象产物使用 `.orbo`。

| 材质文件（Materials/） | 对象名称 | 队列 | 用途 |
|---|---|---|---|
| rain_glass.orbmat | rain_glass | Refraction | 持续向下流动的双层雨滴、水痕、背景扭曲和主光高光 |
| heat_haze.orbmat | heat_haze | Refraction | 向上流动的热空气扰动，四边渐隐 |
| engine_wake.orbmat | engine_wake | Refraction | 从窄喷口向外扩散、末端衰减的快速尾流 |
| clear_glass.orbmat | clear_glass | Refraction | 复用基础 refraction Shader 的轻度染色玻璃 |
| transparent_tint.orbmat | transparent_tint | Transparent | 普通 alpha 混合的透明蓝色表面 |
| pbs_copper.orbmat | pbs_copper | Opaque | 较光滑的铜 |
| pbs_rough_metal.orbmat | pbs_rough_metal | Opaque | 粗糙金属（各向同性） |
| pbs_blue_plastic.orbmat | pbs_blue_plastic | Opaque | 蓝色非金属塑料 |
| pbs_rubber.orbmat | pbs_rubber | Opaque | 深色粗糙橡胶 |

C# 示例（`renderer` 为已有网格渲染组件）：

```csharp
Material rain = Resources.Load<Material>("Builtin/Materials/rain_glass.orbmat");
renderer.materials = [rain];
renderer.castShadows = false;
rain.SetFloat("u_FlowSpeed", 0.8f);
rain.SetFloat("u_RainAmount", 0.9f);
```

加载到的是共享材质，脚本修改会影响使用它的全部对象；需要独立配置时复制 `.orbmat` 为另一个资源。

## 雨玻璃

使用 `Shaders/rain_glass.orbshader`。可放在窗玻璃网格，或用 `Meshes/quad.obj//Mesh/Main` 制作一块玻璃。网格必须有 UV0 和有效法线；UV 的 V=0 对应玻璃下边缘，V=1 对应上边缘。雨滴沿负 V 流动，不依赖世界重力；网格 UV 倒置时调整 UV 或把速度设为负值。双面可见、开启深度测试、不写深度。

- `u_DropletScale`：密度／尺寸，预设 13；增大后水滴更小、更密。
- `u_FlowSpeed`：向下移动速度，预设 0.65；0 为静止，负值反向。
- `u_RainAmount`：0～1 的水滴覆盖量，0 关闭雨滴。
- `u_TrailStrength`：水痕强度，预设 0.20。
- `u_RefractionStrength`：表面 UV 偏移强度，预设 0.008。
- `u_TintColor` / `u_TintStrength`：玻璃染色及其强度。
- `u_Opacity`：与已经绘制的背景混合的权重，通常用 1；玻璃的透视来自相机颜色采样，不需要降低 opacity 才看得到背景。

雨滴和拖尾通过程序生成，不需要外部雨滴贴图。时间由管线的 `u_Time` 提供，运行后自动流动。偏移随玻璃 UV 投影到屏幕，因此旋转玻璃时流动方向也跟随表面。请把法线、UV 和朝向配置正确。

## 热浪与尾流

两种预设共享 `Shaders/heat_wake.orbshader`，使用多尺度噪声及流线扰动，不需要噪声贴图。

热浪：使用面向观察方向的 Quad，V=0 在下方。尾流：把 Quad 拉长、V=0 放在喷口处，V=1 指向尾流末端；不是自动跟随相机的 billboard，需要自行调整面片朝向。可以使用交叉面片，但重叠折射有以下限制。

- `u_WakeShape`：0 为矩形热浪，1 为逐渐扩散的尾流。
- `u_DistortionSpeed`：沿正 V 推进的速度；热浪 1.2，尾流 4，负值反向。
- `u_NoiseScale`：扰动频率，越大越细碎。
- `u_DistortionStrength`：表面 UV 偏移强度；热浪 0.018，尾流 0.035。
- `u_EdgeFade`：UV 边缘渐隐宽度。
- `u_SoftIntersection`：与场景几何相交时的软化距离，采用场景世界单位。
- `u_Opacity`：扰动合成权重；材质不会额外绘制烟雾、火焰或发光。

两类折射都会使用现有深度纹理拒绝前景采样。管线在普通透明绘制后只复制一次相机颜色和深度，所以这些材质能扭曲不透明和普通透明背景，但不会递归折射其他折射面。屏幕外物体无法被采样；没有可用相机纹理时效果不绘制。

## PBS 金属／粗糙度

`Shaders/pbs_metallic.orbshader` 使用 GGX 法线分布、Smith 几何遮蔽、Schlick 菲涅耳以及能量分配的漫反射／镜面项，接入当前方向主光和级联阴影。

- `u_DiffuseColor`：基色。
- `u_Metallic`：0 为非金属，1 为金属。
- `u_Roughness`：粗糙度，Shader 下限 0.045。
- `u_Occlusion`：仅作用于环境漫反射的遮蔽系数。
- `u_EmissionColor`：自发光颜色。
- `texture u_DiffuseTexture Textures/albedo.png`：可选基色贴图，使用内容根相对 Key。

这是基于当前前向管线的 PBS 直接光示例。**管线是线性工作空间**：采样由纹理的 sRGB 格式解码，材质颜色由绑定层转成线性，曝光、色调映射与 sRGB 编码统一由管线末端的输出 Pass 完成。GGX、漫反射的 `1/π` 归一化和 AO 作用范围都建立在这个前提上，因此 **Shader 内不得再做任何颜色空间转换**——那会变成双重编码，画面明显过亮发灰。约定见 [颜色管线](../../../Docs/ColorPipeline.md)。

同族的 `blinn_phong` 与 `transparent` 也按线性重写了数学：漫反射除以 π，镜面按菲涅耳从漫反射里扣除能量。`u_SpecularColor` 直接作为垂直入射反射率，默认的黑色即无高光，与旧行为一致。

目前没有 IBL／环境镜面反射或法线贴图；金属在没有主光高光的方向仍会较暗，属于缺少环境镜面光照的限制。雨玻璃的掠射亮光也是近似值，并非场景反射。已有游戏项目需同步 `Content/Builtin/` 下的 Shader 与 include 并重新导入，仅修改编辑器模板不会自动覆盖项目内容；旧版 `pbs_metallic.orbshader` 带着手写的 sRGB 编码，必须一并替换。

## 自定义 orbmat 参数

在 `.orbmat` 文件中填写以下参数，槽名为 GLSL uniform 的完整名称；不写 `newmtl` 或材质名称：

```text
shader Builtin/Shaders/rain_glass.orbshader
float u_FlowSpeed 0.8
color u_TintColor 0.9 0.95 1.0 1.0
drawqueue Auto
```

`float` 接受一个数，`color` 接受四个 RGBA 数；参数行末不加注释，注释请单独起行。`drawqueue Auto` 继承 Shader，必要时可覆盖为 Opaque、Transparent 或 Refraction。编辑 `.orbmat` 后重新导入；Shader Pass 中显式配置的 Blend/DepthWrite 不会因材质队列覆盖自动改变。
