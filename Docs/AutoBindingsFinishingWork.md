# Auto Bindings 收尾工作记录

主体工作（MetaGen 自动生成全部 Object 派生类的 C# Binding）已提交，本文记录其后的收尾工作：提交划分、对主体代码的修改及原因、新增回归验证。总体计划见 [PLAN.md](PLAN.md)，验收清单见 [AutoBindingsTODO.md](AutoBindingsTODO.md)。

## 提交划分

| 提交 | 内容 |
| --- | --- |
| `metagen auto bindings` | 主体工作：MetaGen 生成器、公共 Binding 运行时、组件/资源迁移、构建链路、模板更新。含重新生成的 `OrbedenCore/Src/Runtime/Generated/*`（此前磁盘上的 `Reflection.Generated.cpp` 已过期，仍引用已删除的 `ScriptBehaviour.h`，构建前必须重新生成）。`Tests/` 按用户要求加入 `.gitignore` 并从版本控制移除。 |
| `dev: auto bindings finishing work` | 收尾工作第一批：见下文。 |

收尾期间继续产生的修改会形成后续提交，由用户决定提交 message。测试代码保持在本地 `Tests/` 目录，不进入版本控制。

## 对主体代码的修改及原因

### Core 运行时（Object.h / Object.cpp / World.h / World.cpp）

**问题**：真实 DLL 重载回归（用 Editor 的 `NativeGameModule` 加载/卸载游戏 DLL）暴露：模块卸载只注销了类型表和函数表，但 World 的 `componentStorages` 里仍保留该模块类型的空 `ComponentStorage`，其中的 `componentType` 指针在 DLL 卸载后悬垂。重载时新类型可能复用同一 `TypeRuntimeId` 槽位，恰好取回旧存储（靠 DLL 基地址复用侥幸工作）；世界销毁时 `~ComponentStorage` 解引用悬垂指针，进程以 0xC0000005 崩溃。

**修改**：
- `Object` 增加 World 注册表（`RegisterWorld` / `UnregisterWorld`），`World` 构造/析构时注册/注销。
- `Object::UnregisterModuleTypes` 在注销类型前，清空各 World 中属于该模块的空组件存储。
- `World::GetOrCreateComponentStorage` 增加防御：槽位里存储的类型与请求类型不一致时重建空存储，防止类型槽位复用命中旧存储。

### MetaGen（BindingModel / BindingGenerator / CppDeclarations / Program）

均为补齐「生成器边界与诊断」验收项（跨模块继承、删除/重命名、不支持签名审计）：

- 支持重复 `--import` 导入多个清单，并按来源记录每个导入类型的托管命名空间；跨模块多层继承（游戏模块派生自另一游戏模块的类型）据此生成正确的 C# 基类引用。
- 修复托管命名空间重复 bug：C++ 类型 `ModA::Base` 此前映射为 `ModA.ModA.Base`。现在 C++ 根命名空间与模块短名一致时并入托管根命名空间（与 Core 的 `Orbeden::` 处理一致），其余子命名空间保留。
- 新增显式诊断（均带文件:行号:成员）：
  - 未声明 `OBJECT_TYPE_DECLARE` 的 Object 派生类；
  - 固定 C 数组字段（提示改用 `List<>` 或 `ORBEDEN_BIND_IGNORE`）；
  - 非 blittable 值结构的指针缓冲区（`ORBEDEN_BIND_BUFFER` 只接受标量/枚举/blittable 结构）；
  - 重载默认参数产生相同托管签名；
  - 同名原生类型（运行时按短名解析包装工厂）及映射后托管类名冲突。
- 反射侧的字段/回调诊断补充行号。

### 测试夹具

`Tests/` 不进入版本控制，文件仅存在于本地：

- `Tests/BindingRegression/ReloadModule`：可重载游戏模块（`ReloadProbe`，用 `MODULE_RELOAD_VERSION` 宏编译出行为不同的 v1/v2）。
- `Tests/BindingRegression/BaseModule`：独立生成清单的基础模块（`BaseProbe`），由 ReloadModule 双重导入，验证跨模块继承。
- `Tests/BindingRegression/Native/BindingBridge.cpp`：桥接 DLL，直接编译 Editor 的 `NativeGameModule.cpp` 驱动真实加载/卸载。
- `Tests/RunBindingRegression.ps1`：串行生成三个模块、双进程并发生成校验、编译、运行。
- `Tests/MetaGenRegression`：词法模型与生成器边界回归。

## 已通过的验证

- 真实 DLL 重载：shadow copy 版本化、活对象阻止卸载/重载、清空后注销类型与函数表、新模块重绑（行为差异可观察）、旧包装拒绝调用、失败回滚恢复旧模块、跨模块基类查询保持包装身份。
- 生成器边界：跨模块多层继承、删除/重命名产物同步、并发生成与串行结果一致、各不支持签名均有定位诊断。

## 已就绪、待运行的验证（测试代码在本地 Tests/，不入库）

`Tests/RunBindingRegression.ps1` 为唯一入口，一次运行覆盖：

- **组件业务（第 3 项）**：Transform 局部/世界变换与监听通知、父子层级；RigidBody 全部属性；碰撞体公共属性与五种几何类型、Mesh 碰撞体引用；CharacterController 形状参数；WheelCollider 悬挂与转向参数、IsGrounded/GetCompression；HeightField 参数变更触发重建（generation 递增）、高度采样重建与双线性采样。
- **资源行为（第 4 项）**：Mesh 索引缓冲上传/快照/脏标记、RefreshNormals、子网格数组写入/快照/配置/范围校验拒绝；Material 纹理槽设置/查询/清除、颜色槽、Shader 引用；Shader.CreateFromSource 的 Pass 反射、ReplaceSource 与脏标记、Pass 快照；Texture2D 像素快照；Skybox 六个面引用。
- **Script 边界（第 5 项）**：托管脚本继承（基类与派生回调都执行）、回调异常按脚本隔离、回调内 AddComponent 延迟到下一帧、回调内销毁延迟处理且 End 只执行一次、可回收程序集卸载后旧包装断开、主程序集重新包装后的 Start/Update/End。
- 脏标记说明：Mesh 有公开 ClearDirty 可验证完整周期；Material/Shader 的清除脏标记入口私有（仅 GpuResourceManager 可调用），桥接世界无 GPU 系统，只能验证置脏侧，GPU 消费侧由发布回归覆盖。

## 发布与模板最终回归（第 6 项）

- **SDK 重同步**：重建 Core vcxproj 重新发布原生/托管 SDK、头文件与 MetaGen；SDK 发布目标新增过期头文件清理（重命名/删除后的头文件不再残留）。
- **AOT 发布**：模板项目 `dotnet publish`（win-x64、PublishAot、NativeLib=Shared、导入 AotRuntimeManifest.targets）成功。IL2104 展开后为 3 处 IL2070/IL2072（`ManagedTypeMetadataCache.Build` 的 GetMethods/GetFields、`InitializeEditorHost` 的 GetConstructor、三处 `GetType()` 传播），已用 `DynamicallyAccessedMembers` 注解消除（`Script` 基类加类级注解 + 反射入口加参数注解），重新发布 0 裁剪警告。
- **Player 构建与启动**：cmake 构建通过、LNK4098 消除；运行 15 秒，PhysX/OpenGL/双脚本域初始化成功、窗口正常关闭、152 次分配/152 次释放。
- **飞行模板回归**：30/50/60/120 Hz 全部 PASS，渲染冒烟 OpenGL error 0、地形截图生成。
- **遗留**：VS2026 更新后 ilcompiler 的 findvcvarsall.bat 在 MSBuild Exec 内失败（link 退出码 123），发布改用 `IlcUseEnvironmentalTools=true` 绕过。不在 VS 环境中启动的 Editor 走 Build Player 时可能遇到同样问题，需在 Editor 中复核。

## Editor Build Player 修复（用户实测反馈）

用户在新项目上执行 Editor 内打包时暴露两个问题：

- **preset 文件解析失败**：`cmake --preset` 从 Editor 启动目录找 CMakePresets.json，而文件在仓库根目录。修复：Player 的 configure/build 命令补 `-S <仓库根>`（`EditorSystem.cpp`），不再依赖启动目录。
- **clang-cl 缺失**：Windows Player 预设固定使用 clang-cl，本机 VS 未安装 LLVM 工具时 CMake 报编译器找不到。修复：管线检测不到捆绑 clang-cl 时给出明确提示（安装「适用于 Windows 的 C++ Clang 工具」组件后重试）。编译器方案决策：先装 clang-cl 打通打包；MinGW gcc 支持作为后续评估项（当前机器的 msys2 处于半更新状态，cc1plus 无法启动，需先修复工具链，且构建系统需加 MinGW 分支与编译器选择 UI）。

## 最终清理与文档（第 7 项）

- 旧代码审计：无 ScriptBehaviour/color4/PhysicsComponents 源码残留；SDK 头文件快照中的过期 `ScriptBehaviour.h` 已清理；Ens/Editor 无逐类型分派残留。
- 文档：ScriptSystem.md 更新生成式 Binding 现状、API 变化清单（ScriptBehaviour→Script、color4→color、静态 Load→Resources.Load\<T\>、Transform getter/setter、ABI 2 共同重建）与模板 HUD 描述；UserManual.md 更新 Transform 示例与动态代理适用范围。
- 迁移矩阵已全部落实到验收证据（见 [AutoBindingsTODO.md](AutoBindingsTODO.md)）。

## 剩余工作

仅剩 Editor 内的 Inspector 菜单/默认字段/依赖检查手验，以及 Editor Build Player 在 VS2026 更新后的发布路径复核。本文档随验收推进同步更新。

## 后续 review 打磨（不涉及项目打包）

针对 review 发现的问题，本轮只调整生成器和模块卸载逻辑：

- Object::UnregisterModuleTypes 在任何注销操作前检查其他模块的完整基类链。即使没有实例，只要外部派生类型仍注册，也拒绝卸载其基类模块；NativeGameModule 的错误提示同步说明依赖原因。
- 固定数组检查递归进入导出的值结构，报出实际所属结构及字段，不再生成将数组当标量赋值的非法代码。
- 普通值结构可作为 Span 缓冲元素：按字段编码、在原生侧还原到临时数组，不共享 C++/C# 结构内存布局。包含字符串/对象引用/隐藏字段等非支持元素仍明确拒绝。
- 修正枚举、布尔 Span 的固定指针类型，清理生成器可空分析警告。

验证：
- MetaGenRegression：包含新增嵌套结构缓冲、嵌套固定数组诊断的全部回归通过。
- 直接编译修改后的 Object.cpp 运行三层跨模块继承测试：提前卸载被拒绝，失败不改变类型表，按派生到基类顺序卸载通过。夹具保留在本地 Tests/BindingRegression/ModuleDependency.cpp。
- 独立 Binding 真实调用：结构字段、空缓冲、枚举、布尔 Span 均通过，11 次分配/11 次释放；运行入口为 dotnet run --project .tmp/binding-polish/BufferTest.csproj（需 Core/GLFW DLL 搜索路径）。
- 完整 RunBindingRegression.ps1 本次未通过：在新增缓冲测试之前的 “Transform setters roundtrip and world transform” 断言停止，详情见 .tmp/binding-polish/runtime.log。未将此记录为全套通过，Transform 问题留待单独核查。
- 未修改或运行用户正在调整的项目打包工程；未重建、发布 Core SDK，模块依赖测试直接编译源码，生成验证写入独立 .tmp 目录。

## 2026-09-10 二次复核修复

修复 MetaGen 仅扫描本模块类来识别 Script 的遗漏：反射阶段现在共享导入后的 BindingModel，跨模块派生 Script 正确注册自身生命周期，也恢复 virtual/override 的错误诊断。专项生成测试与既有 MetaGenRegression 全部通过；实际 DLL 回归夹具重新编译成功。

回归夹具补上 TransformCache 生命周期和更新点，验证父子世界变换；地形采样前也刷新缓存。Mesh 测试的三个索引原先引用了只有两个顶点的数组，已补齐第三个顶点。

完整运行仍未通过，当前失败点为可回收程序集释放断言（ReloadChecks.CreateAndUnload）。此前组件与资源断言已执行通过，后续 Script 边界和 DLL 重载本轮未执行。当前源码独立编译的托管 Core 与 Release 测试也复现；不能将本次记为全套验收通过。准确下一步和日志路径见 AutoBindingsTODO.md 的二次复核记录。未修改项目打包或发布 SDK。
