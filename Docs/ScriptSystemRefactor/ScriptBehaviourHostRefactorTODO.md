# ScriptBehaviour 原生宿主重构 TODO

更新时间：2026-09-07

> `[x]` 表示该项实现或审查已经完成。2026-09-07 用户要求：完成剩余代码和文档，只要求编译通过，不进行运行测试。编译结果在下方单独记录。

## 当前结论

主体架构及剩余功能收尾已完成：每个 C# 脚本绑定独立原生宿主，统一持久化、引用、组件事务和 Inspector。Core、Editor、全部仓库托管项目和提取出的模板 C++ / C# 模块均已编译通过；本轮不执行运行验收。

## 已完成

### 原生组件与脚本身份

- [x] 将 C++ `ScriptBehaviour` 改为可实例化、可反射创建的普通 `Component`。
- [x] 精确类型 `ScriptBehaviour` 作为 C# 脚本的原生宿主；其 C++ 派生类型作为原生脚本。
- [x] 原生宿主保存 `domain`、`enabled`、`managedTypeName` 和托管序列化字段。
- [x] 每个 C# 脚本拥有独立的原生组件实例、`ObjectId` 和组件挂载位置。
- [x] 精确 `ScriptBehaviour` 不出现在普通 `[C++]` 添加菜单中。

### C# Wrapper 与生命周期

- [x] C# `ScriptBehaviour` 改为原生 `ScriptBehaviour` 的 Wrapper，而不是独立脚本身份。
- [x] C# Wrapper 的 `InstanceId` 使用原生宿主 `ObjectId`。
- [x] 通过线程局部构造上下文，把原生宿主连接到游戏脚本的 `(Ens ens)` 构造过程。
- [x] `enabled` 只保存在原生宿主，C# 属性直接代理原生值。
- [x] 删除基于 MountId 的托管脚本身份，Runtime Registry 改为以原生 `ObjectId` 和 generation 管理实例。
- [x] C++ 与 C# 生命周期方法都采用非虚函数定义。
- [x] C++ 生命周期继续使用 MetaGen 静态 thunk；C# 生命周期在加载时解析并缓存闭合 delegate。
- [x] 保留每个阶段 C++ Domain 先执行、C# Domain 只批量进入一次的调度方式。
- [x] 原生宿主挂载、移除、启停和字段变化可以通过结构事件通知托管 Runtime。

### API 与双向互操作

- [x] `Ens.AddComponent<T>()`、`GetComponent<T>()`、`GetComponents<T>()` 已接入原生宿主模型。
- [x] 保留 C++→C# 的 Managed Proxy 字段读写和方法调用。
- [x] 保留 C#→C++ 的 Native Proxy 字段读写和方法调用。
- [x] 托管类型元数据、字段访问器和方法信息按类型缓存，不在每帧重复扫描。
- [x] 原生宿主回调 ABI 已加入创建、删除、枚举、类型、Ens、enabled 和动态字段访问入口。

### 序列化与 Inspector

- [x] C# 脚本宿主和字段改为写入 `.world`，不再生成独立脚本 sidecar。
- [x] `.world` 可以保存 `domain`、`managedTypeName`、`enabled` 和动态托管字段。
- [x] Inspector 将 C++ 与 C# 组件都映射为 World 中的组件卡片。
- [x] Inspector 使用 `[C++]` / `[C#]` 标识，并隐藏 C# 宿主对应的重复原生卡片。
- [x] Inspector 可以显示 Missing Script，并保留原生宿主及其字段数据。
- [x] C++ 与 C# 通用字段编辑已接入 `PropertyDocument` 和同一套 Undo 路径。
- [x] 多选时按类型、域和 occurrence 匹配公共组件。

### 旧机制清理与模板

- [x] 删除 `.world.scripts.json` 的运行时和 Editor 主路径。
- [x] 删除 `SidecarDirty`、MountId、独立托管挂载列表和 `ApplySerializedValues` 特殊入口。
- [x] 删除新项目模板中的 sidecar 文件生成。
- [x] 新项目模板包含简单 C# 脚本、简单 C++ 脚本和双向互操作示例。
- [x] 模板 World 直接挂载 C# 脚本的原生 `ScriptBehaviour` 宿主。
- [x] 静态搜索未发现旧 sidecar/MountId 主路径残留。
- [x] 工作区没有 `.patch`、`.rej` 或 `.orig` 残留文件。

## 剩余收尾

### P0：功能闭环

- [x] 将 C# 字段中的原生 Object 引用改为稳定的持久化身份；不能把仅本进程有效的 `ObjectId` 直接写入 `.world`。
- [x] 为托管宿主的动态 Object 引用补齐资源/实体重映射逻辑。
- [x] 修正“删除 C# 组件后 Undo”快照：必须包含 `[HideInInspector]` 等不可见但需要持久化的字段。
- [x] 组件删除/恢复时保存并恢复准确的挂载顺序，而不是统一追加到末尾。
- [x] 补齐组件添加事务中的 Unique、依赖组件和多选全有或全无语义。
- [x] 检查 Missing Script 的添加、删除、Undo、重载和重新连接流程。
- [x] 恢复 Transform、Renderer、RigidBody、Collider 等专用 Inspector，并让写入继续经过 `PropertyDocument`。
- [x] 让 Inspector 正确显示并处理 `PropertyDocument.ApplyChanges()` 失败，而不是忽略返回值。

### P1：生命周期、句柄与元数据一致性

- [x] 审查回调内添加/删除/启停的延迟生效和重入行为。
- [x] 静态审查 Start、End、禁用/重启和 Ens 销毁状态转换，补齐 End 重入保护；运行验证按用户要求不执行。
- [x] 补齐 World、原生模块和托管程序集重载后的 Wrapper、ComponentProxy 与成员句柄失效规则。
- [x] 逐项核对 CLR 和 NativeAOT 初始化时的托管函数表注册/清理顺序。
- [x] 更新 MetaGen 对新 `ScriptBehaviour` 的处理，避免把运行时内部状态误生成为持久化字段。
- [x] 重新生成 Core 反射元数据，确认可实例化工厂、字段和生命周期 thunk 没有重复注册。
- [x] 静态核对 C++/C# ABI 的字段顺序、槽位数量、Pack、枚举值和函数签名。
- [x] 完成全部改动后的语法、包含关系、空指针和 generation 使用审查。

### P2：文档与模板复核

- [x] 更新 `Docs/ScriptSystem.md`：改成“C# Wrapper 绑定原生宿主”的新模型。
- [x] 更新 `Docs/UserManual.md`：删除 sidecar 描述，补充 C++/C# 添加组件、互操作和保存方式。
- [x] 复核新项目模板的 World 层级、CMake/MetaGen 步骤、AOT 导出文件和示例代码能够互相对应。
- [x] 说明高频互操作应缓存成员句柄；动态 `Invoke(name)` 只用于低频通用调用。

## 编译与验收记录（运行测试按用户要求不执行）

- [x] 构建 `OrbedenCore.vcxproj` 并修复编译错误。
- [x] 构建 `OrbedenEditor.vcxproj` 并修复编译错误。
- [x] 构建全部托管项目，检查 CLR 编译。
- [x] 构建新项目模板的 C++ 游戏模块和 MetaGen 输出。
- [ ] 验证 Editor CLR 模式下的生命周期、序列化、双向互操作和 Inspector。
- [ ] 验证 Player NativeAOT 模式下的同一套功能。
- [ ] 验证同一 Ens 挂载多个 C++/C# 脚本以及同类型多个 C# 脚本。
- [ ] 验证 `.world` 保存/加载、Missing Script、程序集重载和句柄失效。
- [ ] 验证多选、混合值、Undo/Redo、组件增删事务和 PIE 修改丢弃。
- [ ] 检查稳定帧无反射扫描、无状态轮询、无列表重建，且 C# 每阶段只有一次域调用。
- [x] 编译完成后清理本轮临时模板工程及构建日志；保留正常工程构建产物。

## 2026-09-07 完成记录

- 完整组件 XML 快照保留隐藏字段、精确字段类型、稳定身份及挂载位置；属性历史按稳定身份定位恢复后的组件。
- Object 引用使用资源 Key / world:// 路径；组件自身稳定路径随 .world 保存。托管 EnsId 字段使用 Ens 稳定路径。
- 多选添加预检 Unique 和依赖环，创建失败回滚；运行时添加也回滚新增依赖。C++ / C# 创建域显式传入，避免同名类型误匹配。
- 专用 Inspector 恢复字段排序、资源选择、枚举、旋转和颜色编辑；写入与历史经过 PropertyDocument，失败不丢弃历史记录。
- 补齐生命周期重入保护、失效 Wrapper 断开、事件驱动调度状态及真实构造上下文中的 Editor 默认字段初始化。
- World 清空保留 Ens 槽位版本，防止旧 EnsId 命中新对象；组件查询和阶段列表遵循恢复后的挂载顺序。
- MetaGen 排除基类身份和运行状态字段，ScriptBehaviour.enabled 使用手写反射注册；已重新生成 Core 元数据。
- 修复包含缺失、World 类型名遮蔽、EnsId 解析分支错误，以及导出表扫描过期 .obj 导致的链接错误。
- 新模板的 C++→C# 方法解析延迟到首次 Update，确保托管域已初始化；模板 C# 生命周期改为 private，消除 sealed 类型的 protected 成员编译警告。
- ScriptSystem.md 已按新模型重写；UserManual.md 已补充宿主、事务、保存、互操作和重载说明。

### 编译结果

| 目标 | 配置 | 结果 |
| --- | --- | --- |
| OrbedenCore.vcxproj | Debug / x64 | 通过，MSBuild 退出码 0 |
| OrbedenEditor.vcxproj | Debug / x64 | 通过，MSBuild 退出码 0 |
| OrbedenCore.CSharp | net10.0 | 通过 |
| Orbeden.Editor | net10.0 | 通过 |
| OrbedenMetaGen | net10.0 | 通过，Core 和模板生成成功 |
| 模板 C# 脚本及 AOT 导出源码 | net10.0 CLR 编译 | 通过，0 警告 / 0 错误 |
| 模板 C++ 模块及 MetaGen 输出 | MSVC / Debug / x64 | 通过，生成游戏 DLL |

原生 Debug 链接过程中仍有第三方 PhysX 缺少 PDB 的 LNK4099 警告，不影响编译和链接成功。本轮没有执行 NativeAOT 发布/Player 链接、Editor/Player 运行或性能测试；上方运行验收项保留未勾选，原因是用户明确要求只编译、不做运行测试。

本轮创建的 Build/ScriptRefactorTemplate 临时工程、其构建目录和 ScriptRefactor*.log 已清理；正常 Core/Editor/SDK 构建产物保留。