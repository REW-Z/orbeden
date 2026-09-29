# Orbeden 球形物理大气与空气透视：低开销实施设计
日期：2026-09-29。本文只规定实现，不修改代码；目标平台为 Windows、Switch、Linux、FreeBSD。
本文的“雾”指球形大气造成的空气透视与薄霾，不承担局部水滴浓雾、体积云或降落灯光束。
优先级：稳定帧时间 > 跨平台可移植性 > 视觉一致性 > 物理精度。

## 1. 已核实的接入基础

以下路径均相对仓库根目录，行号对应本次读取的版本。

- `OrbedenCore/Src/Rendering/ForwardPipeline.cpp:228`：Render 为每相机选择主光；:266 绘制天空；:276/:279/:297 依次绘制不透明、透明、折射。
- `OrbedenCore/Src/Rendering/ForwardPipeline.cpp:283`：折射背景在透明队列结束后复制；:636 的 BindDrawState 负责公共参数。
- `OrbedenCore/Src/Rendering/ForwardPipeline.cpp:701`：材质纹理之后依次预留阴影、相机颜色、相机深度、环境反射四个槽。
- `OrbedenCore/Src/Rendering/RenderScene.h:17`：RenderCamera 包含相机矩阵、位置、视口、远近裁剪面及场景目标；:100 为 RenderDirectionalLight。
- `OrbedenCore/Src/Rendering/RenderScene.cpp:82`：复制世界 RenderSettings，并转换环境光颜色；:177 转换方向光为线性颜色。
- `OrbedenCore/Src/Runtime/RenderSettings.h:7`：已有天空盒引用、背景开关和独立环境反射参数。
- `OrbedenCore/Src/Rendering/Backend/RenderBackend.h:99`：GpuRenderTargetDesc 支持 RGBA16F 和线性采样；:149 为后端接口。
- `OrbedenCore/Src/Rendering/Backend/OpenGLRenderBackend.cpp:494`：CreateRenderTarget 当前拒绝无深度纹理的目标，需要扩展。
- `OrbedenCore/Src/Rendering/OutputPass.cpp:50`：曝光、AgX、sRGB 编码位于输出阶段；大气必须在其前方合成。
- `OrbedenCore/Src/Runtime/EngineTypes.h:19`：vector3 为 float32；本模块不能修复进入渲染快照前已丢失的全球坐标精度。
- `OrbedenCore/Src/Runtime/WorldSerializer.cpp:625`、:865：世界环境参数手工读取和保存。
- `OrbedenCore/Src/Runtime/Object/Shader.cpp:93`：IsBuiltinTextureUniform 排除引擎纹理；:104 的 IsBuiltinMaterialUniform 排除引擎参数。
- `OrbedenEditor/Src/Editor/ManagedEditorBridge.cpp:943` 与 `OrbedenEditor/Managed/Orbeden.Editor/EditorApplication.cs:46`：环境设置使用手写 ABI。
- `OrbedenEditor/Managed/Orbeden.Editor/EditorEnvironmentSettings.cs:34`、:81、:106：读取、提交和绘制环境设置。
- `Docs/ProjectConventions.md:9`：右手系、Y 向上；:132 要求引擎数值别名；:222 要求登记 vcxproj 与 filters。
- 已读后端和内置 Shader 使用 OpenGL/GLSL 430；本文定义的是可移植算法契约，不声称现有仓库已完成四个平台后端。

## 2. 功能边界与四种组合

- 天空模式独立取值：Cubemap 或 Atmosphere；保留 skyboxEnabled，作为两种背景共用的显示开关。
- atmosphere.fogEnabled 只控制物体空气透视，不控制程序化天空、太阳盘、灯光或反射。
- Cubemap＋FogOff：完整保留原有天空盒与材质画面。
- Cubemap＋FogOn：天空盒保持原图；场景表面应用大气雾。天空盒可能已含大气，不再整体二次加雾。
- Atmosphere＋FogOff：程序化天空与太阳盘正常显示，场景材质不应用空气透视。
- Atmosphere＋FogOn：天空和场景空气透视共用密度、太阳输入及单次散射算法。
- 关闭 skyboxEnabled 后不画背景；FogOn 仍作用于场景表面。None/DepthOnly 相机不覆盖既有背景。
- 天空模式切换当帧生效，不做隐式交叉淡化，不按飞行高度或相机距离切换算法。
- 程序化天空不自动生成反射 Cubemap；已有 reflectionEnvironment 与 skybox 反射来源继续生效，界面注明“反射环境单独配置”。
- 首版采用 Rayleigh＋Mie 单次散射；不实现多次散射、臭氧、月亮、星空、地形体积阴影、云层与天气模拟。
- 支持地表至 100 km 相机高度和最长 2500 km 空气透视；超范围采用本节后文定义的退化，不宣称支持太空飞行。

## 3. 确定的技术路线

- 大气径向对称：密度只依赖距地心的高度；采用球形地球，不实现椭球或随地形起伏的密度层。
- 创建太阳透光率二维 LUT、每相机空气透视二维图集 L/Tau、每相机天空二维 LUT。
- LUT 通过全屏四边形的片元着色器生成；不用 Compute、SSBO、3D 纹理、MRT、GPU float64 或时间重投影。
- 物体片元只查询 L/Tau 并合成，不做光线步进；天空片元查询天空 LUT 并解析绘制太阳盘。
- 图集 L 和 Tau 各生成一次，避免为本功能扩展 MRT；Tau Pass 只积分密度，不查询太阳光。
- 每个 ForwardPipeline 只保留一套可复用资源；顺序服务所有相机，保证资源数量不随相机数量增长。

## 4. 文件与类型

- 新增 `OrbedenCore/Src/Runtime/AtmosphereSettings.h`：SkyMode、AtmosphereQuality、AtmosphereSettings。
- 新增 `OrbedenCore/Src/Rendering/AtmosphereRenderer.h/.cpp`：AtmosphereFrame、AtmosphereQualityDesc、AtmosphereRenderer。
- 新增 `OrbedenEditor/Templates/Builtin/atmosphere_common.orbinc`：球面数学、密度与积分函数。
- 新增同目录 `atmosphere_sampling.orbinc`：材质空气透视查询与合成函数。
- 新增 `Builtin/Shaders/atmosphere_transmittance.orbshader`、`atmosphere_aerial.orbshader`、`atmosphere_sky_lut.orbshader`、`atmosphere_sky.orbshader`，完整磁盘前缀均为 `OrbedenEditor/Templates/`。
- 修改 ForwardPipeline、RenderSettings、WorldSerializer、RenderBackend、OpenGLRenderBackend、Shader.cpp 和上述编辑器环境设置桥接文件。
- 修改内置 pbs_metallic、blinn_phong、transparent、particle_unlit、particle_trail、refraction、rain_glass、heat_wake 八个 Shader。
- 使用现有 FullscreenQuad（`Rendering/FullscreenQuad.h:13`）和 GpuResourceManager::GetShader（`Rendering/GpuResourceManager.h:230`）。
- 新文件登记到 `OrbedenCore/OrbedenCore.vcxproj` 与对应 filters；新增函数写简短中文说明，不手改 Generated 目录。

## 5. 设置与数据布局

- `enum class SkyMode : uint32 { Cubemap=0, Atmosphere=1 }`；RenderSettings 新增 skyMode，默认 Cubemap。
- `enum class AtmosphereQuality : uint32 { Low=0, Balanced=1 }`；所有平台默认 Low。
- `AtmosphereSettings` 字段按顺序：bool fogEnabled=false；AtmosphereQuality quality=Low。
- 接续三个 float64：planetCenterX=0、planetCenterY=-6371000、planetCenterZ=0，单位为米，表示当前世界原点坐标系内的地心。
- 接续三个 float32：metersPerWorldUnit=1、aerosolDensity=1、sunRadianceScale=20。
- RenderSettings 新增 `AtmosphereSettings atmosphere`；数值范围：metersPerWorldUnit 为 [0.0001,10000]，aerosolDensity 为 [0,8]，sunRadianceScale 为 [0,100]。
- aerosolDensity=0 只关闭 Mie，保留 Rayleigh；sunRadianceScale 只缩放散射亮度与太阳盘，不改变消光、表面主光或环境光。
- 地球半径 R=6371 km，大气顶 H=100 km；Rayleigh 高度 HR=8 km，Mie 高度 HM=1.2 km。
- Rayleigh 散射系数 RGB=(0.005802,0.013558,0.033100)/km；Mie 散射=0.003996/km，消光=0.004440/km，均乘 aerosolDensity；Mie g=0.8。
- `AtmosphereQualityDesc` 依次保存 int32 transWidth、transHeight、aerialWidth、aerialHeight、aerialSlices、atlasColumns、skyWidth、skyHeight、viewSteps、skySteps。
- Low 对应 (128,64,24,14,16,4,96,48,12,24)；Balanced 对应 (256,64,48,28,24,6,192,96,24,48)。
- `AtmosphereFrame` 保存 bool fogActive、skyActive；quality；float32 cameraHeightKm、maxDistanceKm；vector3 cameraUp、sunDirection；color sunRadiance。
- 接续 matrix4x4 inverseProjection、cameraWorldRotation；int32 viewportWidth、viewportHeight；color groundBackground；AtmosphereSettings settings。
- fogActive=settings.fogEnabled；skyActive=renderSettings.skyboxEnabled && skyMode==Atmosphere && camera.clearMode==SolidColor；非法配置或阴影调试使两者同时为 false。
- sunDirection=-mainLight.direction；sunRadiance.rgb=mainLight.color.rgb×max(intensity,0)×sunRadianceScale；alpha=1；无主光时 RGB=0。
- cameraWorldRotation 取相机 worldMatrix 的归一化旋转基，平移置零；拒绝退化基与非透视投影，本相机本帧停用大气并只记录一次错误。
- 非有限设置、非法枚举、非正单位比例属于非法配置：编辑器拒绝提交、XML 读取失败；运行时快照停用本相机大气并记录一次错误。

## 6. 坐标、球面与精度

- CPU 以 float64 计算 p=camera.position×metersPerWorldUnit−planetCenter，再计算 h=length(p)/1000−R、up=p/length(p)。
- GPU 只接收 h、up、相机相对距离和旋转；不上传百万米级绝对位置再相减。
- 世界执行原点平移 Δworld 时，调用方必须同步执行 planetCenter-=Δworld×metersPerWorldUnit；本功能不实现地形或 Transform 原点平移系统。
- 相机 h∈[-0.1,0) km 时，积分高度固定到 0.001 km；h<-0.1 或 h>100 时该相机停用大气、背景走原天空盒路径并记录一次诊断。
- 地球模型用于介质边界，真实地形仍由引擎绘制；地形海拔不改变径向密度。
- `ComputeAltitude(h,mu,s)`：q=h×(2R+h)+2s×(R+h)×mu+s²；返回 q/(sqrt(max(R²+q,0))+R)，避免直接相减损失近地高度精度。
- `IntersectAtmosphereBoundary(h,mu,Hb)`：求 s²+2bs+c=0，其中 b=(R+h)mu，c=(h−Hb)(2R+h+Hb)，Hb 为 0 或 100。
- 判别式<0 返回无交点；使用 qroot=−b−signNonzero(b)×sqrt(b²−c)，两根为 qroot 与 c/qroot，qroot=0 时两根取 −b；排序后选择最近非负交点。
- `ClipAtmosphereSegment(h,mu,d)`：终点取 d、大气顶出口、地球首次入口中的最小非负值；地面向外射线忽略 s=0 的地球交点。
- 查询点 h<0 时密度钳制到海平面，h>100 时密度为零；所有长度在 Shader 内以 km 表示。
- 每相机 D=maxDistanceKm=clamp(farPlane×metersPerWorldUnit/1000,1,2500)；表面距离>D 时使用 D 的结果，记录这是远景近似上限。

## 7. 光学模型与确定的积分算法

- `EvaluateDensity(h)` 返回 rhoR=exp(−max(h,0)/8)、rhoM=exp(−max(h,0)/1.2)；h>100 返回零。
- sigmaS_R=betaR×rhoR；sigmaS_M=0.003996×aerosolDensity×rhoM；sigmaT=sigmaS_R+0.004440×aerosolDensity×rhoM。
- `EvaluatePhase(nu)`：PR=3(1+nu²)/(16π)；PM=(1−g²)/(4π×max(1+g²−2g×nu,0.0001)^1.5)；nu=dot(viewRay,sunDirection)。
- `BuildIntegrationInterval`：在裁剪后的 [0,d] 内以 sClosest=clamp(−(R+h)mu,0,d) 分段，将步数平均分给两侧非空区间；仅一侧非空时使用全部步数。
- 每个子区间使用二次分布边界，使步长在靠近 sClosest 的一端最小；靠左时 t_j=(j/n)²，靠右时 t_j=1−(1−j/n)²。
- 每小段以两个边界的中点采样密度，ds 为边界差；禁止添加随机抖动。
- `IntegrateAtmosphere(h,up,ray,d,N,outputKind)`：先裁剪、分段，初始化 T=1、L=0、Tau=0，再从近到远积分。
- 每段 e=exp(−sigmaT×ds)，w 分通道取 (1−e)/sigmaT；sigmaT<1e−6 时 w=ds。
- outputKind=Radiance 时，当前位置的局部 up=normalize((R+h)up+s×ray)，用局部高度与 dot(up,sunDirection) 查询 Tsun。
- J=sunRadiance×Tsun×(sigmaS_R×PR+sigmaS_M×PM)；执行 L+=T×J×w、T*=e、Tau+=sigmaT×ds。
- outputKind=OpticalDepth 时只累积 Tau；两种输出必须使用相同采样区间。存储 Tau 时逐通道钳制到 20。
- 不使用提前终止，确保固定工作量与 L/Tau 一致；输出 L.rgb 或 Tau.rgb，alpha 固定为 1。
- 这是基于物理的单次散射近似，不将 sunRadianceScale 当作 lux，不保证与现有美术灯光的绝对能量一致。

## 8. 太阳透光率 LUT

- `atmosphere_transmittance.orbshader` 输出 RGBA16F 的 RGB 透光率；仅密度或质量变化时重建。
- 网格节点 v=j/(height−1)，h=100v²；令 r=R+h，muH=−sqrt(max(h(2R+h),0))/r。
- 节点 u=i/(width−1)，mu=muH+(1−muH)u²；所有节点代表未穿过地球的出射光线。
- 使用第 7 节的分段二次积分、固定 64 步计算到大气顶的光学厚度；输出 exp(−Tau)。
- `SampleSunTransmittance(h,mu)`：先判断太阳射线是否穿过地球，穿过则返回零；否则反算 v=sqrt(h/100)、u=sqrt(clamp((mu−muH)/(1−muH),0,1))。
- 每个轴以 (0.5+x×(size−1))/size 查询，线性采样、ClampToEdge、无 mip；h=100 且向外的路径返回 1。
- 地球遮光与日落来自上述求交；不读取 CSM，不模拟建筑、山脉或云对天空散射的阴影。

## 9. 空气透视图集

- 节点 (x,y) 对应屏幕 uv=(x/(W−1),y/(H−1))；inverseProjection 将 (2uv−1,1,1) 还原视线，经 cameraWorldRotation 后归一化。
- 距离层 k 的 d_k=0.01×(exp(k/(Z−1)×ln(1+D/0.01))−1) km；k=0 写 L=0、Tau=0。
- 图集列数为 atlasColumns，行数=Z/atlasColumns；层 k 位于 (k%columns,k/columns)。
- 每层是 W×H 节点；整张图集宽=W×columns、高=H×rows；Low 为 96×56，Balanced 为 288×112。
- 每个图集像素重建所属层、节点和射线；L Pass 使用 viewSteps 积分，Tau Pass 使用相同步数但跳过太阳与相位函数。
- `SampleAerialPerspective(uv,dKm)`：将 uv 钳制 [0,1]，z=ln(1+clamp(dKm,0,D)/0.01)/ln(1+D/0.01)×(Z−1)，取相邻两层。
- 每层图集坐标=(tileOrigin+0.5+uv×(W−1,H−1))/atlasSize，保证双线性过滤不串层；分别查询 L 和 Tau，再按 z 小数插值。
- 返回 `AtmosphereSample { vec3 radiance; vec3 transmittance; }`，T=exp(−Tau)；共四次二维纹理查询。
- d=0 或 fogActive=false 返回 L=0、T=1；距离使用表面到相机的欧氏距离，不使用视空间 z。
- 空间密度平滑使 LUT 可低分辨率；不从深度纹理生成图集，因此不在山体轮廓处把背景雾涂到前景。

## 10. 程序化天空

- `atmosphere_sky_lut.orbshader`：屏幕方向映射与图集一致，使用 skySteps 积分到大气顶或地球交点；无远裁剪限制。
- 天空 LUT 存储 L+T×groundBackground.rgb，仅地球相交射线含后项；未命中地球的外空间背景为零。
- groundBackground 使用相机已线性化的 clearColor，作为未加载远地形的颜色底板；不是实体地面，不写深度。
- `atmosphere_sky.orbshader` 以 (0.5+screenUv×(skySize−1))/skySize 查询天空 LUT；太阳盘不烘焙入低分辨率 LUT，避免转头时闪烁。
- 太阳角半径固定 0.00465 rad；以 dot(ray,sunDirection) 与 cos(0.00465) 比较，边缘宽度用 max(fwidth(dot),1e−7)。
- 太阳盘 RGB=sunRadiance×SampleSunTransmittance(cameraHeight, dot(cameraUp,sunDirection))/(π×sin(0.00465)²)，乘太阳盘覆盖率。
- 当前观察射线命中地球时不画太阳盘；最终输出 skyLut.rgb+sunDisk.rgb，alpha=1。
- 绘制时禁用深度测试、深度写入与混合；保持主 Pass 已清理的深度，绘制后恢复主 Pass 基线。
- 夜间单次散射可能偏暗；环境照明、反射与星空不由此功能自动补偿。

## 11. Renderer 方法与资源生命周期

- AtmosphereRenderer 持有 RenderBackend*、FullscreenQuad、四个 Ref<Shader>、四个 GpuRenderTargetID、GpuTextureID neutralTexture、当前 AtmosphereFrame、透光率缓存键和视图缓存键。
- 缓存另存关联 Shader program ID；program 更换即使参数相同也重建关联 LUT；资源有效标志和一次性错误标志在资源失效时清零。
- 四个目标分别为 transmittanceTarget、aerialRadianceTarget、aerialOpticalDepthTarget、skyTarget；颜色纹理由 GetRenderTargetColorTexture 取得，随目标释放。
- `Initialize(RenderBackend*)`：同后端直接返回；不同后端先 Shutdown，再绑定后端并初始化 quad；以 CreateTexture 创建 1×1、RGBA8、全零、srgb=false 的 neutralTexture，不提交绘制。
- `EnsureResources(GpuResourceManager&,quality,needFog,needSky)->bool`：按第 4 节固定资源 Key 加载 Shader，取 passes[0].shaderProgram；只创建需要的目标。
- 全屏 Shader 声明 geometry Standard，禁用 expandedGeometry；顶点只读取 a_Position/a_TexCoord；新增 uniform 统一使用 u_Atmosphere 前缀。
- 每次资源确保检查纹理句柄、Shader pass、FBO 完整性和 quad；任何失败关闭本相机大气并恢复原天空盒路径，单次记录错误。
- `BuildFrame(const RenderScene&,const RenderCamera&,const RenderDirectionalLight*,AtmosphereFrame&)->bool`：校验设置与投影，计算第 5/6 节全部字段。
- `PrepareCamera(scene,camera,mainLight,gpuManager)->bool`：在主 Pass 开始前 BuildFrame；两开关均未激活时直接返回，不加载 Shader、不创建 LUT；否则 EnsureResources、更新透光率和需要的视图 LUT。
- 透光率键=(quality,aerosolDensity)；视图键为 AtmosphereFrame 全部有效字段逐字段比较，禁止对含 padding 的结构 memcmp。
- 视图键完全相同且相关 LUT 有效时跳过更新；移动、转头、太阳或参数改变当帧完整更新，不跨帧分摊、不重用旧视线。
- `RenderLut(target,program,outputKind)`：BeginPass、关闭深度/混合/剔除、绑定 quad/参数、DrawIndexed(0,6)、EndPass；不能嵌套主 Pass。
- `BindMaterialUniforms()`：向当前程序提交下述公共参数；程序一次配置，但纹理每批次绑定。
- 公共参数固定为 u_AtmosphereFogEnabled(int)、u_AtmosphereMaxDistanceKm(float)、u_AtmosphereMetersPerWorldUnit(float)、u_AtmosphereViewport(vec4:width,height,0,0)、u_AtmosphereAtlas(vec4:W,H,Z,columns)。
- LUT/天空另使用 u_AtmosphereCameraHeightKm(float)、u_AtmosphereCameraUp(vec3)、u_AtmosphereSunDirection(vec3)、u_AtmosphereSunRadiance(vec4)、u_AtmosphereAerosolDensity(float)。
- 矩阵为 u_AtmosphereInverseProjection、u_AtmosphereCameraWorldRotation；vec4 参数另有 u_AtmosphereGroundBackground、u_AtmosphereTransSize(width,height,0,0)、u_AtmosphereSkySize(width,height,0,0)。
- 整数控制为 u_AtmosphereOutputKind（0=L、1=Tau）、u_AtmosphereStepCount；循环编译上限固定 64，达到 StepCount 后退出；纹理名称见第 13 节。
- `BindMaterialTextures(uint32 firstSlot)`：绑定 L、Tau 两槽；fogInactive 时绑定有效 1×1 零纹理占位，shader 动态分支直接返回。
- `RenderSky()`：仅在 skyActive 时在当前主 Pass 绘制，不自行 BeginPass；返回 bool 表示是否成功替代原天空盒。
- `InvalidateResources()`：释放四个目标和 Shader 引用，清缓存键；quad 和 neutralTexture 保留，内容根切换后重新加载。
- `Shutdown()`：InvalidateResources、DeleteTexture(neutralTexture)、quad.Shutdown、清后端指针；不单独删除目标拥有的颜色纹理。
- 关闭功能保留已分配资源供再次启用；质量改变先释放旧尺寸目标，再创建新目标；不存在后台更新。

## 12. 后端契约与跨平台

- GpuRenderTargetDesc 新增 bool colorOnly=false；colorOnly=true 必须 depthOnly=false 且 depthTexture 无效；仅此组合允许无深度创建。
- 修改 OpenGL CreateRenderTarget：按 colorOnly 跳过深度有效性要求及深度附件，保留现有目标行为；四张大气 LUT 全部 colorOnly=true。
- 新增 `RenderBackend::GetFragmentTextureUnitCount() const -> uint32`；OpenGL 初始化查询一次 GL_MAX_TEXTURE_IMAGE_UNITS 并缓存。
- 最低能力：片元 float32、二维 RGBA16F 颜色目标及线性采样、至少 16 个片元纹理槽、至少 512×512 纹理。
- LUT 为线性数据，关闭 sRGB 与混合，不生成 mip；不以 RGBA8 静默代替半浮点。
- 新 Shader 数学限制在顶点/片元公共子集；当前 OpenGL 入口使用 GLSL 430。Switch 后端通过正式 SDK 的着色器工具转换同一算法，不假定桌面 GLSL 可直接运行。
- Windows/Linux/FreeBSD 使用已实现的 OpenGL 后端验证；Switch 接入属于平台后端工作，需正式开发机和对应工具链验收。
- 未满足能力时两种大气功能停用、贴图天空盒仍可使用；这属于不支持配置，不能标记为目标平台通过。

## 13. 管线与材质接入

- ForwardPipeline 新增 AtmosphereRenderer atmosphere；Initialize/InvalidateResourceCaches/Shutdown 转发对应生命周期。
- Render 选定 mainLight 后、主 BeginPass 前调用 PrepareCamera；阴影调试视图启用时传入的本帧大气开关强制为 false。
- 原天空绘制位置：skyboxEnabled 且 skyMode=Atmosphere 时先 RenderSky，失败调用原 RenderSkybox；Cubemap 直接走原路径。
- BindDrawState 每程序首次配置调用 BindMaterialUniforms；设 M=material.textureBindings.size()，保留既有 M..M+3，在 M+4/M+5 绑定 L/Tau。
- FogOn 且 M+6 超过片元纹理槽上限时拒绝该材质配置并给出资源 Key；实现验收覆盖内置八个 Shader，不能静默使部分物体无雾。
- FogOff 且 M+6 超限时两个大气 sampler 均指向既有 M+1 相机颜色槽，不覆盖其纹理绑定；该槽类型同为 sampler2D，关闭分支不采样，保持旧材质可用。
- Shader.cpp 的 IsBuiltinTextureUniform 增加 u_AtmosphereRadianceTexture、u_AtmosphereOpticalDepthTexture、u_AtmosphereTransmittanceTexture、u_AtmosphereSkyTexture。
- IsBuiltinMaterialUniform 排除所有 u_Atmosphere 前缀，避免 LUT 和控制参数进入材质属性列表。
- `ApplyAtmosphereToSurface(vec3 rgb,vec3 worldPosition,vec2 screenUv)`：dKm=length(worldPosition−u_CameraPosition)×metersPerWorldUnit/1000，查询 L/T 后返回 rgb×T+L；alpha 保持不变。
- 材质 screenUv 固定为 gl_FragCoord.xy/u_AtmosphereViewport.xy；主场景目标原点为零，不叠加最终输出 Viewport 偏移；新增粒子 varying 使用同一坐标约定。
- pbs_metallic、blinn_phong、transparent 在光照、自发光、tint 完成之后调用；阴影调试颜色输出之前停用雾分支。
- particle_unlit/particle_trail 新增最终世界位置 varying；Alpha 混合使用 rgb×T+L，Additive 混合只用 rgb×T。
- BindDrawState 新增 u_AtmosphereAdditive，每批根据 BlendMode 设置；Alpha 与 Additive 不共享缓存值。
- 不新增场景全屏加雾 Pass；不再对完成透明混合的场景加第二遍雾；OutputPass 无需修改。
- 自定义表面 Shader 必须 include atmosphere_sampling 并调用 ApplyAtmosphereToSurface；旧 Shader 不自动改写，在文档列出迁移契约。
- refraction/heat_wake 采样到的 cameraColor 已包含雾，禁止再调用表面加雾函数。
- rain_glass 保留 transmitted，只把本地 reflection 乘该表面的 T，不再额外加 L；无 cameraColor 的透明降级分支执行普通表面雾规则。
- refraction 的染色继续作用于已加雾背景，这是保留的屏幕空间近似；不反演背景雾，不增加折射路径体积积分。
- 编辑器辅助线、选择描边、UI 不参与本模块；已有世界粒子按上述 Shader 契约参与。

## 14. 保存、编辑器与发布

- WorldSerializer 在现有 RenderSettings 标签增加属性：skyMode、atmosphereFogEnabled、atmosphereQuality、planetCenterX、planetCenterY、planetCenterZ、metersPerWorldUnit、aerosolDensity、sunRadianceScale。
- 缺失属性使用第 5 节默认值，保证老世界仍为贴图天空＋雾关闭；整数枚举按数值保存，float64 使用 17 位有效数字、Invariant 格式。
- 编辑器 ABI 在原 64 字节后追加：uint32 SkyMode、FogEnabled、Quality、Reserved；float64 CenterX/Y/Z；float32 MetersPerWorldUnit、AerosolDensity、SunRadianceScale、Reserved2。
- 追加字段偏移依次为 64/68/72/76/80/88/96/104/108/112/116；现有 64 位编辑器 ABI 总大小 120 字节，C++ 与 C# 同步增加尺寸及偏移断言；不将编辑器 ABI 用作跨平台存档。
- EditorApplication 的 TryGetWorldRenderSettings/SetWorldRenderSettings 增加上述载荷；EditorEnvironmentSettings 的读取和提交完整往返所有字段。
- 面板依次绘制 Sky Enabled、Sky Mode、六面天空盒资源、Atmosphere Fog、Quality、Aerosol Density、Sun Radiance Scale、三个 Planet Center、Meters Per World Unit。
- 地心三个 float64 使用现有 InputText（EditorGUI.cs:227），按 InvariantCulture 解析；失焦合法后提交，非法时保留旧值并显示字段错误。
- Fog 关闭时程序化天空相关设置仍可编辑；Cubemap 模式仍保留大气参数；反射资源选择始终独立。
- 本功能的新字段写入世界，但不新增 Object 组件、不引入额外托管运行时 ABI；运行时代码通过 World.renderSettings 修改配置。
- 当前项目版本见 `OrbedenCore/Src/Defines/Version.h:8` 为 28；实施本方案升级为 29，并在 Docs/BuildAndPackaging.md 记录大气 Shader 与字段迁移。
- `OrbedenEditor/Src/Editor/ProjectUpgrader.cpp:87` 保留 Content，不能假设版本升级会更新游戏内 Builtin；必须显式发布新增 Shader/include。
- ContentMigration 新增 `MigrateAtmosphereAssets(contentRoot,templateRoot,report,error)->bool`，由 EditorSystem::RunProjectUpgrade 在提交版本号前调用；fromVersion<29 时执行，否则跳过。
- v29 新文件缺失则复制，相同则跳过，同 Key 不同内容保留并列入 pendingShaderKeys；八个既有 Shader 仅匹配随迁移器携带的 v28 基线时更新，用户改版保留并列入报告。
- 基线目录为 `OrbedenEditor/Templates/Migrations/28AtmosphereBaseline/`，运行时相对 templateRoot 读取同名八个文件；源/目标读写失败返回 false，不提交版本号。
- 基线文件随 v29 迁移资源发布；升级冲突项目仍能以默认 FogOff 打开，FogOn 验收前必须完成自定义 Shader 接入。
- 发布同时更新 Docs/RenderingPipeline.md、Docs/ColorPipeline.md 和 Builtin/README.md，写清四种组合与上述限制。

## 15. 工作量、内存与性能验收

- Low 每视图：L/Tau 图集各 5376 像素，天空 4608 像素；Balanced 分别为 32256、18432。
- Low 的 L 积分上限 64512 步、Tau 上限 64512 步、天空上限 110592 步；Balanced 分别为 774144、774144、884736。
- 透光率重建 Low 上限 524288 步、Balanced 上限 1048576 步，仅设置/质量改变发生。
- Low 四张颜色纹理约 184 KiB，Balanced 约 776 KiB；另计四边形、Shader、1×1 占位与驱动分配，不包含已有场景缓冲。
- 动态相机每帧最多三次 LUT draw＋一次天空 draw；透光率重建额外一次。FogOff 跳过两张图集更新，贴图天空跳过天空 LUT 与天空 draw。
- 两功能均关闭时不执行大气 Pass；材质大气分支不查询纹理。无新深度拷贝、无逐帧 CPU/GPU 同步读回。
- Switch 性能门槛：开发机掌机模式、1280×720、Low、单相机，雾＋程序化天空合计增量 GPU 时间 P95≤1.0 ms。
- Windows/Linux/FreeBSD 性能门槛：项目登记的最低配置设备、1920×1080、Low、单相机，增量 GPU 时间 P95≤1.0 ms。
- 上述为验收目标，不是当前实测结果；最低配置 GPU 型号必须记录在测试报告中，不能以高端设备替代。
- 固定飞行回放预热 300 帧，采集 1800 帧；同场景分别切换四种组合；包含材质查询、重叠透明和 LUT 生成成本。
- 四相机分屏单独记录总 GPU 增量；不承诺单相机预算覆盖四相机。无灯光不触发额外 CPU 工作。
- 如 Low 超预算，发布判定失败；不得通过隔帧更新、偷偷关闭雾或缩短雾距离使测试通过。

## 16. 正确性验收与 Todo list

- 数值参考使用 float64、4096 均匀步积分同一单次散射模型；不以参考图的曝光差掩盖误差。
- 测试高度 0.001/1/10/25/80 km、距离 0/0.01/1/10/100/1000 km、太阳高度 −10/0/10/60°，并覆盖地平线两侧。
- 所有结果有限，T∈[0,1]、L≥0；同一射线距离增加时 T 不增加；零距离严格 L=0/T=1。
- Low 与参考的透光率最大通道绝对误差≤0.08；在参考亮度≥0.01 的样本上 L 相对误差 P95≤25%；Balanced 分别≤0.04/15%。
- 连续爬升、360°转头、改变 FOV、视口缩放、两个相机交替、原点平移后画面无缓存串用；无人工时间抖动。
- 四种天空/雾组合、Sky Disabled、没有太阳、ClearMode 切换、阴影调试、资源重载均逐项截图验收。
- 检查透明玻璃、加法粒子、雨玻璃及热尾流：alpha 不变、无重复雾亮、无纹理槽冲突；近处座舱不出现天空 LUT 串色。
- 检查日落、地平线和下视薄霾；接受单次散射夜景偏暗、反射来自独立贴图、六面天空与实时太阳可能不匹配。
- [ ] P1：新增设置、枚举、XML 往返、编辑器 ABI 与独立开关；四组合默认行为通过。
- [ ] P2：扩展无深度颜色目标与纹理槽能力查询，完成四个平台能力登记。
- [ ] P3：实现球面数学、稳定高度计算、密度与单次散射；建立数值参考。
- [ ] P4：实现透光率 LUT、L/Tau 图集、天空 LUT 与太阳盘；验证图集边界。
- [ ] P5：接入每相机调度、生命周期、缓存键和纹理槽；验证资源失败恢复。
- [ ] P6：接入八个内置 Shader、Alpha/Additive 与折射规则；编写自定义 Shader 迁移说明。
- [ ] P7：升级 v29 模板与项目迁移，保留用户修改，完成老世界与旧项目回归。
- [ ] P8：执行数值、视觉、资源泄漏测试及四平台性能验收，提交配置、截图和 GPU 报告。
- P1→P3→P4→P5→P6→P7→P8 顺序执行；P2 必须在 P4 GPU 接入前完成；本次设计阶段不构建、不运行测试。

## 17. 原理来源

- [Bruneton：球形大气透光率与散射实现](https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html)：确认球形大气、散射和透光率的物理基础。
- [Hillaire：实时天空与空气透视](https://sebh.github.io/publications/egsr2020.pdf)：采用分离小型查找表的工程方向。
- 本文的图集布局、固定积分步数、参数限制和单次散射取舍是针对 Orbeden 的设计，不宣称完整复现上述算法。
