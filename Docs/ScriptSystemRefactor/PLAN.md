# C# 脚本绑定可实例化的原生 ScriptBehaviour

## 总结

原生 `Orbeden::ScriptBehaviour` 改成可实例化的非抽象 `Component`：

```text
C++ 脚本：
CharacterComponent(C++)
  └─ 继承 ScriptBehaviour(C++)
       └─ Component(C++)

C# 脚本：
NpcAiComponent(C#)
  └─ 继承 ScriptBehaviour(C# Wrapper)
       ⇄ 绑定一个 ScriptBehaviour(C++) 实例
            └─ Component(C++)
```

精确类型为 `ScriptBehaviour` 的原生实例用于承载 C# 脚本；原生派生类型用于 C++ 脚本。由于目前没有实例工程，直接采用新格式，不实现任何旧 sidecar、旧脚本身份或旧序列化兼容。

## 原生组件模型

- 将 C++ `ScriptBehaviour` 从 `OBJECT_TYPE_DECLARE_ABSTRACT` 改为普通可构造、可反射创建的类型。
- 不声明虚生命周期函数；基类自身没有生命周期方法也完全合法。
- 增加：
  - `ScriptDomain domain`
  - `bool enabled`
  - `std::string managedTypeName`
  - C# 序列化字段表
- 域规则固定为：
  - 精确 `ScriptBehaviour` 实例：`Managed`
  - `ScriptBehaviour` 的 C++ 派生实例：`Native`
- `domain` 对外只读，由实际原生类型决定；序列化时写入并在读取时校验。
- 精确 `ScriptBehaviour` 不出现在普通 `[C++]` 添加菜单中，只在选择 C# 类型时由 Editor/Runtime 创建。
- 未配置 `managedTypeName` 的精确实例属于无效 Managed 宿主，不进入生命周期调度。
- 每个 C# 脚本拥有独立的原生宿主、`ObjectId` 和挂载位置。
- 所有 C# 脚本共享原生 `ScriptBehaviour::TypeId`；具体类型由 `managedTypeName` 区分。

## C# Wrapper 与生命周期

- C# `ScriptBehaviour` 改成原生 `ScriptBehaviour` 的真正 Binding/Wrapper。
- Runtime 创建 C# 脚本的顺序：
  1. 创建或找到原生 `ScriptBehaviour`。
  2. 配置 `managedTypeName` 和序列化字段。
  3. 将原生指针放入线程局部构造上下文。
  4. 调用游戏脚本 `(Ens ens)` 构造函数。
  5. C# 基类从上下文取得指针并连接原生对象。
- C# 派生类构造期间即可安全访问 `InstanceId`、`Ens` 和 `enabled`。
- 没有原生构造上下文时直接 `new` C# 脚本抛出异常。
- `enabled` 只存储在原生宿主中，C# 属性直接代理原生值。
- `ScriptRuntimeRegistry` 使用原生 `ObjectId` 建立映射，删除 MountId 和独立托管身份。
- 生命周期仍采用预解析回调：
  - C++：具体类型静态 thunk。
  - C#：缓存的闭合 delegate。
  - 两端都不使用生命周期虚函数。
- 每阶段仍固定执行 C++ Domain，再一次性进入 C# Domain。
- 原生宿主的挂载、移除、启停和销毁通过结构事件通知托管 Runtime，不进行逐帧轮询。

## API 与互操作

- `Ens.GetComponent<NpcAiComponent>()` 返回绑定原生宿主的真实 C# 实例。
- `Ens.GetComponents<NpcAiComponent>()` 按原生组件挂载顺序返回实例。
- `Ens.AddComponent<NpcAiComponent>()` 创建原生宿主并构造 C# Wrapper。
- 动态 `GetManagedComponent`/`ComponentProxy` 保留，供按类型名调用和跨语言互操作使用。
- C++→C# 字段和方法仍通过 Managed Proxy 调用。
- C#→C++ 字段和方法仍通过 Native Proxy 调用。
- 原生宿主提供组件身份和序列化存储，但 C# 方法仍属于托管域，不伪装成普通 C++ 成员函数。
- 程序集卸载后旧 Wrapper 和代理立即失效；重新加载时在现存宿主上建立新 Wrapper，并提升 generation。

## 统一序列化与 Inspector

C++ 和 C# 脚本全部存入 `.world`：

```xml
<Component type="ScriptBehaviour">
  <Field name="domain" type="ScriptDomain" value="Managed" />
  <Field name="managedTypeName" type="string" value="Game.NpcAiComponent" />
  <Field name="enabled" type="bool" value="true" />
  <Field name="speed" type="float32" value="2" />
</Component>
```

- C# 字段使用与普通 C++ Component 相同的 `<Field>` 表达、PropertyDocument、Undo 和保存事务。
- 动态字段保存在原生宿主的字段表中。
- 进入 Play 时将宿主字段应用到 C# 实例。
- PIE Inspector 修改同时更新宿主字段表和活跃 C# 对象。
- 游戏代码直接修改普通 C# 字段只影响本次运行，不自动持久化。
- `domain`、`managedTypeName`、`enabled` 是保留字段名。
- 缺少 C# 类型时保留宿主和字段，Inspector 显示 `Missing Script`。
- Inspector 根据域显示：
  - `[C++] CharacterComponent`
  - `[C#] NpcAiComponent`
- 不再单独合并 sidecar 组件，也不显示重复的原生 `ScriptBehaviour` 卡片。
- C# 组件修改只设置 `WorldDirty`。

## 直接删除的旧机制

不做兼容层或迁移工具，直接删除：

- `.world.scripts.json` 的加载、保存和模板生成。
- `SidecarDirty` 及 sidecar 保存事务。
- MountId 和基于 MountId 的脚本匹配。
- 独立于原生组件的 C# 脚本挂载列表。
- 旧 `ApplySerializedValues` 特殊入口。
- sidecar 与 World 双份 Inspector 数据合并。
- 旧格式检测、回退和迁移代码。

## 验收

- C++ `ScriptBehaviour` 能通过反射直接构造，且不是抽象类。
- 每个 C# 脚本对应一个可枚举的原生 `ScriptBehaviour` 实例。
- C# Wrapper 的 `InstanceId` 与原生宿主 `ObjectId` 一致。
- 同一 Ens 可以同时挂载多个 C++、C# 以及同类型 C# 脚本。
- `.world` 能完整往返保存两类脚本，不生成 sidecar。
- 验证 `GetComponent<T>`、跨语言代理、字段同步和句柄失效。
- 验证 Start/Update/FixedUpdate/LateUpdate/DrawGUI/End 次数与 C++→C# 顺序。
- 验证启停、运行时增删、Ens 销毁、程序集重载和 Missing Script。
- 稳定帧无反射扫描、无状态轮询、无列表重建，C# 每阶段只发生一次域调用。
