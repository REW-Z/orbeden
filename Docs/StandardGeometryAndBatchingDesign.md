# Standard 几何统一、绘制策略与 Ens Static 修改方案

## 1. 目标与范围
- 删除 Legacy 契约与旧几何兼容分支；迁移仓库内全部 Legacy Shader。
- Standard 支持 Uniform、Instanced、Expanded；静态合批与动态合批共用 Expanded。
- Material 不新增合批开关；Renderer 决定允许的优化路径，管线决定实际批次。
- Ens 增加 static 世界变换约束；static 不代表必须合批，也不自动传播到子节点。
- 本文只定义实施方案，不修改代码；保留 Particle 名称与拖尾功能，不扩大到 GPU 模拟、Indirect Draw 或 SRP Batcher。

## 2. 已核实的现状
- `OrbedenCore/Src/Runtime/Object/Shader.h:28`：Legacy=0、Standard=1、Particle=2；Pass 默认 Legacy。
- `OrbedenCore/Src/Rendering/GpuResourceManager.cpp:402`：Legacy 只编译原始 Program；Standard 编译 Uniform/Instanced；Particle 额外编译 Expanded/TrailInstanced。
- `OrbedenCore/Src/Rendering/DrawBatchBuilder.cpp:471`：EmitBatches 选择批次；静态网格来源至少两个兼容项才自动实例化。
- `OrbedenCore/Src/Runtime/Object/StaticMeshRenderer.h:52`：Renderer 已有 enableInstancing，默认 true。
- `OrbedenCore/Src/Rendering/ForwardPipeline.cpp:400`：展开绘制要求 expandedProgram；`:499`：单绘制逐 Pass 写 u_Model。
- `OrbedenEditor/Templates/Builtin/geometry_input.orbinc:36`：统一模型矩阵接口已在 Expanded 下返回单位矩阵。
- `OrbedenCore/Src/Runtime/Object/Transform.cpp:14`：位置、旋转、缩放 setter 直接修改并通知 World，尚无 static 约束。
- `OrbedenCore/Src/Runtime/World.cpp:572`：SetParent 负责重挂层级；`Runtime/WorldSerializer.cpp:387`、`:542` 负责 Ens XML 写入与读取。
- `OrbedenCore/Src/Runtime/CookedAssetSerializer.cpp:23`：BlobFormatTag=3；`Defines/Version.h:8`：项目版本=26。

## 3. Shader 契约与兼容能力
- 保留 `ShaderGeometryContract { Standard=1, Particle=2 }`，删除 Legacy，禁止将旧数值 0 重新解释为 Standard。
- ShaderPass.geometryContract 默认 Standard；省略声明采用 Standard；显式 Legacy 导入失败并报告迁移要求。
- Standard 编译 Uniform、Instanced、Expanded；Particle 在此基础上编译 TrailInstanced；任一声明变体失败则整个 Shader 上传失败。
- 保留 GeometryMode 四种模式及 Uniform 专用 Program；禁止用 instanceCount=1 统一替代普通单绘制。
- ShaderPass 新增 `bool supportsExpandedGeometry=true`；OrbShader Pass 状态新增 `expandedGeometry on|off`，单 Pass 简写支持顶层 `--------expandedGeometry on|off`。
- expandedGeometry 只能声明一次且位于 stage 前；off 时不编译 Expanded，也禁止静态/动态合批；Uniform、Instanced 保持可用。
- 自定义 Shader 依赖模型空间顶点动画时必须声明 off；实例路径保留各实例的原始模型空间。
- Uniform 从 u_Model、uniform tint/UV 读取；Instanced 从实例属性读取；Expanded 从世界空间顶点、顶点 tint 和已变换 UV 读取。
- 展开时 CPU 变换位置、逆转置变换法线、变换并正交化切线；非有限或奇异矩阵跳过该项并计入诊断。
- TrailInstanced 继续要求拖尾位置/颜色接口；声明 Particle 不代表普通顶点公式自动支持拖尾。
- Shader 支持某模式只表示输入兼容；透明、光照、软粒子、贴图 Alpha 均由原 Shader 代码决定。

## 4. Shader 迁移清单
- Builtin/Shaders 下 refraction、rain_glass、heat_wake 改为 Standard，替换直接 u_Model 为统一模型接口，保持原法线公式和片元输出。
- Examples/FlightTraining/Shaders 下 heat_wave_distortion、rain_glass_refraction 做同样迁移，保持已有 UV 与颜色行为。
- Project/Content/Shaders/skybox 改为 Standard 且 expandedGeometry off，保持专用视投影与深度公式，只由天空盒路径提交 Uniform。
- blinn_phong、pbs_metallic、transparent 保持 Standard，新增 Expanded 验证，不改变既有光照计算。
- particle_unlit、particle_trail、shadow_depth 保持 Particle；粒子普通几何与拖尾分别验证合法提交模式。
- 输出、描边、调试线的 C++ 内嵌 Program 不属于 ShaderGeometryContract，保持各自创建与绘制入口。
- 新增变体不能让未使用的实例属性进入 Uniform 输入；几何分支使用预处理宏，禁止运行时分支选择模式。

## 5. Renderer 策略与选择规则
- 在 Rendering/RenderTypes.h 定义 `DrawStrategy : uint32 { Auto=0, Individual=1, GpuInstancing=2, DynamicBatching=3 }`。
- StaticMeshRenderer 删除 enableInstancing，新增 `DrawStrategy drawStrategy=Auto`；Material 不新增字段。
- Individual：仅 Uniform，作为排序屏障；GpuInstancing：只允许 Instanced，否则 Uniform；DynamicBatching：只允许 Expanded 动态批，否则 Uniform。
- Auto：先使用有效静态批；剩余项先组成实例批；剩余符合预算的项组成动态批；其余 Uniform。
- static+GpuInstancing 使用实例批；static+Individual 使用单绘制；static+DynamicBatching 使用动态批，保持 static 变换约束。
- 普通 Renderer 合批必须至少包含两个绘制项；显式实例提交与粒子指定 Instanced 时，单实例仍允许走 Instanced。
- GPU 实例批要求同 Mesh、子网格索引区间、Material、Pass、队列、最终渲染状态与 receiveShadows；每批最多 65536 实例。
- 动态/静态展开批不要求相同 Mesh；要求同 Material、单 Pass、队列、最终渲染状态与 receiveShadows。
- 多 Pass Renderer 始终逐对象完成全部 Pass，不进入任何合批；不改变现有粒子/显式实例对多 Pass 的限制。
- 不透明项仅在允许重排的连续区域中分组；Individual、多 Pass、特殊深度/混合状态构成屏障，不跨越屏障聚合。
- Transparent、Refraction 先按现有规则排序，只合并连续兼容项；不得为合批移动透明对象的顺序。
- 动态合批采用确定预算：每项最多 1024 个有效引用顶点；每批最多 4096 个有效引用顶点、12288 个索引，达到任一上限即拆批。
- 每项先统计去重后的索引引用顶点；只展开被引用顶点；只剩一个项的普通动态批退回 Uniform。
- ParticleSystem 保留 Instanced/DynamicBatch 显式路径；Standard 允许普通 Billboard/Mesh 的两条路径；拖尾实例仍要求 Particle。
- Expanded 不兼容时普通 Renderer 退回 Uniform；粒子显式选择不兼容路径时显示明确错误，不悄悄改成另一模式。

## 6. Ens static 与运行时约束
- Ens 新增私有 `bool isStatic=false`、`bool GetStatic() const`、`bool SetStatic(bool value)`；失败返回 false 并记录原因。
- XML Ens 新增 static 属性；缺省 false；编辑器显示 Static 复选框，支持撤销与多选。
- static 定义为世界位置、世界旋转、世界缩放运行时不变；不禁止启用、禁用、销毁或修改材质参数。
- 所有 static Ens 的祖先必须 static；设置 static=true 时先检查祖先，失败不隐式修改父级。
- static 父节点可以拥有动态子节点；设置 static=false 时若存在 static 后代则拒绝，多选事务必须整体预检后执行。
- 运行时 static 标记只读；运行时创建的普通 Ens 默认非 static；本次不增加运行时冻结/解冻 API。
- World 新增 `bool runtimeActive=false`、`void SetRuntimeActive(bool)`、`bool IsRuntimeActive() const`；暂停不解除该状态。
- Application 进入模拟和 Player 激活世界前设置 runtimeActive=true；退出模拟恢复编辑世界时清除；切换世界同步设置。
- World 内容准备阶段允许恢复序列化数据；激活前验证 static 层级和物理约束，失败拒绝激活并定位 Ens。
- Transform 三个 setter 在实际值变化前检查 runtimeActive && Ens.GetStatic()；命中则保留原值，不发送变换通知。
- World::SetParent 在拆链前检查：运行时 static 子节点禁止重挂；编辑时 static 子树只能挂到满足祖先约束的位置。
- World 新增 `bool CanChangeTransform(EnsId) const`，供 setter、层级操作和物理入口统一校验；销毁内部拆链使用私有清理入口，不触发用户移动限制。
- static Ens 禁止活动的动态/运动学刚体与角色控制器驱动；配置校验和物理创建入口拒绝组合，静态碰撞体允许。
- 所有物理回写在写入前校验；拒绝时不推进对应动态 actor，以防物理位置与显示位置分离；错误按对象与原因去重。
- 编辑器 Play 与暂停期间禁用 static Ens 的变换、重挂和 Static 编辑；原生 setter 校验为最终保护，不能只锁 UI。

## 7. 静态几何缓存
- 新增 Rendering/StaticBatchCache.h/.cpp，由 ForwardPipeline 持有；编辑模式不生成静态批，运行时每帧 PrepareFrame 前刷新脏分组。
- 新增 StaticBatchMember：owner、rendererId、subMeshIndex、sourceMeshId、model、worldBounds、vertexStart/count、indexStart/count。
- 新增 StaticBatchGroup：兼容键、成员列表、Expanded 顶点/索引 CPU 数据、GPU VBO/IBO/VAO、容量与 revision。
- 静态批只吸收 static+Auto、单 Pass、supportsExpandedGeometry、Opaque、DepthTest On、DepthWrite On、Blend Off 的 Mesh 项。
- Transparent/Refraction 不加入持久静态批，按 Auto 后续实例/动态/单绘制流程执行，保留逐相机排序。
- 分组键含 Material、Pass、最终状态、receiveShadows、drawLayer、castShadows；组内按 rendererId、subMeshIndex 排序。
- 每组最多 65536 展开顶点、196608 索引；超限拆组；单个超限项或不足两个成员不静态合批，继续 Auto 后续路径。
- `Refresh(World&, GpuResourceManager&)` 收集候选、比对成员/网格内容版本/材质与 Shader 状态/变换，重建受影响组并删除空组。
- 材质颜色、贴图值变化不要求重建几何；影响分组的资源身份、Pass、队列、状态变化必须重新分组。
- `BuildGroup(...)` 调用统一展开函数，一次上传 GPU；上传失败撤销该组映射，本帧源对象继续普通 Auto 选择。
- `CollectVisibleRanges(...)` 按原成员 AABB 和 drawLayer 裁剪，仅合并索引连续且可见的范围，保留原对象独立裁剪。
- 不为每台相机重新上传静态顶点；分离的可见索引范围分别 DrawIndexed；单个可见成员仍可使用缓存 Expanded。
- `Release()` 在 World 替换、内容根切换、Shutdown 时释放 GPU 资源与非拥有引用，禁止留下已销毁 Renderer 指针。
- Renderer 删除、禁用、Mesh 替换、资源热更新在下一次绘制前刷新；CPU 检查不得仅依赖 Transform 脏通知。

## 8. 公共展开、提交与阴影
- 新增 Rendering/GeometryExpander.h/.cpp，定义 `bool AppendExpandedMesh(const Mesh&, uint32 subMeshIndex, const matrix4x4&, const color& tint, const color& uvRect, List<GpuExpandedVertex>&, List<uint32>&)`。
- 函数校验索引、矩阵与容量，构建引用顶点映射，追加转换后的顶点与重定位索引；失败不留下部分追加数据。
- 网格粒子、普通动态合批、静态缓存调用此函数；Billboard 和拖尾保持自身几何生成，统一输出 GpuExpandedVertex。
- DrawItem 携带 drawStrategy、静态组句柄/索引范围；DrawBatch 新增 `bool persistentGeometry=false` 及静态组句柄。
- DrawBatchBuilder 将静态缓存项与剩余项分开处理；实例/动态键比较独立实现，禁止继续用必须相同 Mesh 的键筛选动态批。
- ExecuteExpandedBatch 对动态批执行展开上传；对持久批绑定缓存 VAO 并绘制范围；二者共用材质状态与 Expanded Program。
- 每帧 CPU 展开结果按批保存，在该帧多相机/阴影输入相同时复用；键包含有序成员、几何版本、变换、tint 和 UV。
- CSM 从完整投影候选集单独裁剪静态成员，不能复用主相机可见范围；遵守 castShadows、layer、Opaque 与当前阴影轮廓语义。
- Shadow Expanded 不可用则从保留的源成员逐对象 Uniform 绘制阴影；这属于模式能力处理，不恢复 Legacy 类别。
- 不合并跨对象的多 Pass 阴影语义；选中描边继续从原 Renderer 与源 Mesh 绘制，静态缓存不替换对象身份。

## 9. 导入、序列化、编辑器与升级
- AssetPipeline::ParseShaderGeometryContract 删除 legacy 分支，添加 expandedGeometry 解析与重复/位置校验。
- GpuShaderPass 同步支持标志与变体有效性检查；释放函数只释放实际创建的 Program，禁止句柄拥有权别名。
- Cooked Shader Pass 写入 supportsExpandedGeometry；BlobFormatTag 从 3 升到 4，旧 Cooked 全部拒读并重新导入。
- WorldSerializer 在组件读取前恢复 isStatic，整棵树读取完成后校验；场景克隆、Play 快照、撤销恢复保存此属性。
- 版本 27 的一次性源场景迁移：enableInstancing=true/缺省→drawStrategy=Auto，false→Individual；迁移后删除旧字段。
- 不保留 enableInstancing 运行时别名；重新生成原生反射、C# Binding、SDK 与场景编辑字段描述。
- Inspector 提供策略下拉框与实际路径/未合批原因；static+DynamicBatching 提示成本，但不自动改策略。
- Shader Inspector 显示展开兼容能力；Material Inspector 不增加合批控制。
- 项目版本从 26 升到 27，BuildAndPackaging.md 记录场景迁移、Shader 接口迁移和 Cooked 重建要求。
- 旧项目自定义 Shader 不自动改写 GLSL；升级报告列出未接入接口的资源并要求作者迁移；仓库模板逐项迁移后同步项目模板发布流程。
- 更新 RenderingPipeline.md，明确 StaticMeshRenderer 的类名不表示 Ens 已 static；新增文件加入 vcxproj 与 filters。

## 10. 实施顺序与验收
- 阶段 A：Shader 契约/导入/Cooked/全部模板迁移；验证 Uniform、Instanced、Expanded 图像一致，Legacy 声明明确失败。
- 阶段 B：DrawStrategy、公共展开、普通动态合批、粒子兼容检查；验证不同 Mesh 同材质可融合，超预算回到单绘制。
- 阶段 C：Ens static、序列化、运行态与物理保护、Inspector；覆盖父级、重挂、暂停、销毁、加载失败和撤销。
- 阶段 D：持久静态缓存、逐成员裁剪、资源失效、多相机与 CSM；覆盖部分成员可见、禁用、删除和 Mesh 热更新。
- 阶段 E：版本升级、生成绑定、构建 Editor/Player；验证旧场景 true/false 开关迁移以及新旧 Cooked 边界。
- 图像用例：PBS/Blinn/透明/折射、非均匀缩放、粒子 tint/图集、拖尾两路径、多 Pass、透明交错与阴影级联。
- 性能记录 CPU 合批/展开耗时、Draw 数、上传字节、静态显存、变体编译时间；比较同场景 Individual/Auto/Instancing/DynamicBatching。
- 单绘制回归只测 Uniform：不得绑定实例缓冲，不得引入几何模式运行时分支；不把减少 Draw 数单独当作性能成功判据。
- 失败用例必须保留可绘制对象或给出明确错误，不允许缺变体、缓存上传失败、非法 static 修改造成静默消失。
