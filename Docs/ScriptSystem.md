# 脚本系统

本文说明 C++ 与 C# 脚本的组件模型、生命周期、场景存储、跨语言调用和构建方式。

## 1. 组件与身份

C++ 脚本继承原生 `Script`；C# 脚本继承 `Orbeden.Script`，每个实例绑定一个精确类型为原生 `Script` 的组件。

```text
Ens
├─ Transform
├─ MoveBehaviour (C++ 派生组件，Native)
├─ Script (Managed，Game.NpcAi)
└─ Script (Managed，Game.NpcAi)
```

原生 `Script` 可以通过反射工厂构造，基类没有虚生命周期方法。`domain` 根据实际原生类型确定：精确宿主是 Managed，C++ 派生类型是 Native，外部不能修改域。

每个宿主有独立的 `ObjectId`、稳定路径和组件挂载位置。C# Wrapper 的 `InstanceId` 等于宿主的 `ObjectId`。所有 C# 脚本共享原生 `Script::StaticType()->GetId()`，具体托管类型由 `managedTypeName` 区分。同一 Ens 可以挂载多个两种语言的脚本，包括多个同类型 C# 脚本。

| 身份 | 用途 | 是否持久化 |
| --- | --- | --- |
| ObjectId / InstanceId | 本进程互操作、Wrapper、组件句柄 | 否 |
| TypeRuntimeId | 本进程原生类型注册表的数字索引 | 否 |
| 原生类型名 | `.world` 中 Component 的 `type` 属性，用于加载时查找类型 | 是 |
| Component stableId | 保存、引用、删除恢复、属性历史 | 是 |
| managedTypeName | C# 完整类型名 | 是 |
| generation | 检测重载后的过期代理和成员句柄 | 否 |

`TypeRuntimeId` 按运行时类型注册表的槽位分配，不保证跨进程或重新构建后保持一致。组件序列化使用注册类型名，例如 `<Component type="Transform">`；加载时通过类型名查找当前注册的 `Type`，再创建组件并读取字段。因此，TypeRuntimeId 的数值变化不影响场景加载。类型名必须能在加载时解析，重命名组件类型时需要同步修改场景中的类型引用。`stableId` 标识具体组件实例，类型名标识组件的种类。

### 原生脚本 Binding 与类型身份

C++ 根对象的实际类名为 `Orbeden::Object`，头文件通过 `using Orbeden::Object` 允许现有原生代码简写为 `Object`；反射注册名仍为 `Object`。C# 根对象为 `Orbeden.Object`。

| 实例 | 实际原生类型 / TypeRuntimeId | 托管侧表示 | 生命周期执行域 |
| --- | --- | --- | --- |
| C++ `MoveBehaviour : Script` | `MoveBehaviour::StaticType()->GetId()` | `ComponentProxy`，或调用者自行封装的普通 C# 类 | Native |
| C# `Game.NpcAi : Orbeden.Script` | `Script::StaticType()->GetId()` | `Game.NpcAi`，由 `managedTypeName` 与 CLR Type 区分 | Managed |

继承关系不要求 TypeRuntimeId 相等。`MoveBehaviour::StaticType()->Is(Script::StaticType())` 为 true，但两个类型的 ID 不同。基类查询可以匹配派生实例，成员解析仍使用实例的 `GetType()`。互操作组件句柄保存的是域、generation 和 ObjectId，并不是 TypeRuntimeId。

MetaGen 只为 Core 的 Object 派生类生成 C# 包装和绑定调用入口。整个游戏原生模块都不生成 C# 绑定，无需额外标记宏。游戏 C++ `MoveBehaviour` 通过 `ens.GetNativeComponent("MoveBehaviour")` 返回的 `ComponentProxy` 访问；C# 的 `GetComponent<T>` / `GetComponents<T>` 只返回具有托管表示的组件，不为游戏 C++ 组件创建基类包装。

游戏模块仍生成完整的类型、字段、方法、序列化访问器和生命周期回调元数据。Inspector、场景存储和跨语言 Proxy 使用这些元数据，不依赖 C# 绑定类。C++ 查询仍能匹配原生派生实例。

### 包装与 GC 生命周期

Core 包装按需创建，后续查询复用同一包装。原生对象关联的强 GC 句柄负责保活包装，局部变量离开作用域或执行普通 GC 不会解除关联。原生对象销毁时释放句柄并使包装失效；程序集卸载及引擎 API 断开时清理对应包装。用户代码仍持有失效包装时，托管内存等到没有引用后再由 GC 回收。

显式 `Resources.UnloadUnusedObjects()` 对可回收对象的包装临时使用弱句柄，执行 GC 后收集真实托管引用，再恢复仍存活包装的强句柄。场景所属对象保持强保活，资源依赖继续由原生回收流程标记。游戏 C++ 对象没有托管包装，不参与句柄保活。


## 2. 编写 C# 脚本

```csharp
using Orbeden;

namespace Game;

public sealed class NpcAi : Script
{
    public float speed = 2.0f;
    [SerializeField, HideInEditor] private int savedCounter;
    public Mesh? mesh;
    public EnsId target;

    public NpcAi(Ens ens) : base(ens)
    {
        // 此时 InstanceId、Ens、enabled 已连接原生宿主。
    }

    private void OnUpdate(float deltaTime)
    {
        vector3 position = Ens.Transform.GetLocalPosition();
        position.x += speed * deltaTime;
        Ens.Transform.SetLocalPosition(position);
    }

    public void AddCounter(int amount) => savedCounter += amount;
}
```

脚本必须是具体类型，并提供 public `(Ens ens)` 构造函数。运行时先创建/定位宿主，再把宿主指针放入线程局部构造上下文，随后调用用户构造函数。基类只允许消费一次匹配的构造上下文；直接 `new NpcAi(ens)` 会抛异常。

`enabled` 的权威值在原生宿主，C# 属性直接代理原生读写。调度器保存由结构事件更新的活动状态，阶段循环无需逐脚本查询原生状态。

编辑器添加 C# 组件时也在真实宿主上下文中执行构造函数，补齐字段初始化器和构造函数产生的序列化默认值，但不执行生命周期。构造函数应只做初始化；场景行为放入 `OnStart`。

public 的受支持字段参与序列化；非 public 字段需要 `[SerializeField]`。`[HideInEditor]` 只隐藏字段，不取消持久化，也不影响删除 Undo 的快照。`domain`、`managedTypeName`、`enabled` 是保留字段名，不要在派生类声明同名字段。

支持基本数值、字符串、vector3、color、quaternion、EnsId 和可绑定的原生 Object 引用。数组、列表、自定义结构体、委托和任意托管对象图不属于当前字段协议。

宿主字段表按脚本类型对账：类型里有、宿主里没有的字段补进宿主（取值来自构造函数与字段初始化器），类型里已经删掉的字段从宿主删除。对账发生在脚本程序集重新加载后（编辑器里由 Inspector 在画组件卡片前做一次，运行时在构造 Wrapper 时做一次）。**类型解析不到时一律不动**——Missing Script 或程序集加载失败不会丢字段与值。编辑器里的删除进撤销；改名等于删旧加新，旧值不保留。

## 3. 编写 C++ 脚本

```cpp
// MoveBehaviour.h
#pragma once
#include "Runtime/Object/Script.h"

class MoveBehaviour final : public Script
{
    OBJECT_TYPE_DECLARE(MoveBehaviour)
public:
    float32 speed = 2.0f;
protected:
    void OnUpdate(float32 deltaTime);
};
```

```cpp
// MoveBehaviour.cpp
#include "MoveBehaviour.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/Transform.h"

OBJECT_TYPE_IMPLEMENT(MoveBehaviour, Script)

void MoveBehaviour::OnUpdate(float32 deltaTime)
{
    Transform* transform = GetEns()->Transform();
    vector3 position = transform->GetLocalPosition();
    position.x += speed * deltaTime;
    transform->SetLocalPosition(position);
}
```

游戏模块的 MetaGen 收集 public 受支持字段；非 public 字段用 `ORBEDEN_SERIALIZE_FIELD` 标记。MetaGen 为声明的生命周期生成静态 thunk，注册到类型回调表；未声明的阶段沿继承链解析。

引擎基类的身份、宿主字段表及运行时状态不自动生成持久化字段。`Script.enabled` 由手写反射注册，写入经过 `SetEnabled`。修改头文件后重新运行 MetaGen，不手改 `Reflection.Generated.cpp`。

## 4. 生命周期与结构变化

两种语言使用相同的约定名称；全部返回 void，不允许 static、virtual、override 或泛型生命周期。

| 阶段 | C++ 签名 | C# 签名 |
| --- | --- | --- |
| 首次启用 | OnStart() | OnStart() |
| 帧更新 | OnUpdate(float32) | OnUpdate(float) |
| 固定步 | OnFixedUpdate(float32) | OnFixedUpdate(float) |
| 帧后更新 | OnLateUpdate(float32) | OnLateUpdate(float) |
| GUI | OnDrawGUI() | OnDrawGUI() |
| 结束 | OnEnd() | OnEnd() |

每阶段固定执行 Native Domain，再一次性进入 Managed Domain。C++ 遍历静态 thunk 表；C# 遍历预绑定闭合 delegate 表。普通阶段不会按名字找方法，也不会执行反射扫描。反射、排序和阶段列表重建只发生于初始化或结构变化；`GetComponents` 等显式查询仍会创建查询结果。

首次处于 `enabled && Ens.WorldActive` 的实例执行一次 Start。禁用和再次启用不会重复 Start，也不会调用 End。组件移除、Ens 销毁或运行时关闭时，已 Start 实例执行一次 End；从未 Start 的实例不执行 End。

回调内创建的新组件可以立即取得 Wrapper，但阶段表在后续边界更新，不能把新回调插入当前正在遍历的列表。删除请求在原生域边界处理，避免用户代码仍在执行时释放组件内存。启停和层级活动变化通过事件更新调度状态。End 内重复删除自身、删除其他组件或销毁 Ens 时有重入保护。

> 也就是说，脚本回调里"加组件、删组件、开关组件"，运行时不会当场改结构，而是先记下来，等这一轮回调全部跑完再统一处理。  

没有配置 `managedTypeName` 或找不到对应 C# 类型的宿主不进入调度。

### GUI 绘制

`OnDrawGUI()` 在引擎 ImGui 帧内执行，可以调用 C# `GUI` 静态类。除标准控件（Label、Button、BeginPanel、Table 等）外，`GUI` 还提供自由绘制 API，用于 PFD、仪表等自定义 HUD：

- `GUI.Rgba(r, g, b, a)` 把颜色打包为原生字节序（0xAABBGGRR）。
- `GUI.BeginFixedWindow(title, x, y, w, h)` / `GUI.EndFixedWindow()` 创建固定位置、无边框、不响应输入的绘制窗口；`GUI.GetViewportSize()` 获取主视口尺寸用于锚定。
- 图元：`Line`、`Polyline`、`Rect`、`RectFilled`、`Circle`、`CircleFilled`、`Arc`、`TriangleFilled`、`Text`、`GetTextSize`。
- `GUI.PushClipRect(minX, minY, maxX, maxY)` / `GUI.PopClipRect()` 裁剪后续绘制（PFD 姿态区必需）。

坐标相对当前绘制窗口内容区域左上角，角度使用弧度，字号通过 fontScale 按默认字体大小缩放。默认飞行 Demo 的 FlightHud 是完整示例。

## 5. 添加、查询和删除

```csharp
NpcAi? created = ens.AddComponent<NpcAi>();
NpcAi? first = ens.GetComponent<NpcAi>();
NpcAi[] all = ens.GetComponents<NpcAi>();
if (created != null) ScriptRuntimeRegistry.RemoveScript(created);
```

查询返回真实 C# 实例，多个同类型实例按原生挂载顺序返回。`[UniqueComponent]` 声明唯一组件；`[DependsOnComponent(typeof(...))]` 声明依赖。依赖图先验证后创建，循环依赖被拒绝。

C++ 用 `ens->AddComponentInstance<MoveBehaviour>()` 显式添加独立实例，`GetComponentInstances` 枚举同类型组件。精确 `Script` 不出现在普通 C++ 添加菜单中。

Editor 多选添加先检查全部目标的 Unique 冲突及依赖图，再按依赖优先顺序创建；任一失败回滚本次创建。撤销同时移除这次新增的依赖，保留原有依赖。删除/恢复使用完整组件快照并恢复原挂载位置。

## 6. 场景存储

```xml
<Ens stableId="world://ens/npc" name="Npc">
    <Component type="Script" stableId="world://ens/npc/Script/ai">
        <Field name="domain" type="ScriptDomain" value="Managed" />
        <Field name="managedTypeName" type="string" value="Game.NpcAi" />
        <Field name="enabled" type="bool" value="true" />
        <Field name="speed" type="float32" value="2" inspectorVisible="true" />
        <Field name="savedCounter" type="int32" value="3" inspectorVisible="false" />
        <Field name="mesh" type="Ref&lt;Orbeden.Mesh&gt;" value="Resource/Mesh/npc.obj//Mesh/Main" />
        <Field name="target" type="EnsId" value="world://ens/target" />
    </Component>
</Ens>
```

上述是组件存储片段，完整 World 包含 Transform、World 根节点和场景层级。保存时 C++ 和 C# 组件共用 `<Component>/<Field>` 表达。读取时检查 domain 与真实原生类型一致。

Object 引用保存资源 Key、Ens 或组件的独立稳定路径，不保存运行时 ObjectId。组件稳定路径随组件保存和恢复，因此跨 Ens 和同类型多实例引用不会依赖本次加载的 ObjectId。托管 EnsId 字段保存目标 Ens 的稳定路径，进入运行态再解析。空引用使用空字符串。临时 orphan 资源没有可持久化身份，不能写入宿主引用字段。

Ref 字段及引用数组/列表反序列化时，目标丢失或类型不匹配报告 Warning，不阻止世界载入；运行时引用为空，原 Key 保留到用户显式清空或重新指定。ObjectField 对空 Key 显示 None，对无法解析的 Key 显示 Missing，并在悬停提示中显示原 Key。数组长度编码或场景 XML 等格式错误仍会拒绝载入。

资源加载会扫描宿主动态 Object 字段；资源移动/重命名时，Editor 对宿主字段执行同一套路径映射。Missing Script 仍保留宿主、完整类型名、字段类型、可见性和字段值，缺少程序集不会丢弃数据。

进入 Play 时将宿主字段应用到 Wrapper。PIE Inspector 修改会同时写宿主和活跃 C# 实例。游戏代码直接给普通 C# 字段赋值只改变本次运行对象，不自动写回宿主。显式代理 SetField 会同步宿主。停止 PIE 后按编辑器的 World 恢复流程丢弃运行修改。

## 7. Inspector 与事务

Inspector 按原生挂载顺序显示 `[C++] Type` 或 `[C#] Type`，每个 C# 宿主只显示一个卡片。找不到类型时显示 `Missing Script`，允许查看保存字段、删除和撤销恢复；重新加载有效程序集后可重新建立 Wrapper。

Transform、Renderer、RigidBody、Collider、CharacterController 使用相应字段顺序、资源选择和枚举控件。标量写入经过 `PropertyDocument`：读取多目标快照、显示 Mixed、验证、提交；失败时回滚并显示错误。

集合字段共用可折叠列表控件，标题栏以只读输入框显示当前数量，并提供增删按钮，元素行支持选择与拖拽排序。支持 C# 一维 `T[]` / `List<T>` 和 C++ `T[N]` / `std::array<T, N>` / `List<T>` / `std::vector<T>`，元素使用 Inspector 已支持的基础值或对象引用类型；C++ 固定数组只能编辑和排序，长度只读。结构操作为各目标保存完整元素快照，刷新字段结构后恢复元素，支持撤销、重做和失败回滚。多选长度不同时只显示共有下标，数量显示 Mixed，可分别追加元素。

Transform 的 localRotation 在 Inspector 中显示为 XYZ 欧拉角（度），按 Z-X-Y 顺序编辑，内部和场景文件仍保存四元数。编辑期间保留用户输入的等价角度分支，撤销或外部旋转变化后重新同步显示。

集合持久化采用元素个数和每项 UTF-8 字节长度编码，保留空槽及字符串中的分隔符。原生对象引用列表继续读取旧场景的 `|` 分隔格式，保存时使用显式长度；场景对象引用在保存、重载和复制时按稳定身份处理。

属性历史以组件稳定身份定位目标，因此“编辑字段 → 删除组件 → Undo 删除 → Undo 字段编辑”不依赖已过期的 ObjectId。完整组件 XML 快照包含隐藏字段。Undo/Redo 失败不应提前弹出历史记录。

C# 组件修改会设置 WorldDirty，组件字段随 World 保存到 `.world` 文件中。

## 8. 跨语言代理

C# 对未知 C++ 游戏类型使用 Native Proxy；C++ 对 C# 游戏类型使用 Managed Proxy。原生宿主提供身份和存储，C# 方法仍在托管域执行。

```cpp
using namespace ScriptInterop;
ComponentProxy script = FindManagedComponent(ensId, "Game.NpcAi", 0);
script.SetField("speed", Reflection::Value(4.0f));

constexpr Reflection::ValueKind signature[]{ Reflection::ValueKind::Int32 };
MemberHandle method;
if (script.ResolveMethod("AddCounter", signature, method) == InteropStatus::Ok)
{
    Reflection::Value args[]{ Reflection::Value(int32(1)) };
    Reflection::Value result;
    script.Invoke(method, args, result);
}
```

```csharp
ComponentProxy? native = ens.GetNativeComponent("MoveBehaviour");
native?.SetField("speed", InteropValue.From(4.0f));
```

方法按名称和精确参数种类匹配；不做隐式数值转换。生命周期名称不进入通用方法表。`InteropValue` 中的 ObjectId 和 EnsId 是调用期间的运行时表示，不能直接当作 .world 中的引用文本。

### 8.1 选择调用方式

| 场景 | 入口 | 参数与结果 |
| --- | --- | --- |
| C# 调用签名已知的 C++ 方法，尤其是每帧调用 | `TryBindMethod` / `TryBindAction`，缓存 `NativeMethod` / `NativeAction` | 具体 ABI 类型直接传值 |
| 运行时才能确定方法签名，或工具动态调用 | 缓存 `ComponentMethod`，或 `ComponentProxy.Invoke(name, ...)` | `InteropValue` |
| C# 读写类型已知的原生字段 | `TryBindField`，缓存 `NativeField<T>` | 具体 ABI 类型直接传值 |
| 动态字段读写或 Inspector 属性 | `TryGetField` / `SetField` 等通用接口 | `InteropValue` |
| C++ 调用 C# 方法 | `ScriptInterop::FindManagedComponent` 与 `MemberHandle` | 通用反射值协议 |

类型化入口用于 C# → C++ 原生方法与字段。动态调用、动态字段和 C++ → C# 仍使用各自的通用协议。游戏 C++ 组件继续通过 Proxy 查询，不产生游戏专用 C# Binding。

### 8.2 绑定一次，重复直接调用

在 `OnStart` 获取组件并校验完整方法签名，随后缓存绑定结果。下面示例使用飞行模板已有的 `FlightController.GetAirspeed()`：

```csharp
using Orbeden;

namespace Game;

public sealed class AirspeedReader : Script
{
    private NativeMethod<float> getAirspeed;
    public float airspeed;

    public AirspeedReader(Ens ens) : base(ens) {}

    private void OnStart()
    {
        ComponentProxy? flight = Ens.GetNativeComponent("FlightController");
        getAirspeed = default;
        if (flight != null)
            flight.TryBindMethod("GetAirspeed", out getAirspeed);
    }

    private void OnUpdate(float deltaTime)
    {
        if (getAirspeed.Invoke(out float value) == InteropStatus.Ok)
            airspeed = value;
    }
}
```

无返回值使用 `NativeAction`；有返回值使用 `NativeMethod`，最后一个泛型参数是返回类型，其余泛型参数按原生声明顺序对应输入参数。`Invoke` 返回调用状态，方法结果通过最后一个 `out` 参数取得。下面的无返回值方法来自模板 `SampleNativeBehaviour`：

```csharp
ComponentProxy? sample = ens.GetNativeComponent("SampleNativeBehaviour");
if (sample != null && sample.TryBindAction("ReceiveManagedPing", out NativeAction<float> ping) == InteropStatus.Ok)
    ping.Invoke(1.5f);
```

多参数绑定同样直接传值。假设原生组件声明了 `float32 Evaluate(float32 value, int32 count, bool enabled, const vector3& direction)`：

```csharp
if (native.TryBindMethod("Evaluate", out NativeMethod<float, int, bool, vector3, float> evaluate) == InteropStatus.Ok)
{
    InteropStatus status = evaluate.Invoke(1.5f, 3, true, new vector3(1, 2, 3), out float result);
}
```

两个类型族支持 0～16 个输入参数，包括无泛型参数的 `NativeAction`。绑定按完整签名精确匹配，不做数值转换，也不补原生默认参数。`unmanaged` 约束只保证类型不含托管引用，允许的 ABI 类型仍由下面的白名单决定。

| C++ 方法类型 | C# 类型化参数或结果 | ABI 约定 |
| --- | --- | --- |
| `bool` | `bool` | 单字节，原生 thunk 使用 `uint8` |
| `int32` / `uint32` / `uint64` | `int` / `uint` / `ulong` | 同宽整数 |
| `float32` | `float` | 32 位浮点 |
| `vector2` / `vector3` / `color` / `quaternion` | 同名 Core 值类型 | 按约定布局传值 |
| `EnsId` | `EnsId` | 运行时实体句柄，8 字节 |
| `std::string` | `NativeUtf8` | 16 字节 UTF-8 视图 |
| `StringId` | `NativeStringId` | 与字符串同布局，绑定种类不同 |
| `void` 返回值 | `NativeAction` | 无结果输出参数 |

`double`、任意自定义结构体、枚举、指针、Object / Ref 引用、数组、列表及托管 `string` 不在当前类型化白名单中。原生引用参数只采用 MetaGen 支持的输入值签名，不提供 `ref` / `out` 参数回写。需要对象身份时可设计为 `EnsId` 参数；需要其他语义时应明确设计接口或使用支持该值类型的动态协议。

### 8.3 类型化字段绑定

字段也可以像方法一样，在初始化时按名称和精确类型绑定一次，随后缓存 `NativeField<T>`。读写阶段不再查询名称字典、编码字段名或经过 `InteropValue` / `Reflection::Value`。下面使用 FlightController 已有的 `throttleRate` 字段：

```csharp
using Orbeden;

namespace Game;

public sealed class ThrottleRateWriter : Script
{
    private NativeField<float> throttleRate;
    public float rate = 1.0f;

    public ThrottleRateWriter(Ens ens) : base(ens) {}

    private void OnStart()
    {
        ComponentProxy? flight = Ens.GetNativeComponent("FlightController");
        throttleRate = default;
        if (flight != null)
            flight.TryBindField("throttleRate", out throttleRate);
    }

    private void OnUpdate(float deltaTime)
    {
        throttleRate.Set(rate);
    }
}
```

`Set(T value)` 返回写入状态，`Get(out T value)` 返回读取状态；仅在 Ok 时使用读取结果。字段类型与方法共用上面的 ABI 白名单，字符串对应 `NativeField<NativeUtf8>`，StringId 对应 `NativeField<NativeStringId>`，同样需要遵守输入固定和输出立即复制的约定。数组、对象引用、任意自定义结构体及没有类型化访问入口的手写字段返回 UnsupportedType；类型不匹配返回 TypeMismatch。可绑定受支持的继承字段和显式序列化字段。

`CanRead` / `CanWrite` 表示绑定时是否存在相应访问入口，不代表组件仍然存活。`ORBEDEN_BIND_ACCESSORS(..., None)` 禁止类型化写入，`ORBEDEN_BIND_ACCESSORS(None, ...)` 禁止类型化读取；不可用的操作返回 UnsupportedType。`IsValid` 校验当前组件与字段代次，Get / Set 自身也会校验，无需逐次先检查 IsValid。

MetaGen 为字段生成具体类型的读写 thunk。写入调用显式 setter；Transform、Script.enabled 等字段保留业务 setter，`ORBEDEN_BIND_CHANGED` 在成功写入后执行一次，随后标记所属 World 已修改。bool setter 返回 false 时返回 InvalidArgument，不继续变更通知或标脏。原生异常转为 InvocationFailed；若 setter 或回调在修改字段后抛异常，字段值可能已经改变，Set 本身不提供撤销事务。

字段使用独立的 DirectField 成员种类和 TypedFieldsApi，复用方法的 Prepare / Complete 调用保护。绑定只保存组件身份与成员槽位，不暴露原生字段内存地址，也不跨调用保存游戏 DLL 访问函数。主线程、重入、模块卸载和代次规则与 NativeMethod 一致；World、反射表或运行库重载后应重新获取并绑定。

### 8.4 UTF-8 字符串

假设原生组件有公开方法 `std::string EchoText(const std::string& value)`，先缓存完整签名，再固定输入缓冲并立即复制输出：

```csharp
//在初始化阶段保存 echoText
InteropStatus bindStatus = native.TryBindMethod("EchoText", out NativeMethod<NativeUtf8, NativeUtf8> echoText);

//调用阶段；输入 byte[] 可按业务需要复用
unsafe
{
    if (bindStatus == InteropStatus.Ok)
    {
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes("中文\0🛩");
        fixed (byte* pointer = bytes)
        {
            if (echoText.Invoke(new NativeUtf8(pointer, bytes.Length), out NativeUtf8 result) == InteropStatus.Ok)
            {
                string copy = result.ToString();
                //保存 copy，不保存 result.Data
            }
        }
    }
}
```

输入视图包含 UTF-8 指针和显式字节长度，允许空文本和内嵌 NUL。输入内存在整个调用期间必须有效，托管缓冲须 `fixed`。输出借用 Core 线程局部缓冲，应在下一次返回字符串的互操作调用、上下文切换或关闭前立即 `ToString()` 复制；不能缓存输出指针，也不要把多个尚未复制的字符串结果当作独立存储。只在调用成功时读取输出。

`StringId` 使用 `NativeStringId`，构造方式同样是指针加字节长度；即使两者布局相同，也不能拿 `NativeUtf8` 绑定 StringId 参数或结果。视图本身不分配，但 UTF-8 编码、C++ 字符串和托管文本复制仍可能分配。

### 8.5 状态、失效与调用保护

| 状态 | 常见原因与处理 |
| --- | --- |
| `Ok` | 绑定或调用成功，可以使用结果 |
| `NotFound` | 名称或参数签名不匹配，核对原生方法声明 |
| `TypeMismatch` | 类型化绑定的返回类型与原生声明不同 |
| `UnsupportedType` | 组件域或 ABI 类型不受支持，或手写元数据缺少类型化入口 |
| `StaleHandle` | API 未连接、绑定默认值、组件销毁、World 切换或成员代次失效 |
| `WrongThread` | 调用线程不是互操作主线程，应回到主线程调用 |
| `ReentrancyLimit` | 原生与托管嵌套调用达到 32 层上限 |
| `InvalidArgument` | 字段 bool setter 拒绝写入，或 UTF-8 视图等输入无效 |
| `InvocationFailed` | 原生方法、字段访问器或变更回调抛异常，或绑定过程发生异常 |

组件缺失时 `GetNativeComponent` 返回 null。失败绑定会输出默认方法值；`Invoke` 自带有效性检查，逐帧调用无需先检查 `NativeMethod.IsValid`。`IsValid` 自身也会执行一次校验，并不为下一次调用保留保护。

World 上下文切换、关闭和反射注册表更新都会使旧句柄失效。即使组件仍然存在，方法也可能已失效，应重新获取组件并重新绑定；不要反复调用旧句柄等待它自行恢复。绑定不拥有组件，不延长组件生命周期。

类型化调用使用 MetaGen 生成的具体 C++ ABI thunk 与 Core 通用泛型调用器。参数按值传递，返回值写入具体类型的输出地址，调用过程不使用 `InteropValue`、固定 payload、`Reflection::Value` 或反射参数列表。数值调用不装箱、不创建参数数组。签名种类仅用于绑定阶段，不随每次参数值传输。Core 关闭 CLR 自动封送；新增类型需要同时实现两端布局与签名映射，不能仅放宽 unmanaged 白名单。

调用按以下顺序进行：

1. 绑定只保存组件身份和不透明成员槽位，不缓存游戏 DLL 函数地址。
2. 每次经稳定 Core 的 `Prepare` 校验线程、World、对象存活、实际类型、成员归属与代次，并取得本次活跃入口。
3. 通过具体签名的函数指针进入 C++ thunk，正常执行 GC 切换，允许原生回调托管。
4. `finally` 经 `Complete` 释放调用保护；即使回调内断开 API，本次保存的 Core 退出入口仍能完成清理。

只有不分配、不执行脚本的 `Prepare` / `Complete` 使用 `SuppressGCTransition`。原生异常转换为 `InvocationFailed`，调用期间拒绝加载或卸载游戏模块，避免回调卸载仍在执行的代码。模块与反射注册表遵守主线程串行修改契约。最近访问的组件指针通过对象销毁通知清理，并在上下文切换时清空。

### 8.6 动态协议与手写入口

运行时才知道签名的查询、Inspector 和通用跨语言操作保留 `ComponentMethod` / `MemberHandle` 与 `InteropValue` 协议。动态方法缓存不透明成员槽位，MetaGen 的 `abiInvoker` 从内联 payload 读写具体值，仍绕过 `Reflection::Value`。数值 From、精确类型 TryGet、相等比较与哈希不装箱；显式读取 Value 或使用托管反射时仍按需装箱。动态 Invoke 使用 params ReadOnlySpan，最多 16 个参数在栈上编码，更多参数使用受控数组。

已有高频调用可以按以下方式迁移：将 `ComponentMethod` 字段替换为具体的 `NativeMethod<...>` 或 `NativeAction<...>`；初始化时将 `TryResolveMethod` 改为 `TryBindMethod` / `TryBindAction`；调用时直接传值，并将 `InteropValue.TryGet` 改为具体类型的 `out` 结果。签名已知的原生字段可将 SetField / TryGetField 改为 TryBindField 加 NativeField.Set / Get；动态字段与 Inspector 保留原协议。FlightHud 和 SampleBehaviour 是方法缓存示例，上面的 ThrottleRateWriter 展示字段缓存。

手写字段的 FieldInfo 需提供 directKind、directGetter / directSetter，类型化入口分别为 `InteropStatus (Object*, WireValue*)` / `InteropStatus (Object*, WireValue)`；只读或只写入口可为空。生成字段会保留 setter、变更通知和 World 脏标记，手写入口须自行保持这些语义。

手写注册方法需要提供 `MethodInfo::abiInvoker` 才能动态调用，提供符合完整 ABI 签名的 `directInvoker` 才能类型化绑定。类型化入口形如 `InteropStatus ORBEDEN_NATIVE_CALL Thunk(Object*, WireArg0, ..., WireResult*)`，void 方法省略结果指针；bool 使用 `uint8`，字符串和 StringId 使用 `ScriptInterop::NativeUtf8`。入口必须使用普通静态函数、按实例实际类型调整 Object 指针，并捕获原生异常返回调用状态；不能直接填入普通 C++ 成员函数指针。通常由 MetaGen 生成这些入口即可。

C++ 调用 C# 的通用方法仍使用托管方法元数据；这一方向的反射调用与对象参数转换仍有成本。大量每帧调用应优先在组件所属语言内完成计算，或提供批量 Proxy 接口；不能把原生数值查询的零分配结论推广到字符串、托管反射及所有调用方向。

### 8.7 性能验证范围

2026-10-22，Windows x64，在 AviatorSage 的 `Tests/ScriptInteropChecks` 中用真实 FlightController 逐项读取 13 个 getter。每条路径预热 10,000 次，交错执行 5 轮，每轮 50,000 组，取单组中位耗时；所有路径的当前线程托管分配均为 0 字节：

| 路径 | CLR Release，μs / 13 次 | NativeAOT Release，μs / 13 次 |
| --- | ---: | ---: |
| 无校验直接调用参考 | 0.286 | 0.334 |
| 带校验直接调用参考 | 0.403 | 0.447 |
| 类型化 NativeMethod | 0.551 | 0.613 |
| 缓存 ComponentMethod | 0.744 | 0.815 |
| Proxy.Invoke(name) | 0.996 | 1.286 |

类型化路径相对同轮 ComponentMethod 耗时分别减少约 26% / 25%。它仍有成员校验及 Prepare / Complete 成本，不能视为与直接调用等价。CLR 为 .NET 10.0.13；NativeAOT 使用本机离线缓存的 10.0.12 工具包，各执行模式内分别比较。

新增字段写入的同轮 CLR Release 测量如下。每组连续写同一 `throttleRate` 字段 13 次，再统一直接读一次用于消费结果；所有路径保留 World 脏标记，均为零托管分配：

| 字段写入路径 | μs / 13 次写入 + 1 次直接读取 |
| --- | ---: |
| 无校验直接写参考 | 0.050 |
| 带校验直接写参考 | 0.233 |
| 类型化 NativeField.Set | 0.461 |
| Proxy.SetField(name)，名称缓存已命中 | 1.094 |

NativeField.Set 相对动态字段入口耗时减少约 58%。字段与上述 getter 测量的操作不同，分别比较，不用字段写入数字推算方法调用收益。

255 项互操作检查已在 CLR Debug/Release、NativeAOT Release 下通过，覆盖方法与字段的具体类型、字符串、混合参数、只读/只写、setter 拒绝、Changed、World 脏标记、异常、原生回调、重入、卸载保护及句柄失效。该测试不启动渲染或物理模拟，不统计原生内存分配，也不代表 HUD 或整帧加速；尚未完成 Editor Play 人工画面验收及完整 Player 发布。

## 9. 重载、CLR 与 NativeAOT

Editor 使用 CLR 和可卸载的游戏程序集上下文；Player 使用生成的 NativeAOT 静态导出薄层。两者都调用 `GameScriptRuntime`，使用同一套宿主模型、生命周期和互操作函数表。

初始化顺序为：结束现有 Wrapper → 连接原生函数表 → 清理成员缓存和 generation → 注册托管函数表 → 枚举现存宿主并构造 Wrapper → 构建阶段表。关闭时先 End 和断开 Wrapper，再清空 Registry/托管函数表/元数据，最后卸载程序集。宿主由 World 持有。

Wrapper 断开原生连接后 `IsAlive` 为 false。组件代理和成员句柄带 generation；World/运行时或模块重载后必须重新获取。不要跨程序集卸载保存 Type、delegate 或已失效的代理。

编辑器重载游戏程序集（`Ctrl+R` 编译出新的 DLL、按 Play、停止 Play 都会触发）时会**无条件清空撤销/重做栈**，Inspector 的属性文档缓存与自定义编辑器实例也一并重建。不丢的是：World 的未保存改动、面板布局与可见性、`.resinfo` 里的导入设置。因此 `Ctrl+R` 只在脚本确实编译过时才重载程序集——纯重导资源那条路径不碰程序集，撤销栈原样保留。

能重载的只有用户游戏程序集：`GameAssemblyLoadContext` 是唯一可收集的上下文。编辑器自身的 `Orbeden.Editor.dll` 由 hostfxr 加载进默认（非可收集）上下文，本仓库没有卸载或重启机制，改了编辑器 C# 代码必须重新构建 `OrbedenEditor.vcxproj`。

ABI 两端使用 Pack=8，结构字段顺序和函数槽位数必须一起修改。目前 Script 宿主表为 17 个指针槽，完整运行时表为 117 个（ABI 7，尾部依次指向独立的 3 槽 TypedMethodsApi 与 TypedFieldsApi；原 ScriptInteropApi 保持 9 槽，Editor 引擎子表仍为 ABI 5、74 槽）；Editor GUI 表为 82 个（包含列表控件与自定义 GUI 的 ID 作用域接口），组件表为 24 个，Gizmo 表为 5 个，面板表为 2 个，资源表为 19 个，应用表为 16 个，完整 Editor 表为 162 个（其中日志表 5 个、性能剖析表 8 个，排在组件表之后）。Editor 表的槽位序号是相对结构体起点的绝对偏移，改动排在前面的表会让后面所有表的偏移一起后移，C++ 的 `ORBEDEN_ASSERT_NATIVE_API_SLOT` 和 C# 的 `ValidateNativeApiLayout` 都要跟着改。

类型化 Proxy 更新后的 ABI 对照如下；函数槽位数不包含完整表头的版本号和尺寸字段。

| 表或模块 | ABI 版本 | 尺寸 / 槽位 |
| --- | --- | --- |
| `OrbedenNativeApi`，完整运行时表 | 7 | 8 字节表头 + 117 个指针槽，尾槽指向 TypedFieldsApi |
| `OrbedenEngineNativeApi`，Editor 引擎子表 | 5 | 8 字节表头 + 74 个指针槽 |
| `ScriptInteropApi`，动态互操作表 | 随外层表校验 | 9 个指针槽 |
| `TypedMethodsApi`，类型化调用表 | 随完整运行时表校验 | Resolve / Prepare / Complete，3 个指针槽 |
| `TypedFieldsApi`，类型化字段表 | 随完整运行时表校验 | Resolve / Prepare / Complete，3 个指针槽 |
| `OrbedenNativeGameModuleApi`，游戏 DLL 描述 | 6 | 按 NativeGameModule.h 校验，与新的 FieldInfo / MethodInfo 布局配套 |

升级时依次构建 Core（包含 MetaGen、托管运行库及所用配置的原生 SDK）、构建并重启 Editor、通过 Update Project 同步项目 SDK、重新生成游戏元数据并构建游戏 C++ / C#。用于发布时再重新 Build Player。更新的具体范围见[构建与打包说明](BuildAndPackaging.md#引擎更新与项目同步)。

断言只保证结构体尺寸一致，保证不了字段顺序：两端同名表的槽位顺序必须逐字对应，新槽一律追加在表尾。顺序错了三个断言会全部通过，却把某个槽调成另一个函数。

组件自定义 Inspector、Scene GUI 和 Gizmos 的注册方式、程序集隔离与示例见 [CustomEditor](CustomEditors.md)。

`EditorGuiNativeApi` 是本仓库唯一带绘制原语的表：彩色文本、批量矩形、坐标读取与命中测试都在这里。批量矩形用 `EditorRectPrimitive`（9 个 float32，Pack=4）而不是嵌套 `vector2`/`color`，避免两端 pack 不一致导致的静默错位。

### 生命周期清理入口

原生宿主由 World 持有，托管 Wrapper 由 ScriptRuntime 持有。移除组件会删除宿主；停止或重载脚本只结束运行态，宿主仍可用于下一次初始化。

- C# 单实例清理统一在 `DestroyScript`：标记已销毁 → 已启动时调用 `OnEnd` → 移除实例及宿主、Ens 索引 → 注销互操作句柄 → 断开原生 Wrapper。
- 宿主移除事件 `OnHostDetached`、Ens 销毁通知和批量 `ShutdownScripts` 都复用这个入口。`RemoveManagedScript` 只向 World 请求删除宿主，由宿主事件触发清理。
- 实例及索引立即移除；正在使用的阶段表通过 `Destroyed` 跳过旧记录，并在后续阶段边界重建，不再执行第二轮已销毁实例清扫。
- C++ 的单组件移除和 `ShutdownNativeScripts` 统一调用 `DetachNativeScript`，先注销调度，再对已启动实例调用一次 `OnEnd`。
- `ShutdownScripts` 只释放实例和阶段表，供初始化和关闭复用；`Shutdown` 还清理互操作、类型缓存并卸载游戏程序集。

销毁标记在用户 `OnEnd` 前设置，防止重入导致重复结束；Wrapper 在回调结束后断开，使 `OnEnd` 仍可访问宿主。

## 10. 模板与构建

新项目模板存放在 `OrbedenEditor/Templates/FlightTraining/`（World、资源、C#/C++ 脚本、原生 `GameNative.vcxproj` 工程），随 Editor 构建拷贝到输出目录；新建项目时递归复制整个模板目录，并对文本文件替换 `{{PROJECT_NAME}}` 占位符（`Project.oeproj` 与 `Script/Project.csproj` 同时改名为项目名）。模板源码不参与 Editor 编译（Orbeden.Editor.csproj 显式排除）。

模板提供自由飞行场景：飞机挂载原生 `FlightController` 和 `FlightTerrainStreamer`，相机挂载 `FlightOrbitCamera`，托管 `FlightHud` 显示飞行状态。原生脚本负责气动力、舵面、复位、地形分块加载和鼠标环绕相机，起落架悬挂由物理系统处理。托管 HUD 通过 `ComponentProxy` 绑定并缓存 `NativeMethod<float>` / `NativeMethod<int>`，直接读取原生状态，并用 `GUI` 自由绘制 API 绘制 PFD、仪表盘和受力数值。操作方式见 [用户手册](Manual/UserManual.md#6-运行和调试)。

首次创建或打开项目时，原生游戏类型可能尚未注册。Editor 会先接受项目元数据，将它提示为“Native scripts need to be compiled”，然后自动执行 MetaGen、MSBuild 编译、游戏 DLL 加载和启动 World 重载。构建失败时项目仍保持打开，可在 `Views > Build Game` 中修复工具链问题并重试 `Build Game C++`。启动 World 尚待 Native 重载时禁止保存，手动构建会跳过构建前保存，避免用空 World 覆盖磁盘场景。

游戏 C++ 构建步骤先运行 MetaGen 生成反射、字段访问器、动态与类型化方法 ABI thunk、生命周期 thunk，再由 MSBuild 编译游戏原生工程；不生成游戏专用 C# 包装或 Binding 注册。Editor 使用 DLL；Player 将游戏源码和反射生成代码编入目标。C# 项目使用 Core SDK；AOT 导出文件只保留固定阶段入口，游戏程序集需要作为裁剪根保留被反射访问的脚本成员。

### Ens 的独立对象身份

Ens 继承 Object；EnsId 仍是带版本的 World 内部句柄，不承担持久化身份。Ens 与 Transform 分别注册独立的 ObjectId 和稳定路径。C# 的 Ens 同样继承 Object，支持 Object 字段和 Ref<Ens> 原生字段引用。销毁 Ens 由 World 统一释放其组件及对象身份。旧文件共用的 ID 在读取时拆分：原 ID 留给 Ens，Transform 获得独立路径；既有 Transform/Object 引用按声明类型迁移，EnsId 引用继续指向 Ens。
