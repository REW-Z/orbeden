# Orbeden 着色器开发手册

本文面向游戏开发者和 Technical Artist（TA），说明如何编写材质 Shader、接入阴影与雾、使用纹理和几何输入，以及排查常见显示问题。当前材质 Shader 使用 GLSL，下面的示例使用 `#version 430 core`。

本手册的公共参数和宏用于场景网格的材质管线。RetainedGUI 的 `ui_surface.orbshader` 由独立 UI 渲染器提交，不会自动获得这些阴影与雾绑定。

## 1. 接入阴影与雾

在 `.orbshader` 的片元阶段、`#version` 之后和 `main()` 之前，加入：

```glsl
#include "Builtin/surface_effects.orbinc"
```

然后在光照计算中调用：

```glsl
ORBEDEN_APPLY_SHADOW(direct, worldPosition, worldNormal);
```

在最终 RGB 合成完成、写入输出之前调用：

```glsl
ORBEDEN_APPLY_FOG(color, worldPosition);
```

两个宏都直接修改第一个参数，无需接收返回值。位置、法线和颜色变量叫什么由你决定，宏不依赖 `v_WorldPosition` 等固定名字。引擎会自动绑定所需纹理、矩阵、光源和雾参数，不需要编写 C# 或 C++ 绑定代码。

| 宏 | 参数 | 使用位置 |
| --- | --- | --- |
| `ORBEDEN_APPLY_SHADOW(directLighting, worldPosition, worldNormal)` | 主方向光的线性 RGB 直接光、世界位置、世界法线 | 漫反射与高光合成之后，与环境光合成之前 |
| `ORBEDEN_APPLY_FOG(rgb, worldPosition)` | 表面的线性 RGB 颜色、世界位置 | 光照、自发光、反射与 tint 合成之后，输出之前 |

第一个参数应为可修改的 `vec3` 变量，也可以是 `vec4` 变量的 `.rgb`。其余参数为 `vec3`；传入归一化的世界法线。参数使用普通变量，不传带自增、赋值等副作用的表达式。宏按语句使用，末尾写分号，不作为返回值表达式使用。雾宏不修改 Alpha。

`surface_effects.orbinc` 已声明片元阶段的 `u_CameraPosition` 和 `u_LightDirection`，不要再次声明它们。需要光源颜色、强度或环境光时，另外声明本手册第 6 节列出的 uniform。

阴影只衰减主方向光的直接光。把环境光、自发光或环境反射一起传进去会让它们在阴影处错误变暗。雾宏依次合成空气透视与浓雾，分别受 Rendering 面板的 **Atmosphere Fog** 和 **Dense Fog** 开关控制，二者与天空背景选择独立。

## 2. 创建一个完整材质

把自己的文件放在 `Content/` 内，例如：

```text
Content/
├─ Shaders/CustomLit.orbshader
├─ Materials/CustomLit.orbmat
└─ Textures/Albedo.png
```

自定义文件不要放在 `Content/Builtin/`，项目更新和 Reset Builtin 会覆盖、删除其中的自定义内容。下列 Shader 不要求纹理存在；未指定纹理时只使用材质颜色。

新建 `Content/Shaders/CustomLit.orbshader`，写入完整内容：

```glsl
--------queue Opaque
--------geometry Standard

--------vert
#version 430 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

uniform mat4 u_ViewProjection;
#include "Builtin/geometry_input.orbinc"

out vec3 v_WorldPosition;
out vec3 v_Normal;
out vec2 v_TexCoord;
out vec4 v_Tint;

void main()
{
    vec4 worldPosition = OrbedenGetModel() * vec4(a_Position, 1.0);
    v_WorldPosition = worldPosition.xyz;
    v_Normal = OrbedenGetNormalMatrix() * a_Normal;
    v_TexCoord = OrbedenGetUv(a_TexCoord);
    v_Tint = OrbedenGetTint();
    gl_Position = u_ViewProjection * worldPosition;
}

--------frag
#version 430 core

in vec3 v_WorldPosition;
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec4 v_Tint;

uniform vec4 u_DiffuseColor;
uniform vec4 u_EmissionColor;
uniform vec4 u_AmbientColor;
uniform vec4 u_LightColor;
uniform float u_LightIntensity;
uniform sampler2D u_DiffuseTexture;
uniform bool u_HasDiffuseTexture;

#include "Builtin/surface_effects.orbinc"

out vec4 FragColor;

void main()
{
    vec3 albedo = max(u_DiffuseColor.rgb, vec3(0.0));
    if (u_HasDiffuseTexture)
        albedo *= texture(u_DiffuseTexture, v_TexCoord).rgb;

    vec3 normal = normalize(v_Normal);
    vec3 lightDir = normalize(-u_LightDirection);
    float nDotL = max(dot(normal, lightDir), 0.0);
    vec3 direct = albedo * nDotL * u_LightColor.rgb
        * max(u_LightIntensity, 0.0);
    ORBEDEN_APPLY_SHADOW(direct, v_WorldPosition, normal);

    vec3 ambient = albedo * u_AmbientColor.rgb;
    vec3 color = direct + ambient + max(u_EmissionColor.rgb, vec3(0.0));
    color *= v_Tint.rgb;
    ORBEDEN_APPLY_FOG(color, v_WorldPosition);
    FragColor = vec4(color, 1.0);
}
```

这个示例使用简单的漫反射。你可以替换 `direct` 的计算公式，保留阴影与雾的调用位置。需要 PBS 或 Blinn-Phong 光照时，参考项目中的 `Builtin/Shaders/pbs_metallic.orbshader` 或 `Builtin/Shaders/blinn_phong.orbshader`。

新建 `Content/Materials/CustomLit.orbmat`：

```text
shader Shaders/CustomLit.orbshader
drawqueue Auto
color u_DiffuseColor 0.8 0.8 0.8 1.0
color u_EmissionColor 0.0 0.0 0.0 1.0
```

资源路径以 `Content/` 为根，不写 `Content/` 前缀。一个 `.orbmat` 文件对应一个材质，无需 `newmtl` 或材质名称行。保存并重新导入 Shader 和材质，把材质放入 `StaticMeshRenderer.materials` 对应子网格槽位。

检查 Renderer 的 `receiveShadows` 和光源的 `castShadows`；若物体还要投射阴影，打开 Renderer 的 `castShadows`。普通不透明网格的阴影深度绘制由引擎统一完成。

## 3. Shader 文件与 include

`.orbshader` 将顶点、片元源码放在一个文件中：

| 声明 | 含义 |
| --- | --- |
| `--------queue Opaque` | Shader 默认绘制队列，可用 `Opaque`、`Transparent`、`Refraction` |
| `--------geometry Standard` | 普通网格的几何输入契约；粒子使用 `Particle` |
| `--------vert` | 顶点阶段开始 |
| `--------frag` | 片元阶段开始 |

每个阶段包含自己的 `#version 430 core`，必须位于该阶段第一条有效指令。当前材质文件支持顶点与片元阶段，不支持在这里添加几何、曲面细分或计算阶段。

`#include "路径"` 是资源导入器支持的语法。导入器优先在内容根寻找目标，未命中时相对当前文件目录寻找；推荐明确使用 `Builtin/...` 或 `Shaders/...`。include 行不要带行末注释，使用双引号，不使用尖括号或宏形式的文件名。

公共片段使用 `.orbinc`，其中不要写 `#version`。没有分段头的片段会进入当前阶段；有 `--------vert`／`--------frag` 时只展开当前阶段和分段前的共享内容。`surface_effects.orbinc` 只提供片元接口，应放在片元阶段使用。

支持 GLSL 的 `#define`、`#if`、`#ifdef` 等宏，但当前没有自动生成材质关键字变体的 `multi_compile`／`shader_feature` 机制。include 在 GLSL 条件编译之前展开：即使写在 `#if` 内，被引用的文件也必须存在，include 链不能成环。新增材质参数时直接写完整的 uniform 声明，材质槽反射不展开宏或判断条件分支。

## 4. 顶点输入、空间与合批

常用网格属性如下，不需要的属性可以不声明：

| location | 类型 | 数据 |
| --- | --- | --- |
| 0 | `vec3` | 顶点位置 |
| 1 | `vec3` | 法线 |
| 2 | `vec2` | UV0 |
| 3 | `vec3` | 切线 |

location 4 及实例相关属性由几何输入接口使用，不要自行占用。包含 `Builtin/geometry_input.orbinc` 后使用：

| 函数 | 用途 |
| --- | --- |
| `OrbedenGetModel()` | 取得当前几何路径的模型矩阵 |
| `OrbedenGetNormalMatrix()` | 取得法线矩阵，处理非均匀缩放 |
| `OrbedenGetUv(uv)` | 应用实例的图集 UV 变换 |
| `OrbedenGetTint()` | 取得实例颜色，线性空间，默认白色 |

不要直接把 `u_Model` 作为所有绘制路径的输入：实例路径使用实例属性，展开几何的顶点已经位于世界空间。引擎注入的 `ORBEDEN_GEOMETRY_MODE` 为 0（单绘制）、1（实例）、2（展开几何）或 3（拖尾实例）；普通网格优先使用上面的函数，不手动定义这个宏。

Orbeden 使用右手坐标系，`+Y` 向上，局部前方为 `-Z`。光源方向 `u_LightDirection` 表示光传播的方向；从表面指向光源的向量用 `normalize(-u_LightDirection)`。

做模型空间顶点动画时，在文件头声明 `--------expandedGeometry off`，关闭无法保留模型空间输入的展开合批。仍需正确支持单绘制与实例路径。这只影响主绘制路径，**不会自动让统一阴影深度 Shader 执行你的顶点动画**。

## 5. 材质参数与纹理

导入器从 GLSL uniform 声明中生成材质槽，公共引擎参数会被排除：

| 声明 | 材质槽 | `.orbmat` 写法 |
| --- | --- | --- |
| `uniform vec4 u_TintColor;` | 颜色 | `color u_TintColor 0.9 0.8 0.7 1.0` |
| `uniform float u_EffectStrength;` | 浮点 | `float u_EffectStrength 0.5` |
| `uniform sampler2D u_DiffuseTexture;` | 纹理 | `texture u_DiffuseTexture Textures/Albedo.png//Texture2D/Main` |

通用材质槽当前不支持独立的 `vec2`、`vec3`、`int`、`bool`、矩阵或自定义 Cubemap 参数。需要多个数值时使用多个 float，或用 vec4 颜色槽；颜色槽的 RGB 会按颜色语义转换，不适合装载未经转换的数据向量。引擎内置 Cubemap 如环境反射由管线绑定。

`.orbmat` 参数名必须与 uniform 完全一致，每个参数占一行，行末不加注释。材质参数也可以在 Inspector 编辑；修改源码后重新导入，槽位增删会同步到材质。Shader 各阶段与各 Pass 中同名材质槽的类型应一致。

纹理槽 `u_DiffuseTexture` 对应的 `u_HasDiffuseTexture` 会由引擎自动设置。需要判断纹理是否存在时，自己在 GLSL 声明这个 bool。其他 `u_名称Texture` 槽遵循 `u_Has名称Texture` 的命名规则。

基础色和自发光贴图使用 sRGB；法线、粗糙度、遮罩等数据贴图使用 Linear。检查纹理 Import Settings，不要在 Shader 中补一次 gamma 转换。普通自定义 float 默认通常为 0，需要非零起始值时在材质中明确设置。

## 6. 引擎自动提供的参数

从版本 79 起，渲染时按每个 Pass、每个几何变体链接后的实际输入筛选参数与纹理。源码中声明但未使用、被编译器移除的输入不提交、不占用绑定槽；同一 Shader 的材质共享绑定布局，修改材质值不重新分析布局。Shader 重新导入时刷新布局。Inspector 仍按源码声明展示材质参数，便于编辑和保留材质设置；同一个 Shader 重导入后的槽位增删和类型变化会刷新面板列表。

按需在 Shader 中声明，名字与类型必须保持一致。以下参数由渲染管线提供，不要在材质里给它们赋值：

| uniform | 类型 | 语义 |
| --- | --- | --- |
| `u_ViewProjection` | `mat4` | 当前相机的世界到裁剪空间矩阵 |
| `u_CameraPosition` | `vec3` | 当前相机世界位置，片元公共 include 已声明 |
| `u_CameraNearPlane`、`u_CameraFarPlane` | `float` | 相机近、远裁剪距离 |
| `u_Time` | `float` | 当前相机的累计时间，秒 |
| `u_LightDirection` | `vec3` | 主方向光传播方向，片元公共 include 已声明 |
| `u_LightColor` | `vec4` | 主光线性颜色 |
| `u_LightIntensity` | `float` | 主光美术强度，没有主光时为 0 |
| `u_AmbientColor` | `vec4` | 线性环境光颜色，已包含环境光强度 |

阴影、雾和环境反射的参数及纹理由相应 include 声明和管线绑定，无需手动分配纹理槽或给 sampler 设置 `layout(binding=...)`。

所有光照和雾合成都使用线性 HDR RGB，可以大于 1。材质颜色和颜色贴图进入 Shader 时已经转为线性值。输出线性颜色即可，曝光、色调映射和 sRGB 编码由引擎最后的输出阶段统一完成。不要在材质 Shader 中再做显示编码，也不要在加雾前把 HDR 颜色截断到 0..1。

## 7. 阴影与雾的使用边界

阴影宏复用现有级联阴影查询，内部已包含 `shadowStrength`。不要再乘一次强度或重复应用阴影。它使用片元导数，应在片元正常执行路径上调用，不放进由像素决定的条件分支。存在 alpha 裁剪时，优先在 `discard` 之前完成阴影查询。

普通不透明几何可投射阴影，透明和折射队列当前不作为投射物。主 Shader 中的 `discard`、顶点位移、风动画等不会自动同步到统一阴影深度 Shader；例如剪裁叶片可能投射原始四边形阴影。宏负责接收阴影，不新增自定义 ShadowCaster Pass 功能。

雾宏用于按当前相机观察的表面颜色，自动根据世界位置计算距离。它不是体积光照接口，不提供光束或体积阴影。对于屏幕 UV 有特殊含义、异于当前相机表面着色的效果，应使用底层函数并自行确认输入。

| 材质或效果 | 处理方式 |
| --- | --- |
| 不透明表面 | 正常调用阴影和雾宏 |
| 普通 Alpha 混合表面 | 对未预乘 Alpha 的 RGB 加雾，单独输出 Alpha |
| 无光照材质 | 可以只调用雾宏；没有直接光就无需调用阴影宏 |
| 加性粒子 | 只乘雾透光率，不叠加散射；参考 `Builtin/Shaders/particle_unlit.orbshader` |
| 折射、玻璃、热浪 | 相机颜色纹理已包含背景雾，不能对采样背景重复套雾宏；参考 `rain_glass.orbshader`／`heat_wake.orbshader` |

需要更细控制时，公共 include 仍提供底层函数：

- `SampleShadow(position, normal)`：返回 0..1 遮挡量，已包含阴影强度。
- `ApplyAtmosphereToSurface(rgb, position, screenUv)`：只加空气透视。
- `ApplyDenseFogToSurface(rgb, position)`：只加浓雾。
- `SampleSurfaceAerial(position, screenUv)`、`SampleSurfaceDenseFog(position)`：分别取得 `radiance` 和 `transmittance`，用于特效自行合成。

混合顺序为先空气透视、后浓雾。同一表面只应用一次各自的效果。单独使用旧查询 include 的 Shader 仍可工作：`shadow_common.orbinc` 需要宿主声明 `u_LightDirection`，`atmosphere_sampling.orbinc` 需要宿主声明 `u_CameraPosition`。

从旧写法迁移到宏时，删除宿主片元的这两个 uniform 声明，将两个查询 include 换成 `surface_effects.orbinc`，以宏替换原来的采样／加雾代码。不要在保留原有衰减或加雾的基础上再追加宏。

## 8. 队列、透明和多个 Pass

`Opaque` 用于普通不透明表面；`Transparent` 用于常规 Alpha 混合；`Refraction` 可以读取透明队列结束后的相机颜色与深度快照。材质的 `drawqueue Auto` 继承 Shader 队列，可以显式覆盖为其他队列。

默认情况下，不透明表面写深度、不混合；透明与折射表面开启混合、不写深度。普通网格默认不剔除正反面，需要时显式设置。多个 Pass 使用 `--------pass 名称`，状态行放在该 Pass 的第一段 GLSL 之前，例如：

```text
--------queue Transparent
--------pass Surface
DepthTest On
DepthWrite Off
Blend On
Cull Back
geometry Standard
```

这段是 Pass 头部，后面仍需完整的 `--------vert` 与 `--------frag` 源码。状态值 `DepthTest`、`DepthWrite`、`Blend` 可用 `Auto`、`On`、`Off`；`Cull` 可用 `Auto`、`None`、`Front`、`Back`。一个文件不能混用显式命名 Pass 和隐式默认 Pass。每个 Pass 必须有唯一名称，以及自己的顶点与片元阶段。

材质队列覆盖不会取消 Shader 显式设置的深度／混合状态。`Blend On` 只开启混合，混合方程由管线和渲染器类型决定，不是在材质 Shader 里选择加性／预乘模式的开关。增加 Pass 会增加绘制与片元工作量，阴影宏、雾宏本身不新增 Pass。

## 9. 性能与排查

这两个宏只是现有函数和颜色合成的封装，相比同样的手动调用不新增查询、纹理采样或渲染 Pass。原本不查询阴影、不加雾的材质接入效果后仍然需要支付相应开销；宏不会消除效果本身的成本。只有需要阴影的直接光部分才查询一次，避免在漫反射和高光上各查一次。每个表面只加雾一次。

按需绑定主要减少 CPU 参数提交与纹理状态操作，不改变阴影贴图或大气图集的生成工作，也不减少已使用 Shader 的采样。大量简单材质或多个 Pass 使用不同输入时可能受益，实际帧率收益需要在目标场景测量。运行时关闭效果不等于从编译后的程序删除输入：保留相关动态分支的 Shader 仍需有效的 sampler 绑定。

完整空气透视查询使用两个片元纹理槽，阴影查询使用一个；浓雾不使用额外纹理槽。没有使用对应查询的程序不会绑定这些纹理。材质与公共纹理的槽位按程序实际需要连续分配，不要把公共纹理暴露成材质纹理，也不要依靠固定槽位编号。实际使用的纹理数量超过后端支持的槽位时，管线会报告数量并拒绝绘制该程序。

| 现象 | 优先检查 |
| --- | --- |
| 找不到公共 include | 游戏项目是否同步了新版 `Content/Builtin/`，路径是否以内容根为基准 |
| uniform 重复定义 | 宿主是否还声明了公共 include 提供的相机位置或主光方向 |
| 阴影不起效 | 宏是否作用于最终使用的直接光，Renderer 的 receiveShadows、光源 castShadows、阴影距离和层掩码 |
| 整个物体在阴影下变黑 | 是否把环境光、自发光、反射一起传入了阴影宏，或重复应用阴影 |
| 加雾不起效 | 对应雾开关、世界尺度、相机距离，以及后续代码是否覆盖了加雾后的颜色 |
| 雾特别浓、颜色发灰 | 是否重复加雾，对已含雾的折射背景再次加雾，或重复做 sRGB 编码 |
| 缩放后光照或阴影错误 | 是否使用世界位置，法线是否经法线矩阵变换并归一化 |
| 网格出现条纹自阴影 | 先检查真实三角形表面是否有褶皱、螺纹或齿状结构，再检查 bias 和采样 |
| 动画物体与投影不一致 | 主 Shader 顶点动画是否未同步到统一阴影深度绘制 |
| 普通绘制正常，合批后错误 | 是否绕过几何输入接口直接使用 u_Model，是否依赖模型空间却仍开启展开几何 |

建议在简单、平滑的地面和球体上验证：切换阴影、空气透视和浓雾，确认各自独立生效；关闭 receiveShadows 后，环境光与雾应仍保留。再检查非均匀缩放、实例绘制和合批。Shader 编译通过只验证语法与链接，画面和 GPU 耗时需要在目标场景、目标设备上确认。

## 10. 更新与发布

公共宏随引擎版本 78 提供。已有项目通过 **Update Project** 同步 Builtin 和 SDK 文档，或手动同步 `surface_effects.orbinc`、`shadow_common.orbinc`、`atmosphere_sampling.orbinc` 及它们引用的公共片段，再重新导入依赖它们的 Shader。Builtin 重置会覆盖其中的自定义内容，自己的 Shader 应放在其他目录。

这份手册随 SDK 发布到游戏项目 `Lib/Docs/ShaderDevelopment.md`；该目录由引擎刷新，自己的笔记放在 `Content/` 内。发布游戏前重新 **Build Player**，把新导入的 Shader 与材质打包进发布资源。


## OpenGL 与 Vulkan

同一份 `.orbshader` / `.orbinc` 同时供 OpenGL 4.3 和 Vulkan 1.3 使用，继续写普通 GLSL 参数。Vulkan 编译器负责跨阶段链接、分配 location / sampler binding，把数值参数转换为参数块并反射偏移；不要给这些参数手写 Vulkan descriptor set。现有材质槽名称、Pass 状态与几何变体保持一致。

数值提交接口支持 mat4、vec3、vec4、float、int / bool 及其按元素命名的数组。引擎公共接口未提供任意 UBO、SSBO 或采样器数组绑定；自定义资源布局不属于当前 Shader 契约，编译时会报告不支持的布局。深度统计的 SSBO 由后端私有实现管理。

世界与逻辑裁剪空间不随后端改变；相机、阴影、屏幕 UV 与深度还原继续按本手册的逻辑约定编写。Vulkan 编译入口适配最终位置、窗口片元坐标与 Y 导数，不在材质 Shader 中重复翻转。显示编码继续只由输出 Pass 完成。
