# Auto Bindings 实施清单

## 目标与约定

所有 Orbeden::Object 派生类生成 C++ 类型化调用入口、C# partial 包装及注册信息。默认导出 public 成员，显式排除内部实现；C# 命名跟随 C++。序列化与 Binding 独立。GUI、输入等非 Object API 不迁移。已迁移类型删除旧绑定，不设 fallback。

## 阶段

- [ ] 0：清点全部 Object 派生类型、手写 Binding 和业务副作用，记录基线。
- [ ] 1：公共词法模型、导出声明、类型转换、C++/C# 生成和测试。
- [ ] 2：公共 Binding ABI、类型工厂、包装身份、Script 域分类和失效机制。
- [ ] 3：全部组件迁移，删除逐类型创建、查询和绑定。
- [ ] 4：全部资源迁移，保留数组、引用、资源所有权和脏标记。
- [ ] 5：Core、游戏模块、Editor 与 AOT 构建前生成，生成产物清理和签名验证。
- [ ] 6：调用方、模板、文档、旧代码清理及完整验收。

## 已完成子项与剩余工作

勾选表示该子项已完成所列验证；阶段总项需等该阶段全部验收后再勾选。编译通过不等于运行时行为已经验收。

### 阶段 0：清点与基线

- [x] 保留既有 Object/Script 重命名、脚本生命周期简化及工作区修改；未回退用户代码。
- [x] 扫描 Core 的 22 个 Object 类型，建立下方类型迁移矩阵；词法扫描回归通过。
- [x] 清点原有组件/资源包装和对应原生函数表，记录 Transform 通知、Mesh/Material/Shader 脏标记、运行时 Shader 创建与资源 Key 行为。
- [ ] 完成全部公开业务 API 的逐成员对照，确认无遗漏；游戏模板类型已导入并构建，仍需逐成员核对。

### 阶段 1：生成器

- [x] 公共词法模型替换旧反射声明正则；继承、命名空间、访问级别、重载、默认参数、序列化标记分离回归通过。
- [x] 生成 Core 22 个类型的 C++ 类型化入口、C# partial 包装和注册清单；真实 Core 原生/托管编译通过。
- [x] 实现数值、字符串、枚举、值结构、Object/Ref、数组快照、Span 缓冲的转换与生成；Core 编译及数值、数学类型、UTF-8、结构数组、Object 引用和 Span 的真实传输回归通过；其他边界类型待补齐。
- [x] 加入递归结构字段、枚举和 accessor 的签名校验；结构布局和枚举值变化导致签名变化的回归通过。
- [x] 无变化时不重写 C++/C#/manifest；生成文本及文件时间戳重复运行回归通过。
- [x] 添加按输出目录的互斥锁；生成器编译通过。
- [ ] 补齐跨模块继承、删除/重命名、错误定位、不支持签名、并发生成的回归。
- [x] 写入通知和唯一组件元数据解析回归、Core 原生/托管编译通过；唯一组件重复添加的真实回归通过。

### 阶段 2：公共运行时

- [x] 接入公共 Binding API、动态 TypeId 解析和缓存函数指针；Core 原生/托管编译通过。
- [x] 接入通用对象创建、组件创建/查询、资源加载及实际类型工厂，删除 Ens 的逐类型分派；Core 编译通过。
- [x] Script 扫描与创建改用执行域判断，排除生成包装并拒绝托管继承原生包装；Core/Editor 托管编译通过。
- [x] 接入 generation 检查及程序集卸载注册引用释放；Core/Editor 托管编译通过。
- [x] 真实 DLL 回归验证 Collider 派生查询/多实例/销毁、原生 Script 独立 TypeId、包装不进托管调度、托管 Start/End 与启用/禁用；原生完整调度仍需模板验收。
- [x] 实测多实例、基类查询身份、多程序集工厂选择、旧句柄拒绝调用。
- [x] 可回收 CLR 程序集卸载回归通过：断开旧包装、保留工厂重新包装原生对象、GC 回收旧加载上下文。
- [ ] 实际 Native DLL 卸载/重载回归；当前 generation 测试通过注销 Binding 模拟失效，未执行 DLL 卸载。
- [ ] 托管继承、异常、回调重入、回调中销毁、重载后的生命周期边界回归。

### 阶段 3：组件迁移

- [x] 删除 Core 手写组件包装、组件专属 ABI 表和原生分派；生成版本已通过 Core 编译。
- [x] Transform 显式调用原有变换 setter，补充层级/世界变换接口；Core 原生编译通过。
- [x] HeightField 显式写入通知、唯一组件标记和 RigidBody 内部力缓存排除已生成并通过 Core 编译；移除反射生成器按 HeightField 类型名猜测通知的分支。
- [ ] 实测变换通知、物理属性、碰撞体引用、地形与悬挂行为。

### 阶段 4：资源迁移

- [x] 删除 Core 手写资源包装及 Mesh/Material/Shader 专属函数表；生成版本已通过 Core 编译。
- [x] Mesh 数组使用原 setter，新增验证子网格范围并标脏的 SetSubMeshes；Core 原生编译通过。
- [x] Material/Shader 使用显式 setter 或只读快照标记；Core 原生编译通过。
- [x] Shader.CreateFromSource 业务移入 C++ Shader，通用加载保留运行时资源 Key 查找；最新 Core 原生构建通过。
- [ ] 逐 API 核对资源业务，实测数组复制、引用解析、脏标记、加载与回收。

### 阶段 5：构建与模块

- [x] Core vcxproj 与托管 Core 独立构建前运行生成；两个入口已构建通过。
- [x] 根 CMake、游戏 CMake/C#、SDK manifest 发布和 AOT 发布顺序已接通；真实游戏 DLL、独立 C#、NativeAOT 与 Player 构建通过。
- [x] 游戏模块注册入口接入 Binding，Core 清单导入和 Native 子命名空间通过真实飞行模板生成及 C++/C# 编译；ABI 升至 2，表头/尺寸/偏移校验通过旧 ABI 拒绝回归。
- [ ] 验证新增/删除类型无需修改中央代码，多个构建入口无产物竞争。

### 阶段 6：调用方和验收

- [x] Editor 资源加载迁移到 Resources.Load，移除旧物理类名映射，Script enabled 调用迁移到 GetEnabled/SetEnabled；托管 Editor 编译通过。
- [x] C# color4 调用方迁移到与 C++ 一致的 color；Core/Editor 托管编译通过。
- [x] 原生及托管 Editor 构建通过；真实 Binding 回归通过；HUD 改为 Native.FlightController 直接调用，完整模板独立 C# 构建通过。
- [x] Windows x64 NativeAOT Player 构建与启动通过：物理、OpenGL、C++/AOT C# 脚本域初始化成功，窗口正常关闭，退出码 0。
- [ ] 展开并核查 IL2104 裁剪警告和 LNK4098 C 运行库冲突；确认不会影响发布。
- [ ] 最终状态下重新发布并运行 Player：本次成功的 AOT 产物早于最后的多程序集工厂/CLR 卸载修复。
- [ ] 最终旧代码审计、用户文档/API 命名变更、外部项目重建说明。

## 类型迁移矩阵

| 类型 | 关键行为 | 状态 |
| --- | --- | --- |
| Object / Component / Script | 身份缓存、所有权、构造和生命周期域 | 创建/身份/脚本域/CLR 卸载通过；DLL 重载与生命周期边界待验收 |
| TransformComponent | 本地变换 setter、层级与缓存通知 | 已生成，待业务与运行验收 |
| StaticMeshRenderer / Camera / DirectionalLight | 资源引用、渲染状态 | 已生成，待业务与运行验收 |
| RigidBodyComponent | 力累积、物理属性 | 已生成，待业务与运行验收 |
| ColliderComponent 及五个派生类 | 几何类型、材质与 Mesh 引用 | Box/Sphere 创建、基类查询和销毁通过；其余物理行为待验收 |
| CharacterControllerComponent | 物理移动、形状参数 | 已生成，待业务与运行验收 |
| HeightFieldComponent / WheelColliderComponent | 生成地形与悬挂 | 已生成，待业务与运行验收 |
| Mesh | 顶点与索引缓冲、子网格、脏标记 | 顶点 Span、快照/写回、运行时资源 Key 通过；索引/子网格/回收待验收 |
| Material | 参数槽、Shader/Texture 引用、脏标记 | 已生成，待业务与运行验收 |
| Shader | 源码、Pass、槽反射、脏标记 | 已生成，待业务与运行验收 |
| Texture2D / Skybox | 像素、资源引用、加载 | 已生成，待业务与运行验收 |
| 游戏模块 Object 派生类 | 外部基类、模块身份、生成包装与原生生命周期 | 飞行模板 C#/DLL/AOT/Player 通过；实际 DLL 重载待验收 |

## 验证记录

实施前：工作区包含已验证的 Object/Script 重命名及脚本清理重构，继续保留。此前 Core/Editor、类型身份与飞行模板 30/50/60/120 Hz 回归通过；不代表本次自动 Binding 已验收。

计划验证：生成器确定性和诊断；新增/删除类型；跨模块继承；值类型、字符串、引用与缓冲区；创建/查询/删除；Script 域；资源副作用；模块卸载和过期包装；Core/Editor；Windows x64 NativeAOT Player 构建与启动。

## 当前进度与恢复入口

最后更新：2026-09-09。主要生成、运行时和构建链路已打通，尚未完成所有类型的逐业务验收。阶段总项暂不勾选。

### 最新验证结果

| 验证 | 命令/入口 | 结果与日志 |
| --- | --- | --- |
| 生成器 | dotnet run --project Tests/MetaGenRegression/MetaGenRegression.csproj -- OrbedenCore/Src | 22 个 Core 类型、词法/元数据、确定性、时间戳、默认重载、私有字段隔离、递归结构/枚举签名变化通过 |
| Core / Editor | MSBuild 对应 vcxproj，Debug / x64；dotnet build 对应 csproj | 原生/托管均通过；.tmp/core-autobindings-native.log、.tmp/editor-autobindings-native.log；最新托管修复见 .tmp/core-autobindings-managed.log |
| 真实 Binding | Tests/RunBindingRegression.ps1 | ABI、派生身份、多实例、唯一组件、Span/数组快照、结构数组、UTF-8/Object 引用、运行时 Key、脚本域、Start/End、启用/禁用、删除和 generation 拒绝调用通过；.tmp/binding-regression.log |
| CLR 卸载扩展回归 | dotnet run --project Tests/BindingRegression/BindingRegression.csproj -- .tmp/binding-regression/BindingBridge.dll（需 Core/GLFW DLL 路径） | 最新测试包括多程序集工厂、旧包装断开、重新包装、可回收加载上下文 GC；.tmp/binding-runtime.log。31 次分配、31 次释放 |
| 模板独立 C# | dotnet build .tmp/AutoBindingsFlight/Script/AutoBindingsFlight.csproj | 自动生成 Native 包装及 HUD 编译通过，0 警告/错误；.tmp/flight-standalone-managed.log |
| 游戏原生 DLL | cmake --build .tmp/AutoBindingsFlight/Native/Build --config Debug | 正式模板 CMake 生成及链接通过；.tmp/flight-native-cmake.log |
| Windows NativeAOT | dotnet restore/publish 模板项目，win-x64、PublishAot=true、NativeLib=Shared，并导入 AotRuntimeManifest.targets | 发布成功；.tmp/flight-aot.log；有 IL2104 待核查 |
| Player 构建 | cmake --build .tmp/AutoBindingsFlight/PlayerBuild --config Debug | Core 静态库、游戏原生 Binding 与 AOT 链接成功；.tmp/flight-player.log；有 LNK4098 待核查 |
| Player 启动 | .tmp/AutoBindingsFlight/PlayerBuild/bin/Debug/OrbedenGame.exe | 运行约 15 秒，物理/OpenGL/两种脚本域初始化成功，正常关闭、退出码 0；.tmp/player-startup.stdout.log、.tmp/player-startup.stderr.log。152 次分配、152 次释放 |

NativeAOT 还原说明：仓库 NuGet.config 清空了源，首次还原缺少 .NET 10.0.12 AOT 包。已用官方 NuGet 源显式还原成功；正式发布流程不再强制清空 RestoreSources。仓库 NuGet.config 未改动，新机器仍需要配置可用源或预装对应包。

### 剩余工作（按继续实施顺序）

- [ ] **1. 实际原生模块重载。** 用 Editor 的 NativeGameModule 加载/卸载真实 DLL，验证活对象阻止卸载、清空后注销类型/函数表、新模块重新绑定、旧包装拒绝调用。不要以当前模拟 generation 失效测试代替。
- [ ] **2. 生成器边界与诊断。** 补齐跨模块多层继承、删除/重命名、并发构建和过期产物回归；审计不支持的公开声明是否都有文件/成员/原因。特别核对固定数组、非 blittable 值结构的指针缓冲、未声明反射宏的 Object 派生类、同名命名空间类型和重载默认参数冲突。
- [ ] **3. 全部业务 API 逐成员对照。** 对照旧 Binding 与生成 API，补齐遗漏能力；完成 Transform 通知、全部物理组件属性、HeightField 重建、WheelCollider 悬挂的运行验证。
- [ ] **4. 全部资源行为验收。** 覆盖 Mesh 索引/子网格、Material/Shader 脏标记和源码/Pass 更新、Texture2D/Skybox、对象引用加载及回收。现有快照和资源 Key 回归只覆盖其中一部分。
- [ ] **5. Script 边界。** 补齐手写托管脚本继承、异常、重入、回调内销毁和重载后的 Start/End；完成 Inspector 默认字段/菜单/依赖检查实际验收。
- [ ] **6. 发布与模板最终回归。** 核查 IL2104、LNK4098；在最新代码下重新同步 SDK、发布 AOT、构建并启动 Player，重跑飞行模板 30/50/60/120 Hz 与渲染回归。此前飞行数值回归不能代表本次迁移后的验收。
- [ ] **7. 最终清理与文档。** 审计并删除剩余已替代的绑定/分派/辅助代码；更新 UserManual、ScriptSystem、构建文档，列出 Object/Script、组件全名、color、Transform getter/setter 等 API 变化，明确 ABI 2 要求 Core/Editor/外部游戏共同重建。将迁移矩阵全部落实到实际验收证据。

准确下一步：先增加真实 Native DLL 重载测试，再处理生成器边界和逐类型业务遗漏。没有需要用户决策的阻塞；缺失的是上述实现/验收工作，不应标记整个计划完成。