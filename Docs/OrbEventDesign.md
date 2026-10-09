# OrbEvent 详细落地方案

状态：设计方案，尚未实施。本文只定义实施合同，不修改代码。
首版保持现有 UI 的零参数／单参数事件合同，统一 C++、C#、Inspector 和场景持久化；删除原有 `UIEvent`、`UIEventBinding` 类型。

## 1．已核实的现状

| 现状 | 代码位置 |
|---|---|
| `UIEvent` 是队列消息，包含事件源、事件编号和载荷 | `OrbedenCore/Managed/OrbedenCore.CSharp/UI/UIEvent.cs:123` |
| `UIEventBinding` 保存目标 Ens、组件类型、方法名和启用状态；解析时只查托管脚本 | `OrbedenCore/Managed/OrbedenCore.CSharp/UI/UIEventBinding.cs:11`、`:182` |
| UI 持久化绑定通过五列宿主字段落盘 | `OrbedenCore/Managed/OrbedenCore.CSharp/UI/UIControl.cs:28` |
| UI 派发采用 FIFO，单帧上限为 4096 | `OrbedenCore/Managed/OrbedenCore.CSharp/UI/UIEventDispatcher.cs:12` |
| 跨域组件、方法句柄和调用桥接已经存在 | `OrbedenCore/Src/Scripting/ScriptInterop.h:48`、`OrbedenCore/Managed/OrbedenCore.CSharp/ComponentProxy.cs:256` |
| 托管字段元数据当前跳过 readonly 字段；通用方法调用使用反射 | `OrbedenCore/Managed/OrbedenCore.CSharp/ManagedScriptInterop.cs:163`、`:436` |
| Inspector 的 UI 方法选择器只枚举 C# 脚本 | `OrbedenEditor/Managed/Orbeden.Editor/UI/UIControlEditor.cs:250` |

## 2．总体结构

- `OrbEvent` 表示组件持有的事件；队列中的一次触发另用内部类型 `OrbEventInvocation` 表示。
- 事件由所属组件的语言域持有唯一状态：原生组件持有 C++ 实例，托管组件持有 C# 实例。
- 另一语言通过 `OrbEventHandle` 访问原实例，包装对象不保存第二份绑定和订阅。
- C#→C#、C++→C++ 在所属域直接派发；跨域调用经过 Interop。
- 持久化回调由 `OrbPersistentCall` 描述，运行时订阅由 `OrbEventSubscription` 管理。
- `OrbEvent` 不继承 `Object`，不作为独立资源，不建立全局按名字广播的事件总线。
- 本次迁移 RetainedGUI 对外事件；`UITextEditor` 内部代码通知保持原用途。

## 3．新增文件与类型

| 目录 | 文件及职责 |
|---|---|
| `OrbedenCore/Src/Runtime/Events/` | `OrbEvent.h/.cpp`：`OrbEventBase`、`OrbEvent<T = void>`、`OrbEventSubscription` |
| 同上 | `OrbEventHandle.h`：跨域事件句柄 |
| 同上 | `OrbPersistentCall.h/.cpp`：持久化回调描述与解析状态 |
| 同上 | `OrbEventRuntime.h/.cpp`：原生事件登记、句柄验证、跨域路由、生命周期清理 |
| 同上 | `OrbEventSerializer.h/.cpp`：绑定文本读写及引用重映射 |
| `OrbedenCore/Src/Runtime/Native/` | `OrbEventInterop.h/.cpp`：事件 ABI 函数表 |
| `OrbedenCore/Managed/OrbedenCore.CSharp/Events/` | `OrbEvent.cs`：`OrbEventBase`、`OrbEvent`、`OrbEvent<T>` |
| 同上 | `OrbPersistentCall.cs`、`OrbEventSubscription.cs`、`OrbEventRuntime.cs`、`OrbEventSerializer.cs` |
| `OrbedenCore/Managed/OrbedenCore.CSharp/Interop/` | `OrbEventInterop.cs`：ABI 布局、函数表和字符串桥接 |
| `OrbedenCore/Managed/OrbedenCore.CSharp/UI/` | `UIEventQueue.cs`：UI 触发队列，内部声明 `OrbEventInvocation` |
| `OrbedenEditor/Managed/Orbeden.Editor/` | `OrbEventDrawer.cs`：通用事件字段绘制 |

## 4．公开 API

C# 使用私有持久化字段和只读访问属性，避免业务代码替换事件实例：

```csharp
[SerializeField] private readonly OrbEvent clicked = new();
public OrbEvent Clicked => clicked;

[SerializeField] private readonly OrbEvent<float> valueChanged = new();
public OrbEvent<float> ValueChanged => valueChanged;
```

C++ 使用组件成员：

```cpp
OrbEvent<> clicked;
OrbEvent<float32> valueChanged;
```

- `OrbEvent.Subscribe(Component owner, Action callback)`：追加运行时订阅，返回 `OrbEventSubscription`。
- `OrbEvent<T>.Subscribe(Component owner, Action<T> callback)`：保存强类型回调，返回订阅句柄。
- C++ 对应参数为 `Component& owner`、`std::function<void()>`、`std::function<void(const T&)>`。
- `OrbEventSubscription.Dispose()`／C++ 析构：解除这一条订阅；重复释放无操作。
- C++ 订阅句柄只允许移动；C# 使用 sealed class 实现 `IDisposable`。
- `Invoke()`／`Invoke(T value)`：同步派发；组件必须已附着且属于当前世界。
- `GetPersistentCalls()`：返回只读绑定快照，禁止取得可变内部列表。
- `InsertPersistentCall(index, call)`：下标范围为 `[0, Count]`，插入后递增配置版本。
- `ReplacePersistentCall(index, call)`：替换完整描述并使该行解析缓存失效。
- `RemovePersistentCall(index)`：删除指定行；越界返回失败。
- 插入、替换、删除成功后通知组件字段变更；运行时订阅不标记场景为已修改。
- 不提供隐式 `+=`、`-=` 运算符；引擎调用方迁移为 `Subscribe` 和订阅句柄。

## 5．参数与签名合同

- 首版支持：无参数、`bool`、`int32/int`、`float32/float`、`vector2`、`std::string/string`。
- 参数种类沿用现有 `InteropValueKind`，不重新定义一套 UI 载荷类型。
- 单参数事件允许绑定 `void Method()` 或 `void Method(T)`；无参数方法丢弃事件参数。
- 无参数事件只允许绑定无参数方法。
- 回调必须是公开实例方法，返回 `void`；拒绝泛型方法、静态方法、`ref`、`out`。
- 参数必须准确匹配；不进行数值转换，不用默认值补齐不匹配的参数。
- 保存所选重载的参数种类，解析时准确匹配重载；匹配结果超过一项时标记 Missing。
- 常量参数配置、双参数事件、返回值事件不进入本次实现。

## 6．持久化描述与解析

`OrbPersistentCall` 是不可变描述，包含以下七个字段：

| 字段 | 类型及含义 |
|---|---|
| `callId` | `uint64/ulong`，事件内部稳定行标识 |
| `enabled` | 用户配置的启用状态 |
| `targetKey` | 目标组件或托管宿主的持久化对象 key |
| `targetDomain` | `Native` 或 `Managed` |
| `targetType` | 原生类型名或托管完整类型名 |
| `methodName` | 方法名 |
| `parameterKind` | `Empty` 或事件对应参数种类 |

- 目标使用准确组件身份，不使用“Ens＋第几个同类型组件”作为新格式。
- `callId` 从 1 单调分配；删除后不复用，加载后从最大值加 1 继续。
- 运行时解析状态为 `Unresolved`、`Ready`、`Missing`，与 `enabled` 分离。
- `ResolvePersistentCall`：查目标 key，验证域与类型，准确匹配方法，缓存调用入口。
- 托管方法在解析时通过 `CreateDelegate` 建立 `Action` 或 `Action<T>`；触发时不执行 `MethodInfo.Invoke`。
- 原生方法缓存组件句柄和方法句柄，通过已核实的跨域方法桥接调用。
- 目标销毁、脚本域重载、原生模块卸载、配置替换时清除解析入口。
- Missing 保留完整描述；配置版本或目标登记版本改变后重新解析。
- 每行每个解析版本只输出一次 Warning；Inspector 查询状态不改变配置。

目标 key 采用已有对象身份入口：`OrbedenCore/Src/Runtime/Object/Object.h:320`。

## 7．运行时派发规则

- 每次触发开始时取得持久化绑定和运行时订阅的快照。
- 先按持久化行顺序执行，再按运行时订阅顺序执行。
- 回调中的增删配置、增删订阅，从下一次触发生效。
- 每次调用前验证发布者、接收者和句柄代次；发布者销毁后终止本次剩余调用。
- 接收者销毁后跳过该项，继续其他项。
- 单项 C# 异常、C++ 标准异常或桥接调用失败记录错误，继续剩余项；异常不跨越 ABI。
- 普通同步事件允许嵌套触发，调用深度上限为 32；超限拒绝本次嵌套调用。
- 配置或订阅变更时重建快照；稳定状态下复用快照。
- 解除订阅时，正在执行的快照保留回调存储；最后一个活动调用退出后释放。
- 所有登记、配置修改、订阅和派发限定在引擎主线程；ABI 返回 `WrongThread` 表示违规。

## 8．Interop 合同

- `OrbEventHandle` 为 16 字节、Pack 8：`domain:uint32`、`generation:uint32`、`slot:uint64`。
- `domain` 固定为 `None=0`、`Native=1`、`Managed=2`；全零表示无效。
- `slot` 在登记代次内不复用；句柄和回调 ID 不进入场景文件。
- 参数沿用 24 字节的 `InteropValueAbi`；字符串仍为 UTF-8 指针＋字节长度。
- 新函数全部使用 `ORBEDEN_NATIVE_CALL`／Cdecl，返回 `InteropStatus`。
- 新建版本为 1 的 `OrbEventApi`，含 `abiVersion`、`structSize` 和下表入口。
- 托管反向表 `ManagedOrbEventApi` 注册同名事件操作与回调操作；原生辅助查询只放在 `OrbEventApi`。

| 入口 | 输入／输出及执行步骤 |
|---|---|
| `ResolveEvent` | 组件句柄、字段名 → 事件句柄；验证字段为事件，登记或返回原登记项 |
| `GetParameterKind` | 事件句柄 → 参数种类；验证代次后读取事件签名 |
| `IsAlive` | 事件句柄 → 状态；同时检查事件登记和所属组件 |
| `Subscribe` | 事件、监听组件、回调域、回调 ID → 订阅 ID；追加订阅描述 |
| `Unsubscribe` | 事件、订阅 ID；移除订阅并延迟释放活动回调 |
| `Invoke` | 事件、值指针、值数量；核对零／单参数合同后派发 |
| `ReadPersistent` | 事件、输出缓冲、容量 → 所需字节数；容量不足不写入 |
| `WritePersistent` | 事件、UTF-8 指针、长度；完整解析成功后原子替换配置 |
| `InvokeCallback` | 回调 ID、值指针、数量；从所属域回调登记表调用回调 |
| `ReleaseCallback` | 回调 ID；释放所属域的委托根或原生函数对象 |
| `ResolveManagedHost` | 原生宿主指针 → 准确托管组件句柄 |
| `GetManagedHost` | 托管组件句柄 → 原生宿主指针 |
| `ResolveComponentKey` | key、域、类型 → 准确组件句柄，不按序号选择 |
| `GetComponentKey` | 组件句柄、输出缓冲、容量 → 持久化 key |
| `DescribeMethods` | 组件句柄、输出缓冲、容量 → 方法描述文本 |
| `RegisterManagedApi` | 反向函数表指针；空指针表示撤销登记 |

- 方法描述使用第 10 节的长度前缀容器格式；每项包含方法名、返回种类、参数种类列表。
- 在 Editor 和 Player 原生 API 的尾部各追加 `GetOrbEventApi`，保持既有槽位偏移。
- 两个外层 ABI 版本从 3 更新为 4，同步 C# 布局检查、大小断言和初始化逻辑。
- 字符串转换全部位于 `OrbEventInterop`，调用统一的 `InteropText`。
- C# 同域字符串事件直接传原字符串；C++ 同域直接传原 UTF-8 字符串。
- 跨域触发时才转换实际字符串参数；一次派发内复用转换结果，无参数回调不触发转换。
- 队列需要持有独立参数：C# 保留不可变字符串引用，C++ 保存字符串副本。

外层表与托管镜像：`OrbedenCore/Src/Runtime/Native/OrbedenNativeApi.h:39`、`OrbedenCore/Src/Runtime/Native/OrbedenEngineNativeApi.h:12`、`OrbedenCore/Managed/OrbedenCore.CSharp/OrbedenCoreRuntime.cs:7`。

## 9．反射与代码生成

- 在 `Reflection::FieldKind`、`Reflection::ValueKind`、`InteropValueKind` 尾部追加 `Event`，保留现有编号。
- 类型化事件值携带 `OrbEventHandle`；它用于运行时访问，不作为持久化文本。
- 原生 `FieldInfo` 增加事件访问器，返回所属组件成员的 `OrbEventBase`。
- MetaGen 识别 `OrbEvent<>` 和第 5 节的五种 `OrbEvent<T>`；其他模板参数报告定位错误。
- 修改 `Program.cs` 的字段分类与反射输出，以及 `BindingTypes.cs`、`BindingGenerator.cs` 的事件映射。
- 生成 C# 只读事件属性；每个包装对象缓存对应代理，句柄改变时替换缓存。
- 托管元数据识别 `OrbEventBase` 派生字段，允许 readonly 事件字段进入持久化。
- 加载配置时修改原事件的持久化列表，不给 readonly 字段赋值。
- 元数据登记后将事件关联到准确组件句柄和字段名；普通字段处理保持现有行为。
- 禁止手工修改生成文件；生成结果由用户构建时刷新。

现有字段分类入口：`Tools/OrbedenMetaGen/Program.cs:395`；现有反射字段结构：`OrbedenCore/Src/Runtime/Reflection.h:307`。

## 10．场景格式与引用重映射

- 沿用 `<Field name="..." type="..." value="..."/>` 外层结构。
- `type` 固定为 `OrbEvent` 或 `OrbEvent<Bool/Int32/Float32/Vector2/String>`。
- `value` 使用长度前缀容器，长度按 UTF-8 字节数计算。
- 外层元素为 `["1", parameterKind, call1, call2, ...]`，第一项为格式版本。
- 每个 call 是七字段容器，字段顺序严格对应第 6 节表格。
- 复用已核实的原生容器格式；C# 实现同格式解析，字符转换调用 `InteropText`。
- `Serialize`：只输出持久化描述，保持行顺序；不输出委托、句柄、解析状态、运行时订阅。
- `Deserialize`：检查版本、完整消费、字段数量、行 ID 唯一性和参数种类，全部成功后应用。
- 无法解析的字段保留原始文本、停止该字段派发并 Warning；用户编辑该字段后写入新格式。
- `EnumerateReferences`：返回每行非空 `targetKey`。
- `RemapReferences`：通过场景复制映射表替换组件 key，其他字段原样保留。
- 复制到新对象时保留行 ID；跨复制范围引用执行现有复制操作的外部引用策略。
- 场景保存、组件快照、Undo、Prefab、PIE 世界复制均使用这组读写与重映射入口。
- Cook 保留持久化描述；Player 加载后重建解析入口，不携带编辑器或 PIE 运行时状态。

容器格式：`OrbedenCore/Src/Runtime/Reflection.cpp:429`；场景快照与引用处理：`OrbedenCore/Src/Runtime/WorldSerializer.cpp:906`、`:1169`。

## 11．RetainedGUI 替换

| 控件 | 新事件字段／公开属性 |
|---|---|
| Button | `clicked`／`Clicked`：`OrbEvent` |
| CheckBox、RadioButton | `checkedChanged`／`CheckedChanged`：`OrbEvent<bool>` |
| Slider、ScrollBar | `valueChanged`／`ValueChanged`：`OrbEvent<float>` |
| ScrollBox | `scrollChanged`／`ScrollChanged`：`OrbEvent<vector2>` |
| ComboBox | `selectionChanged`／`SelectionChanged`：`OrbEvent<int>` |
| TextField | `textChanged`／`TextChanged`、`submitted`／`Submitted`：`OrbEvent<string>` |

- 删除 `UIEvent.cs`、`UIEventBinding.cs`、`UIEventDispatcher.cs`。
- 同时删除 `UIEventIds`、`UIEventPayload`、`IUIEventSource`、控件旧 C# event 和五列存储字段。
- 删除 `UIControl` 的旧绑定操作、摊平、重建、`RaiseCodeEvent` 和旧事件处理入口。
- 将各控件触发点改为 `UIEventQueue.Enqueue(event, argument)`，保留原触发条件。
- `OrbEventInvocation` 只保存事件实例／代理、参数、入队序号和入队时的句柄。
- `UIEventQueue.BeginFrame()` 重置计数；`Dispatch()` FIFO 取出记录，验证句柄后调用事件。
- 回调触发的新 UI 事件追加到队尾；单帧最多处理 4096 条，超限清空余项并报错。
- `Reset()` 清空队列；世界分离和脚本域退出时调用。
- 输入路由、焦点、按下状态和视觉颜色继续由 UI 系统维护，不移入 `OrbEvent`。
- `UIEventTarget`、指针事件和原始输入事件保留，它们属于输入路由合同。
- UI 不再提供独立 Dispatcher 订阅接口；所有对外监听直接订阅对应 `OrbEvent`。

## 12．旧场景读取

- 只增加旧五列数据的读取转换，不保留旧事件类、旧公开 API 或旧运行时。
- 转换入口命名为 `ImportLegacyUIBindings`，在宿主字段对账删除旧字段之前执行。
- 编号映射固定为：0 Clicked、1 CheckedChanged、2 ValueChanged、3 ScrollChanged、4 SelectionChanged、5 TextChanged、6 Submitted。
- 五列长度不同只处理最短公共部分，并 Warning。
- 按原列表顺序把每行分配到对应新事件；保留行顺序和启用状态。
- 原目标类型为空时，按原脚本遍历顺序查找原解析规则会选择的方法。
- 成功找到目标后记录准确宿主 key 与重载签名。
- 无法找到目标时，保留原 Ens key、类型和方法，状态为 Missing；不删除该行。
- 场景引用扫描和重映射仍处理这种 Missing 行的 Ens key。
- 同一事件已有新字段时，新字段优先，跳过该事件的旧数据并 Warning。
- 读取不自动改写世界文件；下一次保存只输出新事件字段。

## 13．Inspector 接入

- 新增 `OrbEventDrawer.Draw(eventProperty)`，通过通用属性绘制入口处理原生和托管事件。
- 每个事件字段显示事件名、参数种类、绑定列表和添加按钮。
- 每行显示启用开关、目标组件对象框、方法选择器、删除按钮。
- 允许先拖入 Ens，再从其组件中选择准确实例；选择后保存组件 key。
- 方法选择器同时列出 `[C++]`、`[C#]` 分组，按完整类型名、方法名、参数种类排序。
- 候选只显示第 5 节允许的方法；同名重载分别显示完整签名。
- 空目标显示 `None`；非空 key 解析失败显示 `Missing`，方法失效也显示 `Missing`。
- 修改通过组件字段事务提交；Undo 保存修改前后完整持久化文本。
- 删除 `UIControlEditor` 内旧事件表绘制；保留控件视觉和导航配置。
- 方法候选在弹窗打开、目标改变或类型登记版本变化时重建；稳定帧复用候选及 `GuiContent`。

## 14．生命周期、实施顺序与验收

- 监听者销毁：解除其全部运行时订阅；发布者销毁：注销事件并使代理句柄失效。
- 脚本域卸载前释放托管委托根；原生模块卸载前释放该模块的函数对象及解析入口。
- 世界切换：先停止派发并清空 UI 队列，再清理运行时登记，最后卸载脚本域和模块。
- 新世界重新登记事件、加载持久化描述、解析回调；旧订阅不得恢复。
- 原生销毁清理接入已有销毁监听机制：`OrbedenCore/Src/Runtime/Object/Object.h:32`。
- 实施顺序：基础类型与句柄 → 原生／托管运行时 → ABI → 反射及生成器 → 序列化重映射 → Inspector → UI 替换 → 旧内容读取。
- 同步 `.vcxproj`、`.filters`；更新 `ProjectConventions.md`、`RetainedGUI.md`、`BuildAndPackaging.md`。
- 实施完成时项目版本由当前 64 更新为 65，记录需要重建引擎、同步 SDK、重建游戏模块。
- 静态检查旧 `UIEvent`、`UIEventBinding` 类型及引用已删除；允许旧字段名仅出现在读取转换入口。
- 用户手动验证四种调用方向：C++→C++、C++→C#、C#→C++、C#→C#。
- 用户手动验证：五种参数、无参数回调、重载、回调异常、订阅增删、对象销毁、Missing、Undo、复制、Prefab、旧场景、PIE、Cook 和 Player。
- 不新增测试代码，不执行编译、测试或基准；保留当前字体、布局、输入和字符串优化的工作区改动。
- 新代码及注释不提及其他商业引擎；公开 C# API 写简短中文 XML 注释。
