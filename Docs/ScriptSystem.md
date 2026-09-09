# 脚本系统

本文说明 C++ 与 C# 脚本的组件模型、生命周期、场景存储、跨语言调用和构建方式。

## 1. 组件与身份

C++ 脚本继承原生 `Script`；C# 脚本继承 `Orbeden.Script`，每个实例绑定一个精确类型为原生 `Script` 的组件。

```text
Ens
├─ TransformComponent
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

`TypeRuntimeId` 按运行时类型注册表的槽位分配，不保证跨进程或重新构建后保持一致。组件序列化使用注册类型名，例如 `<Component type="TransformComponent">`；加载时通过类型名查找当前注册的 `Type`，再创建组件并读取字段。因此，TypeRuntimeId 的数值变化不影响场景加载。类型名必须能在加载时解析，重命名组件类型时需要同步修改场景中的类型引用。`stableId` 标识具体组件实例，类型名标识组件的种类。

### 原生脚本 Binding 与类型身份

C++ 根对象的实际类名为 `Orbeden::Object`，头文件通过 `using Orbeden::Object` 允许现有原生代码简写为 `Object`；反射注册名仍为 `Object`。C# 根对象为 `Orbeden.Object`。

| 实例 | 实际原生类型 / TypeRuntimeId | 托管侧表示 | 生命周期执行域 |
| --- | --- | --- | --- |
| C++ `MoveBehaviour : Script` | `MoveBehaviour::StaticType()->GetId()` | `ComponentProxy`，或调用者自行封装的普通 C# 类 | Native |
| C# `Game.NpcAi : Orbeden.Script` | `Script::StaticType()->GetId()` | `Game.NpcAi`，由 `managedTypeName` 与 CLR Type 区分 | Managed |

继承关系不要求 TypeRuntimeId 相等。`MoveBehaviour::StaticType()->Is(Script::StaticType())` 为 true，但两个类型的 ID 不同。基类查询可以匹配派生实例，成员解析仍使用实例的 `GetType()`。互操作组件句柄保存的是域、generation 和 ObjectId，并不是 TypeRuntimeId。

MetaGen 为全部 Object 派生类生成强类型 C# 包装、原生调用入口和注册清单。C++ `MoveBehaviour` 会生成同名 C# 包装，可直接 `ens.AddComponent<MoveBehaviour>()`、访问属性和调用方法；基类查询返回派生包装并保持同一原生实例的包装身份。生成的原生脚本包装继承 `Orbeden.Script` 但只参与 Native 生命周期，不作为托管脚本启动；手写托管脚本继承原生包装的声明会被拒绝。`ens.GetNativeComponent("MoveBehaviour")` 的动态代理保留给没有生成 Binding 的 C++ API（非 Object 派生类）。

本次迁移的 API 变化：基类名由 `ScriptBehaviour` 改为 `Script`，外部项目需更新源码及 `.world` 中精确宿主的 `type="ScriptBehaviour"` 为 `type="Script"`；具体 C++ 派生类名与 C# `managedTypeName` 不变。C# 颜色类型 `color4` 改为 `color`；`Mesh.Load` / `Material.Load` / `Shader.Load` 静态加载改为 `Resources.Load<T>(key)`；Transform 的本地/世界变换不再以字段属性暴露，统一使用 `GetLocalPosition` / `SetLocalPosition` 等 getter/setter 方法；全部 Object 派生组件改用生成式强类型 Binding，旧的逐类型 Binding 已删除。原生模块 ABI 已升至 2（表头、尺寸、偏移校验），Core、Editor 与外部游戏模块必须一起重建，旧 ABI 模块会被拒绝加载。

## 2. 编写 C# 脚本

```csharp
using Orbeden;

namespace Game;

public sealed class NpcAi : Script
{
    public float speed = 2.0f;
    [SerializeField, HideInInspector] private int savedCounter;
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

public 的受支持字段参与序列化；非 public 字段需要 `[SerializeField]`。`[HideInInspector]` 只隐藏字段，不取消持久化，也不影响删除 Undo 的快照。`domain`、`managedTypeName`、`enabled` 是保留字段名，不要在派生类声明同名字段。

支持基本数值、字符串、vector3、color、quaternion、EnsId 和可绑定的原生 Object 引用。数组、列表、自定义结构体、委托和任意托管对象图不属于当前字段协议。

## 3. 编写 C++ 脚本

```cpp
// MoveBehaviour.h
#pragma once
#include "Scripting/Script.h"

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
#include "Runtime/Object/TransformComponent.h"

OBJECT_TYPE_IMPLEMENT(MoveBehaviour, Script)

void MoveBehaviour::OnUpdate(float32 deltaTime)
{
    TransformComponent* transform = GetEns()->Transform();
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

Object 引用保存资源 Key 或组件稳定路径，不保存运行时 ObjectId。组件稳定路径随组件保存和恢复，因此跨 Ens 和同类型多实例引用不会依赖本次加载的 ObjectId。托管 EnsId 字段保存目标 Ens 的稳定路径，进入运行态再解析。空引用使用空字符串。临时 orphan 资源没有可持久化身份，不能写入宿主引用字段。

资源加载会扫描宿主动态 Object 字段；资源移动/重命名时，Editor 对宿主字段执行同一套路径映射。Missing Script 仍保留宿主、完整类型名、字段类型、可见性和字段值，缺少程序集不会丢弃数据。

进入 Play 时将宿主字段应用到 Wrapper。PIE Inspector 修改会同时写宿主和活跃 C# 实例。游戏代码直接给普通 C# 字段赋值只改变本次运行对象，不自动写回宿主。显式代理 SetField 会同步宿主。停止 PIE 后按编辑器的 World 恢复流程丢弃运行修改。

## 7. Inspector 与事务

Inspector 按原生挂载顺序显示 `[C++] Type` 或 `[C#] Type`，每个 C# 宿主只显示一个卡片。找不到类型时显示 `Missing Script`，允许查看保存字段、删除和撤销恢复；重新加载有效程序集后可重新建立 Wrapper。

Transform、Renderer、RigidBody、Collider、CharacterController 使用相应字段顺序、资源选择和枚举控件。所有写入经过 `PropertyDocument`：读取多目标快照、显示 Mixed、验证、提交；失败时回滚并显示错误。

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

高频互操作应缓存 `MemberHandle` / `ComponentField` / `ComponentMethod`。动态 `Invoke(name)` 只适用于低频通用调用。缓存句柄能避免重复成员查找，但不能消除 ABI 编解码、装箱和通用托管方法反射调用成本；大量每帧调用应使用明确的强类型 Binding 或批量入口。

## 9. 重载、CLR 与 NativeAOT

Editor 使用 CLR 和可卸载的游戏程序集上下文；Player 使用生成的 NativeAOT 静态导出薄层。两者都调用 `GameScriptRuntime`，使用同一套宿主模型、生命周期和互操作函数表。

初始化顺序为：结束现有 Wrapper → 连接原生函数表 → 清理成员缓存和 generation → 注册托管函数表 → 枚举现存宿主并构造 Wrapper → 构建阶段表。关闭时先 End 和断开 Wrapper，再清空 Registry/托管函数表/元数据，最后卸载程序集。宿主由 World 持有。

Wrapper 断开原生连接后 `IsAlive` 为 false。组件代理和成员句柄带 generation；World/运行时或模块重载后必须重新获取。不要跨程序集卸载保存 Type、delegate 或已失效的代理。

ABI 两端使用 Pack=8，结构字段顺序和函数槽位数必须一起修改。目前 Script 宿主表为 16 个指针槽，完整运行时表为 274 个；Editor 组件表为 19 个，完整 Editor 表为 202 个。C++ static_assert 和 C# 初始化布局检查保持对应。

### 生命周期清理入口

原生宿主由 World 持有，托管 Wrapper 由 ScriptRuntime 持有。移除组件会删除宿主；停止或重载脚本只结束运行态，宿主仍可用于下一次初始化。

- C# 单实例清理统一在 `DestroyScript`：标记已销毁 → 已启动时调用 `OnEnd` → 移除实例及宿主、Ens 索引 → 注销互操作句柄 → 断开原生 Wrapper。
- 宿主移除事件 `OnHostDetached`、Ens 销毁通知和批量 `ShutdownScripts` 都复用这个入口。`RemoveManagedScript` 只向 World 请求删除宿主，由宿主事件触发清理。
- 实例及索引立即移除；正在使用的阶段表通过 `Destroyed` 跳过旧记录，并在后续阶段边界重建，不再执行第二轮已销毁实例清扫。
- C++ 的单组件移除和 `ShutdownNativeScripts` 统一调用 `DetachNativeScript`，先注销调度，再对已启动实例调用一次 `OnEnd`。
- `ShutdownScripts` 只释放实例和阶段表，供初始化和关闭复用；`Shutdown` 还清理互操作、类型缓存并卸载游戏程序集。

销毁标记在用户 `OnEnd` 前设置，防止重入导致重复结束；Wrapper 在回调结束后断开，使 `OnEnd` 仍可访问宿主。

## 10. 模板与构建

新项目模板存放在 `OrbedenEditor/Templates/FlightTraining/`（World、资源、C#/C++ 脚本、CMake 配置），随 Editor 构建拷贝到输出目录；新建项目时递归复制整个模板目录，并对文本文件替换 `{{PROJECT_NAME}}` 占位符（`Project.oeproj` 与 `Script/Project.csproj` 同时改名为项目名）。模板源码不参与 Editor 编译（Orbeden.Editor.csproj 显式排除）。

模板提供自由飞行场景：飞机挂载原生 `FlightController` 和 `FlightTerrainStreamer`，相机挂载 `FlightOrbitCamera`，托管 `FlightHud` 显示飞行状态。原生脚本负责气动力、舵面、复位、地形分块加载和鼠标环绕相机，起落架悬挂由物理系统处理。托管 HUD 通过生成的 `Native.FlightController` 强类型包装直接读取原生状态，并用 `GUI` 自由绘制 API 绘制 PFD、仪表盘和受力数值。操作方式见 [用户手册](UserManual.md#6-运行和调试)。

首次创建或打开项目时，原生游戏类型可能尚未注册。Editor 会先接受项目元数据，将它提示为“Native scripts need to be compiled”，然后自动执行 MetaGen、CMake 编译、游戏 DLL 加载和启动 World 重载。构建失败时项目仍保持打开，可在 `Views > Build Game` 中修复工具链问题并重试 `Build Game C++`。启动 World 尚待 Native 重载时禁止保存，手动构建会跳过构建前保存，避免用空 World 覆盖磁盘场景。

游戏 C++ CMake 步骤先运行 MetaGen 生成反射、生命周期 thunk 和 Binding 注册，再编译游戏模块。Editor 使用 DLL；Player 将游戏源码和生成代码编入目标。C# 项目使用 Core SDK；AOT 导出文件只保留固定阶段入口，游戏程序集需要作为裁剪根保留被反射访问的脚本成员。
