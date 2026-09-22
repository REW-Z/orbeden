# SDSM / CSM 阴影设计与实施清单

## 范围与依据

本次替换单张太阳光阴影为每相机独立的稳定 CSM，提供基于实际深度采样分布的异步 SDSM 模式。维持现有 Forward 主着色、透明与折射顺序；不实现灯光分簇、VSM、云阴影、屏幕空间阴影或大世界坐标体系重构。

已核实的基线：

- `OrbedenCore/Src/Rendering/ForwardPipeline.h:35`：固定 1024² 阴影。
- `OrbedenCore/Src/Rendering/ForwardPipeline.cpp:208`：围绕第一相机建立单个正交投影。
- `OrbedenCore/Src/Rendering/ForwardPipeline.cpp:615`：投射物从持久场景收集，不限定相机可见集合。
- `OrbedenCore/Src/Rendering/ForwardPipeline.cpp:316`：主绘制后复制相机颜色和深度，可供异步深度分析。
- `OrbedenCore/Src/Platform/GlfwWindow.cpp:204`：请求 OpenGL 4.3，可使用 compute、SSBO 与 fence。
- `OrbedenCore/Src/Rendering/Backend/OpenGLRenderBackend.cpp:328`：当前深度格式为 D24。
- `OrbedenCore/Src/Runtime/Object/Shader.cpp:101`：内置 uniform 必须从材质反射中排除。

文中米制参数要求内容以一个世界单位表示一米。最终覆盖不超过相机 farPlane，阴影系统不修改相机裁剪面。已卸载几何无法投影；大坐标单精度误差不能由阴影系统恢复。

## 配置与唯一阴影入口

DirectionLight 继续通过 `DirectionalLight` 组件配置；`RenderDirectionalLight` 快照复制全部参数：

| 字段 | 默认值 | 约束与语义 |
| --- | --- | --- |
| shadowDistance | 20000 | 接收范围；运行时限制到相机 farPlane |
| shadowBias | 0.0005 | 世界单位的深度偏移，替代旧归一化偏移 |
| shadowStrength | 0.45 | 保留当前直接光艺术强度；限制到 0..1 |
| shadowCascadeCount | 4 | 1..6 |
| shadowMapResolution | 2048 | 256..4096，向下取二次幂 |
| shadowSplitLambda | 1 | 0..1，线性与对数分区混合，默认纯对数 |
| shadowAdaptive | true | false 为稳定 CSM；true 为异步直方图驱动 SDSM |
| shadowNormalBias | 0.25 | 几何法线偏移的世界纹素倍数 |
| shadowBlendRatio | 0.1 | 每段末尾过渡比例，限制到 0..0.3 |
| shadowDebugView | 0 | 0 关闭；1 级联颜色；2 世界纹素尺寸 |

不增加虚拟阴影占位分支。`CascadedShadowMap` 持有当前算法资源；ForwardPipeline 只负责在主 pass 前调用 Render、绑定查询数据、主 pass 后提交深度统计。未来其他主阴影实现可替换此边界。

## 数据与文件

- `Rendering/ShadowCascadeBuilder.h/.cpp`：纯 CPU 数学主类 `ShadowCascadeBuilder`；关联小类型 `ShadowCascadeSettings`、`ShadowCascade`。不访问 GPU 或 Object 生命周期。
- `Rendering/CascadedShadowMap.h/.cpp`：atlas、每相机统计历史、绘制与绑定主类 `CascadedShadowMap`。
- `Rendering/Backend/RenderBackend.h`：`GpuDepthDistribution`（64 个 uint32 对数桶、near/far）；新增异步统计的创建、提交、轮询、释放接口。
- `Rendering/Backend/GpuResourceIDs.h`：强类型 `GpuDepthDistributionID`。
- `Rendering/Backend/OpenGLRenderBackend.h/.cpp`：每个统计句柄一个 SSBO 与 fence，compute program 在后端延迟创建。
- `Templates/Examples/FlightTraining/Shaders/Builtin/shadow_common.orbinc`：统一 CSM atlas 查询与 PCF。
- `blinn_phong_shadow.orbshader`：通过 include 使用统一查询。
- `Templates/Project/Content/Shaders/shadow_depth.orbshader`：继续承担不透明几何深度绘制。

新增文件更新 Core vcxproj 与 filters；Object 绑定只通过 MetaGen 构建刷新。

## 分区算法

`ShadowCascadeBuilder::BuildSplits`：

1. 校验所有浮点配置，非有限数使用表中默认值；near 至少 0.001，far 至少 near + 0.001。
2. 令 n 为 near，f 为 min(shadowDistance, camera.farPlane)，N 为级联数；内部边界为 `(1-lambda)*(n+(f-n)*i/N)+lambda*n*pow(f/n,i/N)`，最后边界固定 f。
3. adaptive 且有有效直方图时，按累计样本计算 i/N 分位数；桶内在对数深度域插值。将结果限制在基线边界的 0.5..2 倍，再以 0.35 权重与基线在对数域混合。
4. 始终覆盖 n..f；统计只移动内部边界，不缩短总范围，不剔除统计未命中的接收物。
5. 上帧边界以 `1-exp(-6*dt)` 在对数域平滑；dt 从相机 elapsedTime 推导并限制 0..0.1。首次使用、相机切换参数、倒退时间、超过 1 秒间隔或瞬移后立即重建。
6. 保证边界严格递增；相邻边界至少保留全范围的 1e-6。

这是以深度直方图分位数调整 Z 分区的延迟 SDSM 变体，不宣称逐项复现 Intel SDSM 的全部投影拟合优化。固定总覆盖和限制自适应幅度是本实现的稳定性约束。

## 稳定投影与投射物

`ShadowCascadeBuilder::BuildCascade`：

1. 用相机投影逆矩阵还原 NDC 四角射线，在本级起止视深度截取八角。后一级从前一级的过渡起点开始覆盖。
2. 相机空间八角的包围球中心变换到世界空间，半径按 1/16 世界单位向上量化。该球半径不随相机朝向改变。
3. 从太阳方向建立固定正交基；退化方向使用引擎默认太阳方向。选择不平行的 up 轴。
4. 球投影到光源 XY，增加 PCF 边缘保护，中心对齐本级世界纹素网格。
5. 从完整场景筛选有效、castShadows、Opaque、与相机 layer mask 匹配的渲染器。投射物的光源 XY AABB 与接收矩形相交，且光源 Z 不完全位于接收区域下游时，纳入深度范围；不使用相机视锥剔除投射物。
6. Z 范围覆盖接收球与候选投射物，并留出偏移余量。无效 bounds 不参与数学合并，但绘制阶段不以 bounds 剔除该物体。
7. 输出 worldToShadow 矩阵、光源视锥、世界纹素宽度与 depthRange。

近处驾驶舱和远景使用同一分区、同一采样规则。大范围投射物造成 Z 跨度增加时 D32F 与世界尺度偏移减轻误差；不承诺任意掠射角仍保留毫米精度。

## Atlas 与采样

- 两列、ceil(N/2) 行，每格 resolution²；D32F，最近邻，手动 PCF；最多一个 atlas 供当前顺序绘制的相机复用。
- 每相机主 pass 前重绘自己的级联；不同相机保留独立 SDSM 历史，但不各自常驻大 atlas。
- 每格通过 viewport 与局部清深度初始化；明确设置 Less、depth write、无 blend、Cull None、无 polygon offset，结束后恢复管线基线。
- `BindUniforms` 上传最多六组矩阵、分界、atlas rect、texel/depthRange，以及相机 view、世界尺度 bias、过渡和 debug 参数。
- 片元通过世界位置求视深度选择级联；先按几何法线和光照夹角施加 normal bias，再投影。
- 使用接收面导数求阴影 UV 对应的深度梯度；导数在非一致分支之前计算。雅可比退化时关闭梯度修正，拒绝非有限结果。
- 4×4 tent PCF 比较结果加权过滤；每次采样限制在当前 atlas 格的半纹素内，禁止跨级联读深度。
- 每级末端按 blendRatio 双采样并混合；最远级最后 blendRatio 范围淡出。
- 查询接口返回 0..1 遮挡，再乘 shadowStrength；只影响直接太阳光。debug 模式在示例最终颜色阶段显示级联或纹素尺寸。

## 异步深度统计

`CreateDepthDistribution` 创建 64 桶 SSBO。`SubmitDepthDistribution` 在现有 cameraDepthTexture 复制完成后运行 OpenGL 4.3 compute：16×16 工作组，读取全部像素、忽略清屏深度和非有限值；反投影为线性视深度，落入 near..shadowFar 的 64 个对数桶。组内 shared 原子累计，组结束合并到 SSBO。

提交后 memory barrier 与 fence；一个句柄最多一个在途任务，未完成时不覆盖、不等待。`TryReadDepthDistribution` 使用零超时轮询，完成后读取 256 字节并释放 fence。GPU 任务读取的是冻结相机深度，不是阴影 atlas。

每相机历史以 EnsId（id 与 version）关联；保存提交时 near/far、位置、朝向、FOV、viewport 与 layer mask。统计结果只有在投影配置未变化、位移不超过 max(2, 首级分界×0.25)、朝向点积不少于 0.95 时用于适配；否则丢弃并使用完整 CSM 分区。相机移除、项目切换、配置关闭和 Shutdown 都释放句柄。

CSM 模式不执行统计 compute。adaptive 创建或提交失败时保留正确的稳定 CSM 覆盖并记录错误，不回退到旧单图算法。

## 内容与兼容边界

旧的单矩阵 / varying 查询删除；仓库示例统一改为 `SampleShadow(worldPosition, geometricNormal)`。旧自定义 shader 需要按本文件 ABI 迁移，不静默宣称旧代码支持级联。

现有深度 pass 只支持不透明网格轮廓；本次不新增任意材质 alpha discard 或顶点位移的 depth pass 编译系统。使用此类自定义 shader 的投射物必须提供一致的深度几何，本次测试不把缺少此能力当成已解决。普通透明与折射保持原有不投射策略。

项目版本 8→9；shadowBias 单位变更在升级记录中说明，模板场景显式更新；保留原 shadowDistance 值的用户项目仍按该接收范围运行。内容升级不覆盖用户 shader，迁移文档提供新的 include 使用方式。所有发布包重新构建。

## 验证与完成标准

- CPU 回归：分区单调和总覆盖；空直方图；近/远集中分布；配置非法值；相机旋转球半径稳定；纹素对齐；级联过渡角点覆盖；屏幕外上游投射物纳入；不同太阳方向与低角度矩阵有限。
- 隐藏 OpenGL 窗口回归：D32F atlas 创建；compute 编译；已知深度直方图与空深度；无等待轮询；资源反复释放；统一阴影 include 编译；atlas 边界、深度比较和 PCF 像素结果。
- Core Debug 构建刷新元数据与 C#；回归 Shader 内置 uniform 不被识别为材质字段。
- 更新 RenderingPipeline 文档、版本记录与本清单；记录实际执行的命令和失败限制。
- 人工场景验收单独记录，未实际观察的驾驶舱、低角度、远景和掌机性能不标为已通过。

## TODO

- [x] 核实代码与写出设计。
- [x] 实现配置快照和纯数学分区 / 投影。
- [x] 实现 D32F 与异步 GPU 深度直方图。
- [x] 实现每相机 CSM 绘制、SDSM 历史和生命周期。
- [x] 更新阴影 Shader ABI、PCF、过渡与调试视图。
- [x] 更新模板、版本、工程列表与渲染文档。
- [x] 构建 Core 并运行 CPU 回归；Core 全量重编与解决方案构建均 0 错误，CPU 数学回归通过。
- [ ] 运行 OpenGL 回归。未执行：仓库内没有该测试工程，本次决定不新建，转人工验收，见回归记录。
- [x] 审查最终差异并记录验证结果与人工验收边界。

## 技术资料

- [Intel SDSM](https://www.intel.com/content/www/us/en/developer/articles/technical/sample-distribution-shadow-maps.html)
- [Microsoft CSM](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps)

## 自定义 Shader 迁移

1. 将模板的 Shaders/Builtin/shadow_common.orbinc 复制到自定义 Shader 可访问的 include 路径。
2. 删除旧 v_LightSpacePosition、顶点阶段 u_LightViewProjection 计算及旧 SampleShadow() 函数。
3. 片元阶段声明 u_LightDirection 后 include 新文件。
4. 使用 SampleShadow(worldPosition, surfaceNormal) 获取遮挡，仅用 (1-shadow) 调制太阳直接光。
5. 使用 GetShadowDebugColor(worldPosition, finalColor) 支持调试视图；自定义 Shader 不调用此函数时仍正常接收阴影。
6. 不给材质手动配置 u_Shadow* 参数；它们由管线提供。shadow_depth.orbshader 内的 u_LightViewProjection 是深度绘制入口，继续保留。
7. 默认 shadowBias=0.0005 世界单位；旧 .world 中显式的 shadowDistance 不会自动扩大，增大距离时同时检查 Camera.farPlane。

DevPanel 的 `Reset from Template` 可以直接用于把示例的旧 Shader 换成本文件 ABI：该操作现在会连同资源注册表一起重置，改完立即生效，不需要再手动重载项目。改用之前它只清世界、不清 `ResourceManager`，已加载的 Shader 会继续持有旧源码并沿用旧编译结果（旧程序读 `u_LightViewProjection`，而新管线不再上传，表现为**完全无阴影**）。

## 回归记录

实际执行的命令（Git Bash；MSBuild 为 Visual Studio 18 Community）：

1. `MSBuild.exe Build/Tests/ShadowCascadeMath.vcxproj -m -p:Configuration=Debug -p:Platform=x64 -v:minimal -nologo`
   重编译后运行 `Log/ShadowTests/ShadowCascadeMath.exe`，输出 `Shadow cascade math tests passed.`，退出码 0。覆盖分区单调与总覆盖、空直方图回退、分位数响应、非法配置修正、相机旋转球半径稳定、视锥八角覆盖、屏幕外上游投射物纳入、亚纹素平移网格不变。
2. `MSBuild.exe OrbedenCore/OrbedenCore.vcxproj -t:Rebuild -m -p:Configuration=Debug -p:Platform=x64`
   全量重编 0 错误 0 警告。已核对 ShadowCascadeBuilder、CascadedShadowMap、ForwardPipeline、OpenGLRenderBackend 四个编译单元的 .obj 时间戳晚于源文件，DLL 于其后重链，确认为真实重编而非增量跳过。
3. `MSBuild.exe orbeden.slnx -m -p:Configuration=Debug -p:Platform=x64`
   0 错误；复制 38 + 13 个模板与图标文件。`x64/Debug/Templates/Examples/FlightTraining/Shaders/` 下的 `blinn_phong_shadow.orbshader` 与 `Builtin/shadow_common.orbinc` 已与仓库源文件逐字节一致。

失败与限制：

- **未执行隐藏窗口 OpenGL 回归。** 仓库内没有该测试工程（`Build/Tests` 只有 4 个纯 CPU 工程），`CreateDepthDistribution` / `SubmitDepthDistribution` / `TryReadDepthDistribution` 在测试代码中没有任何调用点。本次决定不新建工程，GPU 侧全部转人工验收，见下节。
- MSBuild 参数在本仓库的 Git Bash 下必须用 `-` 前缀。`/m`、`/nologo`、`/p:` 会被 MSYS 改写成 `M:/`、`C:/Program Files/Git/nologo` 这类路径，MSBuild 报 MSB1008；而 `MSBuild ... | tail` 的管道退出码取自 `tail`，会把构建失败伪装成通过（需用 `${PIPESTATUS[0]}` 或先落盘日志）。`Build/*.ps1` 中的 `/m /p:` 写法在 PowerShell 下不受影响。
- 人工验收要跑刚重建出来的 `x64/Debug/OrbedenEditor.exe`。编辑器按 `<exe目录>/Templates` 优先解析模板（`EditorSystem.cpp:2021`），所以 **exe 旁边那份 `Templates` 副本必须是新的**；`OrbedenEditor/x64/Debug/Templates` 下的副本停留在 9 月 10 日、仍是旧阴影 ABI，跑那个 exe 会加载旧 Shader。两份 `Templates` 都是构建产物副本，重建 Editor 时会自动刷新，不要手工编辑。模板源只有 `OrbedenEditor/Templates/` 一处。
- 文档侧修正：`RenderingPipeline.md` 总流程图中残留的 `共享 Shadow Pass` 节点已删除，`有相机?` 直接进入按 `Camera.depth` 遍历。

## 人工验收边界

以下项目未实际观察，不得标为已通过：

- D32F atlas 创建、compute 直方图实际运行、fence 零超时轮询、句柄反复释放。
- 统一阴影 include 在真实内容根下的编译结果，以及 atlas 边界、深度比较与 PCF 的像素结果。
- SDSM 自适应是否真正生效（依赖上一项）。compute 不可用时 `shadowAdaptive` 会静默退回稳定 CSM 覆盖，仅在日志留下 `SDSM histogram unavailable; stable CSM remains active.`，画面不会报错。
- 驾驶舱近景、低角度掠射、远景级联过渡、掌机与低端性能。
- 多相机同帧的 atlas 复用与 SDSM 历史隔离；编辑器主视口与 Game 视图并存时的表现。
- `shadowDebugView` 取值 1 / 2 的实际显示。
- 屏幕空间接触阴影、云阴影、任意材质 alpha discard 的 ShadowCaster Pass 仍不在范围内。
