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

Core 的自动绑定按具体签名调用函数指针，schema 只在初次绑定及绑定代次变化时校验。C# 调用前检查包装身份和绑定代次；对象是否仍存在、实际原生类型是否匹配，由原生入口统一检查一次，C# 不再额外调用 `IsNativeAlive`。对象参数仍传 ObjectId，不使用未经存活校验的缓存指针。

复合值继续使用生成器定义的字段编码。固定尺寸的值结构和此类元素数组先按完整字节块检查边界，再直接读取块内字段，不逐字段重复检查；该布局按字段编码尺寸相加，不使用 C++ 结构体的内存填充。字符串、引用和可变长度字段按各自规则读取。块长度、整数溢出、固定数组长度、对象类型和缓冲区尾部检查仍保留。`NativeBindingReader` 的按块读取只用于生成布局，不用于跳过不可信数据的完整性验证。

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

游戏 C++ 类型只生成原生元数据和调用入口，不生成游戏专用 C# 包装。C# 通过 `Ens.GetNativeComponent` 获取 `ComponentProxy`；C++ 通过 `ScriptInterop::FindManagedComponent` 获取 C# 组件。

### 8.1 C# 调用 C++：类型化方法绑定

签名已知时，在初始化阶段绑定并缓存委托，调用时直接传普通 C# 值：

```csharp
ComponentProxy? native = ens.GetNativeComponent("MoveBehaviour");
if (native != null && native.TryBindMethod("EchoText", out NativeMethod<string, string> echo) == InteropStatus.Ok)
{
    string result = echo("中文\0🛩");
}

if (native != null && native.TryBindAction("StoreText", out NativeAction<string> store) == InteropStatus.Ok)
{
    store("Player");
}
```

`NativeMethod<T0, ..., TResult>` 的最后一个类型是返回类型；无返回值使用 `NativeAction<T0, ...>`。支持 0～16 个输入参数。绑定失败输出 null；调用失败抛出 `NativeCallException`，其 `Status` 保存原始 `InteropStatus`。委托和捕获的组件、成员句柄只在绑定时分配。

### 8.2 类型化字段绑定

```csharp
if (native != null && native.TryBindField("text", out NativeField<string> text) == InteropStatus.Ok)
{
    text.Value = "Enemy";
    string copy = text.Value;
}

if (native != null && native.TryBindField("numbers", out NativeField<int[]> numbers) == InteropStatus.Ok)
{
    numbers.Set([1, 2, 3]);
    int[] copy = numbers.Get();
}
```

`Get()`、`.Value` 和到 T 的单向隐式转换读取字段；`Set(value)` 和 `.Value = value` 写入，赋值不替换绑定。`CanRead` / `CanWrite` 表示绑定时存在的访问入口，`IsValid` 校验当前组件和成员代次。读写自身也执行校验。

原生写入保留显式 setter、Changed 通知和 World 脏标记。bool setter 拒绝输入时返回 `InvalidArgument`，不继续通知或标脏。异常转为 `InvocationFailed`；setter 或通知已执行的修改不自动回滚。只读、只写以及继承、私有序列化字段仍按元数据约束。

### 8.3 自动封送范围

公开泛型接口不再要求 `unmanaged`。Core 调用器与 MetaGen 生成的 C++ thunk 按同一规则自动封送，调用方无需构造 UTF-8 视图、固定缓冲或统一值容器。

| C++ 类型 | C# 类型 | 原生表示 |
| --- | --- | --- |
| `bool` | `bool` | 1 字节 |
| `int32` / `uint32` / `uint64` | `int` / `uint` / `ulong` | 同宽整数 |
| `float32` | `float` | 32 位浮点 |
| `vector2` / `vector3` / `color` / `quaternion` | 同名类型 | 已定义的结构布局 |
| `EnsId` | `EnsId` | 8 字节实体身份 |
| `std::string` | `string` | UTF-8 指针和字节数 |
| `StringId` | `StringId` | UTF-8 文本标识，与 string 签名不同 |
| `Ref<T>` | 对应的 Object 包装类型 | 运行时对象 ID |
| `List<T>` / `std::vector<T>` / `std::array<T, N>` | 支持元素类型的 `T[]` | 元素存储地址和数量 |

枚举仅在两端存在对应的受支持整数签名时按底层整数布局调用。数组签名同时校验元素分类；`int[]` 不可绑定到 `List<float32>`。固定大小的 `std::array` 写入还会检查长度。原始 C 数组、嵌套数组、任意自定义类或结构体、未声明的数值布局及 `ref` / `out` 回写不自动推断，绑定失败返回 `UnsupportedType`，或由 MetaGen 明确拒绝不支持的公开签名。新增类型必须同时定义托管布局、原生布局和签名映射。

输入 string 的 null 映射为空字符串，输入数组的 null 映射为空数组；空对象引用映射为 ID 0。字符串保留 Unicode 和内嵌 NUL。输入编码缓冲仅在调用期间固定；字符串、StringId、数组输出在退出本次调用保护之前复制为托管值，调用方可以直接保存返回结果。对象身份通过 Core 的实际类型包装机制恢复，不复制原生对象，也不延长其生命周期；对象写入由原生端检查目标类型。

成功的类型化标量调用不装箱，不分配参数数组或临时委托。字符串编码、数组转换、对象包装和异常路径可以分配。数组不是逐帧零分配接口，需要零拷贝批量访问时应单独设计缓冲 API。

### 8.4 ABI 和调用保护

底层关闭 CLR 运行时封送，类型转换由 Core 封送器与绑定入口完成，可用于 CLR 和 NativeAOT。已知签名不经过 `Reflection::Value`、动态类型标签或装箱的参数列表。参数类型、数量和返回类型只在绑定阶段校验；入口信任调用方传入的约定布局，不重复逐参数验证。

C# → C++：全部输入均为 blittable 时，参数按具体值类型传入函数指针，结果写入具体类型的局部变量，不建立封送缓冲或分配托管对象。包含引用参数的签名使用每参数原生布局地址；字符串和数组只进行必要的编码、固定及复制。bool 为 1 字节。字段值类型 setter 同样按值传参。返回值采用输出指针统一状态码和异常边界。

`nint/nuint` 对应 C++ 裸指针，直接传递位值，支持 `void*`、`const T*` 和对象指针；不经过 ObjectId 查询。其有效性和所有权由业务负责。`Object` 包装与 C++ `Ref<T>` 继续使用对象身份语义。裸指针字段不参与持久化。

C++ → C# 的 `ManagedMethod<Result(Args...)>` 在绑定时查找公开实例方法并创建强类型委托。调用时经固定的 `UnmanagedCallersOnly` 入口进入已编译的泛型适配函数，再调用该委托；不使用 `MethodInfo.Invoke`、`DynamicInvoke`、运行时代码生成或源生成。每个参数以原生布局地址传入，参数地址数组放在 C++ 栈上；标量借用输入值地址，不装箱、不构造托管参数数组。此方向采用固定地址 ABI，不是任意 C# 方法的原始函数指针，也不等同于 C# → C++ 全 blittable 输入按值传参的 ABI。用法见下文 8.5。

1. 绑定按名称和完整类型签名查找一次，只保存组件身份和不透明成员槽位。
2. 每次调用经稳定 Core 的 `Prepare` 校验线程、World、对象、成员归属和代次，取得本次入口。这里不检查参数类型和数量。
3. 在调用保护内经函数指针进入对应的类型化入口，并在退出保护前复制输出。
4. C# 的 `finally` 或 C++ 的 RAII 析构释放调用期存储，经本次保存的 `Complete` 退出保护。

只有短小、不回调脚本的 Prepare / Complete 使用 `SuppressGCTransition`。实际方法和字段入口执行正常 GC 切换。回调内断开 API 仍可经保存的 Core 入口完成退出；调用期间拒绝卸载模块。模块、World 和反射元数据遵守主线程契约，最多重入 32 层。

### 8.5 C++ 调用 C#：已知签名绑定

在已有 C# 脚本中声明普通公开实例方法，并在静态构造函数中登记需要的完整 CLR 签名。登记按签名复用，不按方法名登记；不需要给游戏方法添加 `UnmanagedCallersOnly`：

```csharp
using Orbeden;

public sealed class MessageScript : Script
{
    static MessageScript()
    {
        ManagedMethodSignatures.Register<string, int, string>();
        ManagedMethodSignatures.RegisterAction<string>();
        ManagedMethodSignatures.Register<int[], int[]>();
    }

    public MessageScript(Ens ens) : base(ens) { }

    public string Format(string text, int count) => $"{text}: {count}";
    public void Print(string text) => System.Console.WriteLine(text);
    public int[] EchoNumbers(int[] values) => values;
}
```

C++ 包含现有 `ScriptInteropInvoke.h`，获取组件后绑定并缓存 `ManagedMethod`：

```cpp
#include "Scripting/ScriptInteropInvoke.h"

using namespace ScriptInterop;
auto proxy = FindManagedComponent(ownerEnsId, "MessageScript");

ManagedMethod<std::string(std::string, int32)> format;
if (proxy.TryBindMethod("Format", format) == InteropStatus::Ok)
{
    std::string result = format("中文", 3);
}

ManagedMethod<void(std::string)> print;
if (proxy.TryBindMethod("Print", print) == InteropStatus::Ok)
{
    print("Player");
}

ManagedMethod<List<int32>(List<int32>)> echo;
if (proxy.TryBindMethod("EchoNumbers", echo) == InteropStatus::Ok)
{
    List<int32> copy = echo(List<int32>{1, 2, 3});
}
```

`ownerEnsId` 是组件所属实体的 `EnsId`，带命名空间的脚本使用完整类型名。`ManagedMethod<TResult(TArgs...)>` 支持 0～16 个参数，`void` 表示无返回值；调用失败抛出 `ManagedCallException`，其 `status` 保存原始状态。仅在绑定成功后保存并使用结果；组件销毁、注册表变化、World 切换或运行时重载后重新查找和绑定。

`ManagedMethodSignatures.Register<T0, ..., TResult>()` 的最后一个类型是返回类型；无参返回值使用 `Register<TResult>()`；无返回值使用 `RegisterAction<T0, ...>()`，无参使用 `RegisterAction()`。登记只需发生在首次 C++ 绑定之前，可放在脚本静态构造函数或游戏初始化代码中。静态泛型调用使 NativeAOT 提前编译对应适配函数；不新增工程、不扫描所有程序集、不生成源码。登记不会自动保留游戏方法的反射元数据，NativeAOT 仍需按项目现有规则保留脚本构造函数与公开方法。

默认已登记以下类型的 `() → T`、`(T) → T` 和 `(T) → void`：`bool`、`int`、`uint`、`ulong`、`float`、`nint`、`nuint`、`vector2`、`vector3`、`color`、`quaternion`、`EnsId`、`string`、`StringId`、`Object`、`Transform`、`Script`；以及 `bool[]`、`int[]`、`uint[]`、`ulong[]`、`float[]`、`nint[]`、`vector2[]`、`vector3[]`、`color[]`、`quaternion[]`、`EnsId[]`、`string[]`、`StringId[]`、`Object[]`、`Transform[]`。另外预置无参 `void` 和下列组合：

| 参数 | 返回类型 |
| --- | --- |
| `(int, int)` / `(uint, uint)` / `(ulong, ulong)` / `(float, float)` / `(vector3, vector3)` | 相应元素类型 |
| `(string, string)` / `(string, int)` / `(int, string)` | `string` |
| `(int[], int[])` | `int[]` |
| `(string[], string[])` | `string[]` |
| `(float, int, bool, vector3)` | `float` |
| `(string, int, vector3, bool)` | `string` |

其余组合须显式登记，重复登记安全。登记必须匹配 C# 方法的精确类型，例如枚举不能只登记其底层 `int`，用户派生的 Object 参数也不能只登记基类。未登记的受支持签名绑定返回 `UnsupportedType`，不会自动退回反射调用。C++ 侧签名按 ABI 类型分类匹配；同名重载若仅区别枚举与整数、不同 Object 派生类型等相同 ABI 分类，会返回 `AmbiguousMethod`，应使用不同方法名。

字符串、StringId、数组和 `Ref<T>` 沿用 8.3 的布局规则自动 Marshal。C++ 字符串转为 C# 字符串，数组转为托管数组；返回数据固定到 C++ 完成复制后释放，得到的 `std::string` / `List<T>` 可直接保存。C# 返回的 null 字符串和数组映射为空容器，null 对象映射为空 `Ref<T>`。`void*`、`T*` 与 C# `nint/nuint` 传递指针位值，不查询 ObjectId；`Ref<T>` 与 C# 对象包装保持对象身份语义。任意自定义结构、嵌套数组、`ref/out` 和泛型方法不在自动封送范围内。

### 8.6 动态属性和事件

签名在运行时才确定时，`ComponentMethod.Invoke`、`ComponentProxy.Invoke`、`TryGetField`、`SetField` 使用普通 `object?` 值和 `ReadOnlySpan<object?>` 参数。Inspector 属性文档、撤销快照和 OrbEvent 也直接保存普通托管值。数值在动态路径按 CLR 对象规则装箱；类型化路径不承担这些转换。

```csharp
native?.SetField("speed", 4.0f);
InteropStatus status = native!.Invoke("EchoText", out object? result, "Player");
if (status == InteropStatus.Ok && result is string text)
{
    Console.WriteLine(text);
}
```

动态签名分类使用 `NativeValueKind`；它是元数据分类，不是值容器。数组属性的槽位数为普通 int，Array 分类保留在属性描述中。动态跨域调用内部仍需要有类型信息的私有协议，两端负责与普通托管值、`Reflection::Value` 转换；该协议不用于 NativeMethod / NativeField / ManagedMethod。现有动态方法协议仅支持单个标量、字符串和对象引用；容器方法使用类型化绑定。C++ → C# 动态调用继续由托管元数据完成参数校验和反射调用。

### 8.7 失败和失效

| 状态 | 含义 |
| --- | --- |
| `NotFound` | 名称或参数签名未匹配 |
| `TypeMismatch` | 返回类型、字段类型或数组元素类型不匹配 |
| `UnsupportedType` | 未定义封送规则、未登记托管签名、组件域不支持或缺少访问入口 |
| `AmbiguousMethod` | 同名重载映射为相同 ABI 签名 |
| `StaleHandle` | 默认绑定、组件销毁、World 切换、注册表或运行时重载 |
| `WrongThread` | 调用不在互操作主线程 |
| `ReentrancyLimit` | 重入超过 32 层 |
| `InvalidArgument` | setter 拒绝输入，或数组长度、对象类型等输入无效 |
| `InvocationFailed` | 原生或托管方法、访问器或通知抛异常 |

非 null 委托只表示绑定成功；组件销毁、World 切换或反射修改后必须重新获取和绑定。绑定不拥有组件，不跨调用保存游戏 DLL 原始入口。

### 8.8 验证记录

自动绑定优化阶段已验证封送、字段通知、编辑器事务、生命周期与批量读取边界。当前预编译委托实现的 Core、Editor 编译通过；使用现有编译工具及临时源码完成 CLR 31 项原生检查和 9 项宿主检查、NativeAOT 31 项原生检查和 8 项宿主检查。覆盖标量、字符串（Unicode 与内嵌 NUL）、数组、对象及空引用、裸指针、枚举、16 参数、签名扩展、错误类型、异常、32 层重入限制、线程与失效校验、调用期卸载拒绝，以及回调中断开 API 后的结果复制。标量调用循环确认零托管分配。验证不新增项目，结束后删除临时源码和脚本；未启动渲染、物理模拟或完整 Player。

性能使用相同方法体和 Core 生命周期保护比较旧按值 ABI 与当前实现；功能测试不代表整帧性能或完整 Player 发布结果。

## 9. 重载、CLR 与 NativeAOT

C# 与 C++ 脚本不生成 `.resinfo`，mtime 和内容哈希（SHA-256，CRLF 按 LF 计算）统一保存到本机 `ResourceCache/ScriptBuild/Observed-v1.json`。Ctrl+R 先按每个文件的 mtime 筛选，新增或 mtime 变化的候选再读取内容并计算哈希；内容指纹相同则更新观察时间并跳过编译，mtime 未变的内容修改不会被筛选发现。文件集合变化继续覆盖新增、删除与重命名。观察记录独立于各语言的成功构建快照，资源目录刷新及编辑器重启后继续复用；失败、取消或构建期间检测到输入变化不更新成功快照。资源目录扫描清理旧版脚本伴生文件，可导入资源仍保留自己的 `.resinfo`。C++ 沿用同步 MSBuild 与模块重载，C# 后台构建；手动构建按钮强制构建并更新成功快照。

Editor 使用 CLR 和可卸载的游戏程序集上下文；Player 使用生成的 NativeAOT 静态导出薄层。两者都调用 `GameScriptRuntime`，使用同一套宿主模型、生命周期和互操作函数表。

初始化顺序为：结束现有 Wrapper → 连接原生函数表 → 清理成员缓存和 generation → 注册托管函数表 → 枚举现存宿主并构造 Wrapper → 构建阶段表。关闭时先 End 和断开 Wrapper，再清空 Registry/托管函数表/元数据，最后卸载程序集。宿主由 World 持有。

Wrapper 断开原生连接后 `IsAlive` 为 false。组件代理和成员句柄带 generation；World/运行时或模块重载后必须重新获取。不要跨程序集卸载保存 Type、delegate 或已失效的代理。

编辑器重载游戏程序集（`Ctrl+R` 编译出新的 DLL、按 Play、停止 Play 都会触发）时会**无条件清空撤销/重做栈**，Inspector 的属性文档缓存与自定义编辑器实例也一并重建。不丢的是：World 的未保存改动、面板布局与可见性、`.resinfo` 里的导入设置。因此 `Ctrl+R` 只在脚本确实编译过时才重载程序集——纯重导资源那条路径不碰程序集，撤销栈原样保留。

能重载的只有用户游戏程序集：`GameAssemblyLoadContext` 是唯一可收集的上下文。编辑器自身的 `Orbeden.Editor.dll` 由 hostfxr 加载进默认（非可收集）上下文，本仓库没有卸载或重启机制，改了编辑器 C# 代码必须重新构建 `OrbedenEditor.vcxproj`。

ABI 两端使用 Pack=8，结构顺序和函数槽位数必须同步。完整运行时 `OrbedenNativeApi` 为 ABI 14、123 个指针槽；Editor 引擎子表为 ABI 8、80 个指针槽。类型化入口签名已经改变，Core、Editor 与游戏原生模块必须共同重建，不能混用旧 DLL。C++ 布局断言与 C# `ValidateNativeApiLayout` 同步校验。

类型化 Proxy 更新后的 ABI 对照如下；函数槽位数不包含完整表头的版本号和尺寸字段。

| 表或模块 | ABI 版本 | 尺寸 / 槽位 |
| --- | --- | --- |
| `OrbedenNativeApi`，完整运行时表 | 14 | 8 字节表头 + 123 个指针槽 |
| `OrbedenEngineNativeApi`，Editor 引擎子表 | 8 | 8 字节表头 + 80 个指针槽 |
| `ScriptInteropApi`，动态互操作表 | 随外层表校验 | 9 个指针槽 |
| `ManagedScriptInteropApi`，托管回调表 | 随完整运行时表校验 | 14 个指针槽，包含动态调用、宿主通知与类型化绑定 / 调用 / 完成入口 |
| `TypedMethodsApi`，类型化调用表 | 随完整运行时表校验 | Resolve / Prepare / Complete，3 个指针槽 |
| `TypedFieldsApi`，类型化字段表 | 随完整运行时表校验 | Resolve / Prepare / Complete，3 个指针槽 |
| `OrbedenNativeGameModuleApi`，游戏 DLL 描述 | 10 | 按 NativeGameModule.h 校验，与自动封送的调用签名配套 |

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

游戏 C++ 构建步骤先运行 MetaGen 生成反射、字段访问器、动态与类型化方法 ABI thunk、生命周期 thunk，再由 MSBuild 编译游戏原生工程；不生成游戏专用 C# 包装或 Binding 注册。Editor 使用 DLL；Player 将游戏源码和反射生成代码编入目标。C# 项目使用 Core SDK；NativeAOT 导出薄层只提供固定阶段入口，游戏程序集需要保留反射访问的脚本构造函数和成员，动态功能遵循裁剪规则。

### Ens 的独立对象身份

Ens 继承 Object；EnsId 仍是带版本的 World 内部句柄，不承担持久化身份。Ens 与 Transform 分别注册独立的 ObjectId 和稳定路径。C# 的 Ens 同样继承 Object，支持 Object 字段和 Ref<Ens> 原生字段引用。销毁 Ens 由 World 统一释放其组件及对象身份。旧文件共用的 ID 在读取时拆分：原 ID 留给 Ens，Transform 获得独立路径；既有 Transform/Object 引用按声明类型迁移，EnsId 引用继续指向 Ens。
