# Builtin Shader / Material 示例

这些资源使用现有 Opaque、Transparent、Refraction 队列。Material 默认继承 Shader 的队列，无需设置 Renderer。新建项目自动复制到 `Content/Builtin/`；已有项目升级保留用户内容，需要手动复制新增文件及 include，保留同名自定义文件，然后重新导入资源。Dev 面板的 `Reset Builtin` 可以整目录从模板回退：它按镜像语义执行，会覆盖同名自定义文件、删掉模板里没有的文件，只想要新增文件时仍按上面手工复制。

## 引擎内部 Shader

`Shaders/shadow_depth.orbshader` 与 `Shaders/skybox.orbshader` 不挂材质，由管线自行加载：前者是级联阴影的深度绘制入口，顶点阶段按几何 ABI 取世界矩阵，普通、实例与展开几何共用这一条位置公式；后者绘制天空盒立方体，用自带的视投影与 `xyww` 深度。

两个文件都由引擎**按文件名在内容根内查找**，位置不限，但同一内容根内同名只能有一份——引擎取字典序靠前的一份并打一条告警。v31 起它们属于 Builtin，新建项目与 `Reset Builtin` 都铺到 `Content/Builtin/Shaders/`，升级流程会删除项目级 `Content/Shaders/` 下的旧副本。

要定制这两个 Shader，只能改这份同名文件（引擎固定按文件名取值，改名不生效）；改过之后不要再对 Builtin 执行 `Reset Builtin`，它是镜像语义，会把文件覆盖回模板版本。

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

PBS、Blinn-Phong 和普通透明材质支持全局环境镜面反射，使用 Cubemap mip 近似粗糙反射；目前没有 GGX 预过滤、反射探针、场景捕获或法线贴图。雨玻璃的掠射亮光仍是独立近似值，并非场景反射。

## 物理大气与空气透视

`Shaders/atmosphere_*.orbshader` 与 `atmosphere_common.orbinc`、`atmosphere_sampling.orbinc` 是引擎内置的大气实现，不挂材质，由管线自行加载。Rendering 面板的 Sky Mode 选择背景来源，Atmosphere Fog 控制物体表面的空气透视，两者独立。

| 组合 | 画面 |
|---|---|
| Cubemap ＋ Fog 关 | 完整保留原有天空盒与材质画面 |
| Cubemap ＋ Fog 开 | 天空盒保持原图，场景表面按大气消光与散射加雾；天空盒已含大气时不再整体二次加雾 |
| Atmosphere ＋ Fog 关 | 程序化天空与太阳盘正常显示，场景材质不加空气透视 |
| Atmosphere ＋ Fog 开 | 天空与表面共用同一套密度、太阳输入与单次散射算法 |

参数：`Quality` 选择 Low 或 Balanced；`Aerosol Density` 只缩放 Mie（0 关闭 Mie、保留 Rayleigh）；`Sun Radiance Scale` 只缩放散射亮度与太阳盘；`Planet Center` 是地心在当前世界原点坐标系内的位置（米，默认 6371 km 在原点正下方，即原点位于海平面）；`Meters Per World Unit` 是世界单位到米的换算，世界用更小尺度表达长距离时改它。

### 浓雾

`Builtin/dense_fog.orbinc` 是与薄霾分开的第二种介质，默认关闭，用 Rendering 面板的 Dense Fog 打开。它做的是贴地浓雾，能见度最低 10 米。

- `Visibility (m)`：按 MOR（5% 透射阈值）给出的能见度，换算成消光系数 `sigmaT = -ln(0.05) / 能见度`。它是**浓雾自身的消光**，不含背景大气。到达该距离时透光率约为 0.05，不是硬裁剪距离，更远的物体仍会残留。
- `Fog Base Height` / `Fog Layer Height` / `Fog Fade`：高度剖面。底面高度、层厚与上下边界的过渡宽度，单位米，相对参考球面。相机穿过边界时连续过渡，不切换表现模型。
- `Fog Top Fade (m)`：雾顶衰减尺度。满密度区到 `base + layerHeight - topFade` 为止，层内平滑降到名义顶部的 5% 密度；顶部上方按 `0.05 × (1+t) × exp(-t)` 衰减，两侧密度和一阶导数连续。尾部在顶部上方 24 倍 Top Fade 处截断，此处密度约 4.7×10⁻¹¹。设为 0 保留硬顶部。
- `Fog Ambient Scattering`：环境光散射倍率，颜色取世界环境光。
- `Fog Sun Scattering`：主光散射倍率，颜色取主方向光。`1` 表示浓雾被主光完全照亮时与白色漫反射面同亮度。

浓雾的散射亮度是这两项之和。只有环境光时雾会很暗——环境光是柔和的补光，而实际浓雾的亮度主要由被多次散射的太阳光决定。两项都是美术照明量，夜间（主光强度与环境光都趋零）雾会跟着变暗，不会自发光泛白。

浓雾的透光率由射线在球壳内的弦长解析积分得到，不采样空气透视的查找表；散射照明是低成本近似，与逐段积分的差别在薄霾与浓雾重叠的中间段，两端极限一致。六面天空与程序化天空同样被浓雾遮蔽。浓雾受 `Atmosphere Fog` 总开关控制。

限制：相机高度没有停用阈值，可以从大气层外观察；最长支持 2500 km 透视距离。只实现 Rayleigh＋Mie 单次散射，夜景偏暗，环境照明与反射不自动补偿；浓雾不做水平雾区、三维噪声、降落灯光束与体积阴影；程序化天空不自动生成反射 Cubemap，Reflection Environment 始终单独选择；天空模式切换当帧生效，不做交叉淡化。

自定义表面 Shader 要参与空气透视，需要包含 `Builtin/atmosphere_sampling.orbinc`、声明 `uniform vec3 u_CameraPosition;`，并在着色完成后调用 `ApplyAtmosphereToSurface(rgb, v_WorldPosition, GetAtmosphereScreenUv())`；加性混合的粒子用 `SampleSurfaceAtmosphere` 只取透光率，不叠加散射。折射与热浪采样的相机颜色已经含雾，不要再调用加雾函数。宿主 Shader 多占两个片元纹理槽（`u_AtmosphereRadianceTexture`、`u_AtmosphereOpticalDepthTexture`）。

## 天空盒与环境反射

Rendering 面板的 Skybox 可选择 `Builtin/Skyboxes/soft_daylight.orbsky`，勾选 Skybox Enabled 显示背景。此资源由六张 128×128 的 sRGB PNG 组成，包含柔和天空、云层与地面反照，没有太阳圆盘；可运行 `Build/GenerateBuiltinSky.ps1` 确定性重新生成。

Reflection Environment 留空时使用同一天空盒，也可以指定另一个 `.orbsky`。Reflection Intensity 默认为 `1`，设为 `0` 关闭反射；关闭天空背景不会关闭反射。PBS 用 Roughness 控制反射模糊程度，Blinn-Phong 和普通透明材质从 Shininess 估算粗糙度。

`.orbsky` 每行格式为 `面名 "内容根相对图片Key"`，必须包含 right、left、top、bottom、front、back 六面，依次对应 +X、-X、+Y、-Y、+Z、-Z。这些是 Cubemap 的历史字段名，front 不代表引擎的 Transform 前向。图片必须是相同尺寸、格式与颜色空间的正方形；以 `#` 开头的行作为注释。

反射采样共用 `Builtin/environment_reflection.orbinc`，在场景线性 HDR 缓冲中合成，统一经过曝光与输出转换。新建项目会包含这些 Builtin 资源；已有内容根需要同步天空盒、六面 PNG、上述 include 以及对应 Shader。

## 自定义 orbmat 参数

在 `.orbmat` 文件中填写以下参数，槽名为 GLSL uniform 的完整名称；不写 `newmtl` 或材质名称：

```text
shader Builtin/Shaders/rain_glass.orbshader
float u_FlowSpeed 0.8
color u_TintColor 0.9 0.95 1.0 1.0
drawqueue Auto
```

`float` 接受一个数，`color` 接受四个 RGBA 数；参数行末不加注释，注释请单独起行。`drawqueue Auto` 继承 Shader，必要时可覆盖为 Opaque、Transparent 或 Refraction。编辑 `.orbmat` 后重新导入；Shader Pass 中显式配置的 Blend/DepthWrite 不会因材质队列覆盖自动改变。

浓雾底面过渡可延伸到参考高度以下，参考高度 0 不代表不透明地面。天空只计算本地雾层，不透过内球累计行星背面的浓雾；程序化天空对空气与浓雾重叠区联合积分。
