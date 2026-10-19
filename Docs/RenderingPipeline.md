# Orbeden 渲染管线概览

Orbeden 使用单线程 Forward Renderer，通过公共 `RenderBackend` 接口执行 OpenGL 4.3 与 Vulkan 1.3 后端。渲染组件主动注册到持久 `RenderScene`，每帧增量刷新变换和资源版本，再为各相机执行裁剪、SubMesh 展开、排序和绘制。

## 核心模块

| 模块 | 职责 |
| --- | --- |
| `RenderSystem` | 初始化、帧调度、多相机、Viewport、RenderTarget、Overlay |
| `TransformCache` | 根据 Transform 通知更新脏子树 |
| `RenderScene` | 持久维护 Camera、DirectionalLight、StaticMeshRenderer 指针注册 |
| `SceneCuller` | Layer Mask 与视锥裁剪 |
| `RenderItemSorter` | Opaque、Transparent、Refraction 队列排序 |
| `ForwardPipeline` | 阴影、天空盒、环境反射绑定、Forward Main Pass 与原生 Refraction Pass |
| `AtmosphereRenderer` | 太阳透光率、空气透视图集、程序化天空三张查找表与表面加雾参数 |
| `GpuResourceManager` | GPU 资源按需上传、缓存和释放 |
| `RenderBackend` | 资源、参数块、管线状态、Pass、裁剪与多窗口呈现接口 |
| `OpenGLRenderBackend` / `VulkanRenderBackend` | 各 API 的资源、命令与同步实现 |
| `ShaderCompiler` | 共同 GLSL 的 Vulkan 编译、跨阶段链接与布局反射 |
| `ImGuiRenderer` | 通过公共接口提交编辑器、调试 Overlay 和浮动窗口 |

## 单帧总流程

全局环境反射在 `ForwardPipeline::PrepareFrame` 选择来源，由 `GpuResourceManager::GetEnvironmentReflection` 返回非拥有型 `GpuEnvironmentReflection`（Cubemap、最大 LOD、线性强度）。Cubemap 的缓存与释放仍归资源管理器，每帧刷新采样数据；切换内容根或清理缓存会清空管线引用。

材质通过统一的 `u_EnvironmentTexture`、`u_EnvironmentMaxLod`、`u_EnvironmentIntensity` 与 `environment_reflection.orbinc` 采样，不直接查询天空盒。纹理槽放在材质纹理、阴影和两张相机纹理之后，避免覆盖已有绑定。没有有效资源或强度为零时返回零贡献。

后续局部探针可在绘制物体前按位置选择采样数据；GGX 预过滤可替换生成 Cubemap 的路径与 LOD 映射。这些接入点尚不包含探针捕获、混合、视差校正或屏幕空间反射，不提前建立空实现。

```mermaid
flowchart TD
    A["World / ECS"] --> B["ReleaseDestroyedResources\n释放已销毁对象的 GPU 缓存"]
    B --> C["Update RenderScene\n处理注册与变换通知"]
    C --> D["Prepare Camera Render Data\nRenderTarget + Viewport"]
    D --> E{"有相机?"}
    E -- 否 --> F["默认窗口清黑"]
    E -- 是 --> H["按 Camera.depth 遍历"]
    H --> I["Cull StaticMeshRenderer\n→ 展开 RenderItem → Sort\n→ 每相机 CSM → Main Pass / Refraction → 提交深度统计"]
    I --> J{"还有相机?"}
    J -- 是 --> H
    J -- 否 --> K["ImGui / Overlay"]
    F --> K
    K --> L["Present / Swap Buffers"]
```

### 1. 更新持久场景

`RenderScene` 在绑定 World 时完整收集一次已有组件，后续由 Camera、DirectionalLight 和 StaticMeshRenderer 的 Attach、Detach 与 enabled 变化维护注册：

- `Transform` setter 和父级变化通过 `ITransformListener` 通知各 `TransformCache`，不再每帧扫描所有 Ens。
- `TransformCache` 只递归更新收到通知的子树，并把本次受影响的 Ens 提供给 `RenderScene`。
- Camera 生成 View、Projection、ViewProjection 和视锥快照，并按 `depth` 升序排列。
- DirectionalLight 复制光照与阴影参数。
- `RenderScene` 直接保存 StaticMeshRenderer 指针，变换、Mesh revision 和世界 AABB 缓存在组件自身的运行时状态中。
- 全局场景不保存扁平 SubMesh 列表；只有相机剔除后的 Renderer 才临时展开 `RenderItem`。

渲染读取期间发生的组件增删会以组件指针排队到安全阶段执行，避免遍历过程中修改指针列表。

### 2. Viewport 与 RenderTarget

Camera 使用归一化 Viewport，默认 `(0, 0, 1, 1)`。RenderSystem 根据窗口或离屏目标尺寸换算为像素区域，并重新计算相机宽高比和视锥。

- `renderTargetId = 0`：最终输出到窗口。
- 有效非零 ID：最终输出到离屏 FBO。
- 无效 ID 或零尺寸 Viewport：跳过该相机。

**`renderTargetId` 描述的是最终输出目标，不是几何的绘制目标。** 每个有效相机都会由 RenderSystem 自动分配一块与 Viewport 同尺寸的 `RGBA16F` 场景缓冲，几何、光照、描边与调试线都画在它上面，再由输出 Pass 转换到最终目标（见 [颜色管线](ColorPipeline.md)）。编辑器视口的离屏目标是显示目标，保持 `RGBA8`。

普通 RenderTarget 由颜色纹理、深度纹理和 FBO 组成；创建、Resize、项目 Reload 和 Shutdown 都由 RenderSystem 统一管理。

每个有效相机还会自动持有一组与 Viewport 同尺寸的 GPU 快照：

- `CameraColorTexture`：场景缓冲在透明队列之后的线性 HDR 副本，供折射队列采样。
- `CameraDepthTexture`：在相同时间点复制的 Depth24 深度；普通透明物体默认不写深度。
- 快照始终生成，不需要相机开关，也不提供 CPU 回读。

### 3. 每相机级联阴影

ForwardPipeline 选择第一个开启阴影的方向光，由 CascadedShadowMap 在当前相机主 pass 前绘制 1..6 级稳定 CSM。两列 D32F atlas 按顺序相机复用；各相机的 SDSM 历史按 EnsId 独立保存。

- 包围球稳定投影、世界纹素网格对齐、级联过渡、4×4 tent PCF 和接收面深度修正。
- 投射物从完整场景收集，遵守 layer mask、castShadows 与 Opaque 队列，包含相机外上游遮挡物。
- shadowBias 使用世界单位，shadowNormalBias 使用本级纹素倍数。实际接收范围不超过相机 farPlane。
- shadowAdaptive=false 使用固定 CSM 分区；true 使用冻结相机深度的 64 桶对数直方图调整内部边界。
- 后端 compute 在主绘制后提交，完成状态零超时轮询；没有有效统计时仍保持完整 CSM 覆盖，不等待 GPU。
- 任意材质 alpha discard、顶点位移没有自动的 ShadowCaster Pass，当前只保证不透明几何轮廓一致性。

配置、算法、Shader ABI、验证清单见 [SDSM / CSM 详细方案](CascadedShadows.md)。

### 4. 每相机裁剪与排序

裁剪规则：

1. `renderer.drawLayer & camera.drawLayerMask` 必须非零。
2. Renderer 运行时缓存的 `worldBounds` 必须与相机视锥相交。
3. `VisibleItem` 只保存 `renderer + cameraDistance`。
4. 剔除完成后，按可见 Renderer 当前的 Mesh/SubMesh 生成相机临时 `RenderItem`。

排序规则：

- Opaque：初始近到远排序；`DrawBatchBuilder` 随后在连续可重排区间内按 Shader 基础程序、Material 聚集网格组，同材质的组按最近距离排列，组内仍近到远。
- Transparent：远到近。
- Refraction：远到近。

不透明重排仅用于单 Pass Standard、深度测试开、深度写开且混合关的项。`Individual`、多 Pass、特殊深度／混合状态项构成排序屏障，不跨区间聚合。相同 Material 的不同网格在可重排区间内保持相邻；Shader 基础程序用于分组，最终绘制仍选择各自的 Uniform／Instanced／Expanded 变体。材质排序的提交行为见下文「材质排序与 Uniform 提交」。

队列是离散语义标记，不提供自定义数值优先级。已有序列化数值保持 `Opaque=0`、`Transparent=1`，原生折射使用 `Refraction=2`。

### 5. Forward Main Pass

```mermaid
flowchart TD
    A["绑定相机场景缓冲 / Viewport\n（RGBA16F，原点 0，视口尺寸）"] --> B["按 ClearMode 局部清屏"]
    B --> C{"SolidColor 且启用背景?"}
    C -- 是 --> D["绘制程序化天空\n不可用时退回 Skybox"]
    C -- 否 --> E["Opaque Pass"]
    D --> E
    E --> F["DepthTest On\nDepthWrite On\nBlend Off"]
    F --> G["Transparent Pass"]
    G --> H["DepthTest On\nDepthWrite Off\nBlend On"]
    H --> I["Copy Camera Color + Depth\n一次 GPU Blit"]
    I --> J["Refraction Pass\nDepthWrite Off\nBlend On"]
    J --> K["选择描边合成"]
    K --> L["世界空间调试线"]
    L --> M["输出 Pass\n曝光 → AgX → sRGB 编码\n写 finalRenderTarget"]
```

相机主 Pass 的目标是引擎分配的场景缓冲，所以它的原点恒为 `0`；`viewportX/Y` 只在输出 Pass 写到最终目标时才有意义。

输出 Pass 必须排在描边与调试线之后：那些内容也写进场景缓冲，要一起转换到显示空间。它是全引擎唯一的显示编码点，细节见 [颜色管线](ColorPipeline.md)。

绘制队列由 `Shader.drawQueue` 提供默认值（缺省为 `Opaque`）。`Material.overrideDrawQueue` 默认关闭；开启后采用 `Material.drawQueue`，`Material.GetDrawQueue()` 返回最终队列。未绑定 Shader 且未覆盖时返回 `Opaque`（材质能否实际绘制仍由资源有效性决定）。Renderer 不再保存队列，同一 Renderer 的不同子网格可以分别使用不透明、透明或折射材质；阴影仅绘制最终队列为 `Opaque` 的子网格，并受 Renderer 的 `castShadows` 控制。

OrbShader 在所有 Pass/阶段之前声明一次全局队列，适用于该 Shader 的全部 Pass：

```text
--------queue Transparent

--------vert
...
--------frag
...
```

队列支持 `Opaque`、`Transparent`、`Refraction`，不区分大小写；省略声明时使用 `Opaque`，重复声明、未知值或在 Pass/阶段之后声明会导入失败。材质内部格式 `.orbmat` 可用 `drawqueue Transparent` 指定材质覆盖，`drawqueue Auto` 或省略该行表示继承 Shader。运行时也可通过材质的两个字段设置；关闭覆盖后立即恢复当前 Shader 默认值，切换 Shader 不会清除已有材质覆盖。

每个 `RenderItem` 会按 Shader 中的 Pass 声明顺序连续绘制。Pass 可独立配置 `DepthTest`、`DepthWrite`、`Blend` 和 `Cull`；`Auto` 每次从当前 Opaque、Transparent 或 Refraction 队列基线解析，不继承前一个 Pass 的状态。

`ClearMode` 含义：

- `SolidColor`：清颜色和深度，并允许绘制天空盒。
- `DepthOnly`：只清深度，保留最终目标已有的颜色。
- `None`：完全保留最终目标内容。

场景缓冲是每相机独立的，首帧没有可保留的内容，所以 `DepthOnly` 与 `None` 会先把最终目标的当前内容复制进场景缓冲。深度不做这种保留：最终目标（尤其编辑器显示目标）的深度从未被写入，复制它没有意义。清屏使用 Scissor 限制在当前 Viewport，因此分屏相机不会互相清除画面。

绘制时，`configuredPrograms` 对每个相机、每个绘制队列中的每个 Shader Program 只准备一次相机／灯光等公共参数。Material 参数在材质、GPU 绑定版本或 Pass Program 变化时设置，先写入公共 `ShaderParameters`；后端在 Draw 时提交变化值或上传参数快照。各几何变体是独立 Program，分别保存参数状态。

Refraction Pass 是引擎原生阶段。进入该阶段前，管线一次性冻结相机颜色和深度，并自动绑定：

```text
u_CameraColorTexture
u_CameraDepthTexture
u_UseCameraTextures
u_CameraNearPlane
u_CameraFarPlane
u_Time
```

新项目包含 `Builtin/camera_texture_common.orbinc`、雨水玻璃和热浪 Shader 范例。这些 Shader 已声明默认 `Refraction` 队列，材质默认继承，无需在 Renderer 上配置。

### 6. 物理大气与空气透视

`AtmosphereRenderer` 用球形大气做单次散射，产出三张查找表，供背景与表面共用：

| 查找表 | 内容 | 尺寸（Low / Balanced） |
| --- | --- | --- |
| 太阳透光率 | 二维，节点按高度平方与视线余弦平方分布 | 128×64 / 256×64 |
| 空气透视图集 | 每相机一套，距离层 × 视口节点，L 与 Tau 各一张 | 96×56 / 288×112 |
| 天空查找表 | **方向空间**（与相机朝向无关），只含沿射线的大气散射，不含太阳盘与地球底板 | 96×48 / 192×96 |

- 透视与天空模式由 `RenderSettings.skyMode` 独立选择，默认 `Cubemap`；`atmosphere.fogEnabled` 只控制物体表面的空气透视。四种组合互不隐含：Cubemap＋FogOff 完整保留原有画面，Cubemap＋FogOn 只给表面加雾，Atmosphere＋FogOff 只换背景，Atmosphere＋FogOn 天空与表面共用同一份密度、太阳输入与散射算法。
- 查找表在主 Pass 之前生成，一张全屏四边形一个 Pass，不嵌套主 Pass；材质只查询图集，不做光线步进。
- 天空表按**方向**而不是屏幕坐标存放：行是地平线相对仰角（地平线落在 v=0.5 的节点行边界上、靠地平线平方加密），列是视线与太阳方向的夹角余弦。这样从外空间看，大气顶边缘 6~12 px 宽的辉光带能落在密集行里，而不是整条挤进同一行被放大成屏幕轴对齐的台阶；表也因此与相机朝向无关。
- 查找表里不放二值判定。地球遮挡由天空着色器逐像素解析求交，覆盖率按 fwidth 估计的边界宽度做一像素过渡（和太阳盘同一套）；地平线两侧的查找表采样分开取（各自夹到 0.5∓半行），按覆盖率混合，避免表跨地平线把"掠射天空"和"近地散射"混成一个不代表任何一侧的值。地面底板的透光率：相机在大气外时沿视线查一次透射率表；在大气内时地面到相机是一段近距离，直接在射线上按 4 步积分（透射率表给不出这一段——近地平线方向两端的 τ 都很大，而需要的是它们之间不到 1 的差，半精度表表示不出来）。逐通道且随像素变化。透射率表存的是光学厚度 τ 本身，不是 exp(−τ)：掠射时 τ 能到 20 上下，存 exp(−τ) 会在半精度里让蓝通道下溢成 0。
- 表面合成在光照、自发光与 tint 之后：alpha 混合用 `rgb × T + L`，加性混合只用 `rgb × T`。折射与热浪采样的相机颜色已经含雾，不再调用表面加雾函数；雨玻璃只给本地反光乘该表面的 `T`。
- 菜单与设置入口在 Rendering 面板；世界文件保存全部大气参数，缺失属性按默认值读取。

相机高度没有停用阈值：每条射线由着色器求大气球壳的入射点、出射点与地球交点，相机在大气层外时从入射点开始积分，射线不经过大气时返回零散射与单位透光率，观察表面时再以表面距离截断。只有落到地心以下的位置才判为无效并回退天空盒。

**浓雾**是独立于球形大气的第二种介质，默认关闭，有自己的开关（`denseFog.enabled`），不受 `fogEnabled` 支配。密度模型由 `denseFog.mode` 选择：`Height` 密度只随高度变化（谷地积雾、山脊露出），`Distance` 密度与高度无关、只按相机距离加雾（没有高度参数，适合由脚本按飞行高度驱动）。它用 `visibilityMeters` 按 MOR（5% 透射阈值）描述自身消光，雾层没有底面：低于雾顶高度的所有高度都保持满密度，只在雾顶一侧衰减。高度按**平地近似**沿射线线性变化，分段剖面因此整条有初等原函数，透光率是路径积分的一次求值——不建查找表，也不做逐像素求积与球面边界求交。散射亮度取世界环境光乘 `fogScatteringScale`，环境光为零时浓雾不发光，夜间不会自行泛白。六面天空与程序化天空都被浓雾遮蔽，各算一次。

两种介质各自独立开关，合成顺序固定为**先空气透视、后浓雾**：透光率相乘，薄霾的散射亮度再乘浓雾透光率，浓雾散射按 `散射色 × (1 − 透光率)` 计入。浓雾是近场介质，叠在最外层；相机在雾层内时这与逐段积分一致，相机在层外俯视时是有偏差的顺序合成。程序化天空仍由 `skyMode` 独立控制。

限制：透视距离上限 2500 km；不实现多次散射、臭氧、月亮、星空、云层与地形体积阴影；浓雾不做水平雾区、三维噪声、降落灯光束与体积阴影；程序化天空不自动烘焙反射 Cubemap，反射环境始终独立配置；夜景单次散射偏暗，环境照明与反射不由此功能补偿。算法、参数与验收见 [大气散射设计](AtmosphereScatteringDesign.md)。

### 7. OrbShader 多 Pass

旧的单组 `vert/frag` 文件会自动成为名为 `Default` 的 Pass。多 Pass 文件使用以下格式：

```glsl
--------pass Outline
depthTest auto
depthWrite off
blend auto
cull front
--------vert
// vertex GLSL
--------frag
// fragment GLSL
```

Pass 名称区分大小写且必须唯一；分段和状态关键字不区分大小写。每个 Pass 必须各有一个 `vert` 和 `frag`，状态只能写在 Pass 头与第一个 Shader Stage 之间。支持的值为：

- `depthTest`、`depthWrite`、`blend`：`auto`、`on`、`off`。
- `cull`：`auto`、`none`、`front`、`back`。

内置表面 Shader `blinn_phong`、`pbs_metallic`、`transparent`、`refraction` 在 `Default` Pass 中显式使用 `Cull Back`，Uniform／Instanced／Expanded 变体沿用同一 Pass 状态。正面为 引擎约定的 CCW 绕序，背面不进入光栅着色；该设置不会省去已提交顶点的变换工作。负缩放改变绕序时沿用现有处理，不自动翻转正面定义。

天空盒、大气、UI、粒子、雨玻璃、热尾流以及阴影 Pass 保持原剔除状态。自定义 Shader 省略 Cull 或使用 `Cull Auto` 时仍按 None 处理；需要双面显示的表面可使用显式声明 `Cull None` 的自定义 Shader。现有项目需同步 Builtin 模板并重新导入上述四个 Shader，材质引用与模型资源无需修改。

## GPU 资源缓存

Mesh、Texture、Skybox、Shader、Material 在第一次使用时上传到 GPU。Shader 的所有 Pass 会作为一个整体编译和缓存，任一 Pass 编译失败都会使整个 Shader 无效。

缓存有效时直接返回；无缓存或版本变化时创建/刷新 GPU 对象。每帧开头的 `ReleaseDestroyedResources` 释放已销毁 CPU 对象对应的 GPU 对象和缓存项。

Backend 还会缓存 Program、VAO、Texture Slot、Depth/Blend 状态和 Uniform Location，过滤重复绑定与状态设置。Uniform 的值缓存与材质排序共同构成下面的提交策略。

## 材质排序与 Uniform 提交

公共层按 Program 保存 `ShaderParameters`，按绘制提交 `PipelineState`。Shader／Material 排序继续聚集兼容绘制；每个相机、队列与 Program 分别准备公共参数，连续相同材质且 GPU 绑定版本未变时复用材质参数与纹理绑定。对象矩阵、实例 tint／UV、接收阴影及加性混合标记仍随批次更新。调用方写入的是 CPU 数据，不执行原生图形 API。

参数写入先比较 CPU 值，只把变化项加入待提交列表；重复写入同一项只排队一次。后端消费列表后清空标记，不逐绘制遍历全部参数。数组名和首元素名（如 `u_Colors` 与 `u_Colors[0]`）指向同一个缓存项。

| 后端 | 参数与状态提交 |
| --- | --- |
| OpenGL | 只消费变化列表，再按实际 uniform 位置和值过滤；缓存深度、混合、剔除、线框及深度偏移状态；顶点输入的原生 VAO 按窗口上下文分别创建 |
| Vulkan | glslang 把独立参数归入 set 0 / binding 0 的动态 Uniform Buffer；反射偏移与 CPU 参数项只关联一次，变化项增量更新 std140 布局。参数变化后上传帧内不可变快照，以动态偏移选择当前绘制数据；同一帧的相同缓冲页、采样器、图像和存储缓冲组合复用 Descriptor Set。采样器绑定从 1 起自动分配 |
| 固定功能状态 | 深度、混合、剔除、线框、图元类型与深度偏移共用 `PipelineState`；Vulkan 按 Shader、输入布局、附件格式和状态缓存管线 |

Vulkan 使用三帧资源池、帧 fence、逐交换链图像的呈现信号量，以及 Synchronization2 图像／缓冲依赖。流式数据保留最新 CPU 内容，跨帧按需复制到当前帧的上传区间；同帧后续写入不会覆盖已录制绘制读取的数据。删除对象的原生资源延迟到引用它的 GPU 帧完成后释放。

连续绘制复用最近的图形管线与资源组合；纹理绑定、采样槽参数、图像访问状态或缓冲页变化时重新检查资源依赖。原生命令只更新变化的管线、Descriptor Set／动态偏移、顶点／索引缓冲与深度偏移；每次开始新的命令缓冲时清空命令绑定缓存。GPU 完成等待只提交已记录的新命令；空检查点复用已完成状态，有未完成帧时仍等待队列，不重复执行设备等待。Profiler 中仍保留全部阶段等待作用域。

相机的天空／不透明／透明绘制结束后，再复制颜色与深度；存在折射批次时以 Load 语义开始折射绘制。后端也处理 UI 中间目标、采样转换、深度读取与统计的资源访问，不依赖 OpenGL 的隐式 Pass 边界。ImGui 复用 CPU 暂存数组，共用投影与绘制绑定按提交设置，在用户回调后恢复。

### 后端选择与坐标契约

Editor 在 **Editor Settings → Rendering Backend** 选择 OpenGL 或 Vulkan，按 Apply 在当前会话切换，不需要启动参数或重启。选择写入项目文件的 `<EditorGuiConfig graphicsBackend="OpenGL|Vulkan">`，项目打开后自动应用；旧项目默认 OpenGL。Play 期间禁用切换。切换发生在帧边界：先验证新设备，再释放并重建 GUI、浮动窗口及 GPU 缓存；World、未保存编辑、选中状态与面板布局保留。离屏目标保持逻辑 ID，设备上的图像内容重新生成。目标设备初始化失败时保留原后端，设置面板显示错误。Player 暂时通过 `--graphics-api vulkan` 或 `--graphics-api opengl` 选择，默认 OpenGL。Vulkan 要求 1.3、Dynamic Rendering、Synchronization2，以及当前编辑器线框所需的非实心填充能力；初始化不支持时报告失败。

世界空间、CPU 投影、视锥、阴影矩阵及 Shader 传递的逻辑裁剪坐标继续使用右手系与 `[-1,1]` 深度。Vulkan 编译入口在最终位置上做 `z = (z + w) / 2`，显示窗口使用负高度 viewport；离屏纹理保留原来的 UV 约定。默认片元坐标与 Y 导数由生成的共享入口统一适配。现有深度还原、折射 UV 和阴影采样公式继续使用逻辑约定。

Vulkan 的深度附件使用 D32F；OpenGL 保留普通 Depth24 与阴影 D32F。颜色目标继续使用线性 HDR RGBA16F，窗口使用 UNORM SDR 格式，由输出 Pass 做唯一一次显示编码。

ImGui 的 `ImTextureID` 保存引擎纹理 ID，由 `ImGuiRenderer` 经公共接口采样。图标上传、线框、调试线和浮动窗口不再直接调用 GL；附加窗口通过 Surface 共享设备资源。

## 当前边界

- 提供 OpenGL 4.3 与 Vulkan 1.3；当前窗口平台为 GLFW。
- 实际只使用一个主方向光；各相机顺序重绘并复用级联 atlas。
- CSM 每级默认 2048²、4 级，可选择固定分区或异步 SDSM；VSM、云阴影和屏幕空间接触阴影尚未实现。
- 相机裁剪仍为线性扫描，没有 BVH 或 GPU Culling。
- 主画面的绘制路径按渲染器的 `drawStrategy` 与批次可合并性选择：`Auto` 依次尝试持久静态批、实例批、动态展开批，都不成立才逐对象绘制；`Individual` 始终单绘制并作为排序屏障；`GpuInstancing` 与 `DynamicBatching` 各自限定一种合批方式，条件不足时退回单绘制。实例批要求同 Mesh、子网格区间与材质；动态展开批只要求同材质与同渲染状态，用 `DrawIndexedInstanced` 与 `DrawIndexed` 分别提交（持久静态批直接绑定缓存组的顶点输入）。Indirect Draw 尚未实现。
- 阴影中的 `Auto` 普通网格先复用有效持久静态批；其余项只尝试 GPU 实例批，条件不足时直接绘制原 GPU 网格，不逐级联进行 CPU 动态几何展开。显式 `DynamicBatching`、粒子与显式实例提交保持原路径；不修改 Renderer 保存的策略。`Render/Shadows` 记录阴影入口，`Cascade`、`BuildItems`、`BuildBatches`、`DrawBatches` 记录各级联；其中 `DrawBatches` 包含 Pass 建立、状态设置、绘制提交与结束，不是 GPU 时间查询。
- 每次相机渲染只收集一次普通网格和显式实例的阴影候选，再按级联视锥筛选；同一 Renderer 的子网格共享一次包围盒判断。持久静态批和粒子保留逐级裁剪路径，投射物分区包围盒继续使用原筛选规则并复用存储容量。`Render/Shadows/CollectItems` 记录共享收集阶段，`BuildItems` 记录级联筛选和静态批／粒子追加；不跨相机或跨帧复用候选内容。
- `StaticMeshRenderer` 只是组件类名，**不代表它所属的 Ens 已 static**：是否进入持久静态批由 Ens 的 `static` 约束和渲染器的 `drawStrategy` 共同决定。
- RenderScene 在同一线程增量更新并立即消费。
- 所有 Refraction 物体共享同一份冻结快照，因此不会互相折射。
- 普通透明物体全部先于 Refraction 绘制；前景透明物体可能被后绘制的折射表面覆盖。

普通 Pass 的 CPU 采样在 `Render/BuildBatches` 下区分绘制项收集、排序与批次生成；排序细分距离排序、按键分组、组构造与组重排。绘制按 Uniform／Instanced／Expanded／TrailInstanced 分别记录；展开路径细分旧块清理、GPU 网格查询、引用顶点统计、索引校验、顶点转换、索引重排、块提交、状态绑定与 Draw 提交。保留既有 `Render/ExpandParticles` 与 `Render/UploadInstances` 名称用于前后比较，前者也包含普通网格动态展开。采样粒度为批次或网格阶段，不逐顶点采样；仍是 CPU 作用域时间，不是 GPU 时间查询。每帧事件容量为 4096，帧历史仍为 1024，原生事件数组约 160 MiB。

展开几何的引用计数与局部索引按 Mesh 内容版本缓存，首次构建同时完成索引校验和首次引用顺序映射；网格修改后重建，销毁后清理，资源失效时清空。每批仍重新计算世界空间顶点；位置、法线、切线公式和 32 位索引格式不变。索引输出直接复制缓存映射并加顶点基址，已消费的展开块回收容量供后续批次复用。新增 `GetTopology`／`BuildTopology`／`ReserveBuffers` 采样；`ValidateIndices` 包含拓扑缓存读取或重建，首次录制需区分冷缓存和后续帧。

CPU 展开按绘制项准备共享变换系数，`PrepareTransform` 记录准备耗时；`TransformVertices` 记录顶点计算与写入。x64／启用 SSE2 的 x86 使用 SSE2 并行计算位置、法线、切线与 UV 分量，其他目标使用标量路径；不要求 AVX，不修改公共向量布局或 GPU 顶点格式。合批判定优先保留后续可实例化的连续组，实例能力不足时继续尝试策略允许的动态批。普通 Renderer 的动态批只接收单项不超过 128 个引用顶点、384 个索引的小网格绘制项，连续兼容项至少 8 个才组批，整批最多 1024 个引用顶点、3072 个索引；不足门槛或超限时复用原 GPU 网格绘制。预算按子网格绘制项计算，先拒绝索引超限项，再读取引用顶点统计；Auto 与显式 DynamicBatching 共用门槛，GPU Instancing、持久静态缓存和粒子／显式实例路径不变。预算数值仍需在实际场景测量。

## 主要源码

- 总调度：`OrbedenCore/Src/Rendering/RenderSystem.cpp`
- 持久场景：`OrbedenCore/Src/Rendering/RenderScene.cpp`
- 阴影与主 Pass：`OrbedenCore/Src/Rendering/ForwardPipeline.cpp`
- GPU 缓存：`OrbedenCore/Src/Rendering/GpuResourceManager.cpp`
- 渲染接口与后端：`OrbedenCore/Src/Rendering/Backend/`
- GLSL / SPIR-V 编译：`OrbedenCore/Src/Rendering/ShaderCompiler.cpp`
- 基础验证：`Tools/OrbedenRenderSmoke/README.md`
