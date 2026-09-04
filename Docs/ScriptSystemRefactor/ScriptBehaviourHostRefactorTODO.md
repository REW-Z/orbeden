# ScriptBehaviour 原生宿主重构 TODO

更新时间：2026-09-04

> `[x]` 表示功能代码已经写入工作区，不代表已经通过构建和运行测试。按约定，当前尚未开始构建和测试；等确认后再执行。

## 当前结论

主体架构已经改成“每个 C# 脚本绑定一个可实例化的原生 `ScriptBehaviour` 组件”。核心调用路径已经接通，但仍有若干正确性和 Editor 收尾项，因此目前不能算全部完成。

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

## 明天优先收尾

### P0：功能闭环

- [ ] 将 C# 字段中的原生 Object 引用改为稳定的持久化身份；不能把仅本进程有效的 `ObjectId` 直接写入 `.world`。
- [ ] 为托管宿主的动态 Object 引用补齐资源/实体重映射逻辑。
- [ ] 修正“删除 C# 组件后 Undo”快照：必须包含 `[HideInInspector]` 等不可见但需要持久化的字段。
- [ ] 组件删除/恢复时保存并恢复准确的挂载顺序，而不是统一追加到末尾。
- [ ] 补齐组件添加事务中的 Unique、依赖组件和多选全有或全无语义。
- [ ] 检查 Missing Script 的添加、删除、Undo、重载和重新连接流程。
- [ ] 恢复 Transform、Renderer、RigidBody、Collider 等专用 Inspector，并让写入继续经过 `PropertyDocument`。
- [ ] 让 Inspector 正确显示并处理 `PropertyDocument.ApplyChanges()` 失败，而不是忽略返回值。

### P1：生命周期、句柄与元数据一致性

- [ ] 审查回调内添加/删除/启停的延迟生效和重入行为。
- [ ] 验证 Start、End、禁用/重启以及 Ens 销毁时的状态转换，确保 End 恰好一次。
- [ ] 补齐 World、原生模块和托管程序集重载后的 Wrapper、ComponentProxy 与成员句柄失效规则。
- [ ] 逐项核对 CLR 和 NativeAOT 初始化时的托管函数表注册/清理顺序。
- [ ] 更新 MetaGen 对新 `ScriptBehaviour` 的处理，避免把运行时内部状态误生成为持久化字段。
- [ ] 重新生成 Core 反射元数据，确认可实例化工厂、字段和生命周期 thunk 没有重复注册。
- [ ] 静态核对 C++/C# ABI 的字段顺序、槽位数量、Pack、枚举值和函数签名。
- [ ] 完成全部改动后的语法、包含关系、空指针和 generation 使用审查。

### P2：文档与模板复核

- [ ] 更新 `Docs/ScriptSystem.md`：改成“C# Wrapper 绑定原生宿主”的新模型。
- [ ] 更新 `Docs/UserManual.md`：删除 sidecar 描述，补充 C++/C# 添加组件、互操作和保存方式。
- [ ] 复核新项目模板的 World 层级、CMake/MetaGen 步骤、AOT 导出文件和示例代码能够互相对应。
- [ ] 说明高频互操作应缓存成员句柄；动态 `Invoke(name)` 只用于低频通用调用。

## 等确认后再做：构建与测试

- [ ] 构建 `OrbedenCore.vcxproj` 并修复编译错误。
- [ ] 构建 `OrbedenEditor.vcxproj` 并修复编译错误。
- [ ] 构建全部托管项目，检查 CLR 编译。
- [ ] 构建新项目模板的 C++ 游戏模块和 MetaGen 输出。
- [ ] 验证 Editor CLR 模式下的生命周期、序列化、双向互操作和 Inspector。
- [ ] 验证 Player NativeAOT 模式下的同一套功能。
- [ ] 验证同一 Ens 挂载多个 C++/C# 脚本以及同类型多个 C# 脚本。
- [ ] 验证 `.world` 保存/加载、Missing Script、程序集重载和句柄失效。
- [ ] 验证多选、混合值、Undo/Redo、组件增删事务和 PIE 修改丢弃。
- [ ] 检查稳定帧无反射扫描、无状态轮询、无列表重建，且 C# 每阶段只有一次域调用。
- [ ] 测试结束后清理临时工程、构建目录、生成日志和其他临时产物。

## 明天建议的执行顺序

1. 先修复 Object 引用持久化和 Inspector 删除/恢复快照，这是当前最明确的数据正确性风险。
2. 再收紧生命周期、重载失效和 ABI/MetaGen 一致性。
3. 恢复专用 Inspector，复核模板并更新两份用户文档。
4. 功能代码确认闭环后暂停，等待明确许可再开始构建和测试。
