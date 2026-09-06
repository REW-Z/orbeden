# 脚本系统

更新：2026-09-07。本文描述原生宿主重构后的唯一实现路径。

## 1. 组件与身份

C++ 脚本继承原生 `ScriptBehaviour`；C# 脚本继承 `Orbeden.ScriptBehaviour`，每个实例绑定一个精确类型为原生 `ScriptBehaviour` 的组件。

```text
Ens
├─ TransformComponent
├─ MoveBehaviour (C++ 派生组件，Native)
├─ ScriptBehaviour (Managed，Game.NpcAi)
└─ ScriptBehaviour (Managed，Game.NpcAi)
```

原生 `ScriptBehaviour` 可以通过反射工厂构造，基类没有虚生命周期方法。`domain` 根据实际原生类型确定：精确宿主是 Managed，C++ 派生类型是 Native，外部不能修改域。

每个宿主有独立的 `ObjectId`、稳定路径和组件挂载位置。C# Wrapper 的 `InstanceId` 等于宿主的 `ObjectId`。所有 C# 脚本共享原生 `ScriptBehaviour::TypeId`，具体托管类型由 `managedTypeName` 区分。同一 Ens 可以挂载多个两种语言的脚本，包括多个同类型 C# 脚本。

| 身份 | 用途 | 是否持久化 |
| --- | --- | --- |
| ObjectId / InstanceId | 本进程互操作、Wrapper、组件句柄 | 否 |
| TypeId | 本进程原生类型注册表 | 否 |
| Component stableId | 保存、引用、删除恢复、属性历史 | 是 |
| managedTypeName | C# 完整类型名 | 是 |
| generation | 检测重载后的过期代理和成员句柄 | 否 |

不再使用独立托管挂载列表、MountId 或 `.world.scripts.json`。不提供旧格式检测、迁移或兼容读取。

## 2. 编写 C# 脚本

```csharp
using Orbeden;

namespace Game;

public sealed class NpcAi : ScriptBehaviour
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
        vector3 position = Ens.Transform.localPosition;
        position.x += speed * deltaTime;
        Ens.Transform.localPosition = position;
    }

    public void AddCounter(int amount) => savedCounter += amount;
}
```

脚本必须是具体类型，并提供 public `(Ens ens)` 构造函数。运行时先创建/定位宿主，再把宿主指针放入线程局部构造上下文，随后调用用户构造函数。基类只允许消费一次匹配的构造上下文；直接 `new NpcAi(ens)` 会抛异常。

`enabled` 的权威值在原生宿主，C# 属性直接代理原生读写。调度器保存由结构事件更新的活动状态，阶段循环无需逐脚本查询原生状态。

编辑器添加 C# 组件时也在真实宿主上下文中执行构造函数，补齐字段初始化器和构造函数产生的序列化默认值，但不执行生命周期。构造函数应只做初始化；场景行为放入 `OnStart`。

public 的受支持字段参与序列化；非 public 字段需要 `[SerializeField]`。`[HideInInspector]` 只隐藏字段，不取消持久化，也不影响删除 Undo 的快照。`domain`、`managedTypeName`、`enabled` 是保留字段名，不要在派生类声明同名字段。

支持基本数值、字符串、vector3、color4、quaternion、EnsId 和可绑定的原生 Object 引用。数组、列表、自定义结构体、委托和任意托管对象图不属于当前字段协议。

## 3. 编写 C++ 脚本

```cpp
// MoveBehaviour.h
#pragma once
#include "Scripting/ScriptBehaviour.h"

class MoveBehaviour final : public ScriptBehaviour
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

OBJECT_TYPE_IMPLEMENT(MoveBehaviour, ScriptBehaviour)

void MoveBehaviour::OnUpdate(float32 deltaTime)
{
    TransformComponent* transform = GetEns()->Transform();
    vector3 position = transform->GetLocalPosition();
    position.x += speed * deltaTime;
    transform->SetLocalPosition(position);
}
```

游戏模块的 MetaGen 收集 public 受支持字段；非 public 字段用 `ORBEDEN_SERIALIZE_FIELD` 标记。MetaGen 为声明的生命周期生成静态 thunk，注册到类型回调表；未声明的阶段沿继承链解析。

引擎基类的身份、宿主字段表及运行时状态不自动生成持久化字段。`ScriptBehaviour.enabled` 由手写反射注册，写入经过 `SetEnabled`。修改头文件后重新运行 MetaGen，不手改 `Reflection.Generated.cpp`。

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

没有配置 `managedTypeName` 或找不到对应 C# 类型的宿主不进入调度。

## 5. 添加、查询和删除

```csharp
NpcAi? created = ens.AddComponent<NpcAi>();
NpcAi? first = ens.GetComponent<NpcAi>();
NpcAi[] all = ens.GetComponents<NpcAi>();
if (created != null) ScriptRuntimeRegistry.RemoveScript(created);
```

查询返回真实 C# 实例，多个同类型实例按原生挂载顺序返回。`[UniqueComponent]` 声明唯一组件；`[DependsOnComponent(typeof(...))]` 声明依赖。依赖图先验证后创建，循环依赖被拒绝。

C++ 用 `ens->AddComponentInstance<MoveBehaviour>()` 显式添加独立实例，`GetComponentInstances` 枚举同类型组件。精确 `ScriptBehaviour` 不出现在普通 C++ 添加菜单中。

Editor 多选添加先检查全部目标的 Unique 冲突及依赖图，再按依赖优先顺序创建；任一失败回滚本次创建。撤销同时移除这次新增的依赖，保留原有依赖。删除/恢复使用完整组件快照并恢复原挂载位置。

## 6. 统一 .world 存储

```xml
<Ens stableId="world://ens/npc" name="Npc">
    <Component type="ScriptBehaviour" stableId="world://ens/npc/ScriptBehaviour/ai">
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

上述是组件存储片段，完整 World 仍包含 Transform、World 根节点和场景层级。保存时 C++ 和 C# 组件共用 `<Component>/<Field>` 表达。读取时检查 domain 与真实原生类型一致。

Object 引用保存资源 Key 或组件稳定路径，不保存运行时 ObjectId。组件稳定路径随组件保存和恢复，因此跨 Ens 和同类型多实例引用不会依赖本次加载的 ObjectId。托管 EnsId 字段保存目标 Ens 的稳定路径，进入运行态再解析。空引用使用空字符串。临时 orphan 资源没有可持久化身份，不能写入宿主引用字段。

资源加载会扫描宿主动态 Object 字段；资源移动/重命名时，Editor 对宿主字段执行同一套路径映射。Missing Script 仍保留宿主、完整类型名、字段类型、可见性和字段值，缺少程序集不会丢弃数据。

进入 Play 时将宿主字段应用到 Wrapper。PIE Inspector 修改会同时写宿主和活跃 C# 实例。游戏代码直接给普通 C# 字段赋值只改变本次运行对象，不自动写回宿主。显式代理 SetField 会同步宿主。停止 PIE 后按编辑器的 World 恢复流程丢弃运行修改。

## 7. Inspector 与事务

Inspector 按原生挂载顺序显示 `[C++] Type` 或 `[C#] Type`，每个 C# 宿主只显示一个卡片。找不到类型时显示 `Missing Script`，允许查看保存字段、删除和撤销恢复；重新加载有效程序集后可重新建立 Wrapper。

Transform、Renderer、RigidBody、Collider、CharacterController 使用相应字段顺序、资源选择和枚举控件。所有写入继续经过 `PropertyDocument`：读取多目标快照、显示 Mixed、验证、提交；失败时回滚并显示错误。

属性历史以组件稳定身份定位目标，因此“编辑字段 → 删除组件 → Undo 删除 → Undo 字段编辑”不依赖已过期的 ObjectId。完整组件 XML 快照包含隐藏字段。Undo/Redo 失败不应提前弹出历史记录。

C# 组件修改只设置 WorldDirty。保存不生成第二份脚本文件，也不存在两份 Inspector 数据合并。

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

初始化顺序为：结束旧 Wrapper → 连接原生函数表 → 清理旧成员缓存和 generation → 注册托管函数表 → 枚举现存宿主并构造 Wrapper → 构建阶段表。关闭时先 End 和断开 Wrapper，再清空 Registry/托管函数表/元数据，最后卸载程序集。宿主由 World 持有。

旧 Wrapper 断开原生连接后 `IsAlive` 为 false。组件代理和成员句柄带 generation；World/运行时或模块重载后必须重新获取。不要跨程序集卸载保存 Type、delegate 或旧代理。

ABI 两端使用 Pack=8，结构字段顺序和函数槽位数必须一起修改。目前 ScriptBehaviour 宿主表为 16 个指针槽，完整运行时表为 256 个；Editor 组件表为 19 个，完整 Editor 表为 61 个。C++ static_assert 和 C# 初始化布局检查保持对应。

## 10. 模板与构建

新项目模板在同一 Cube 上挂载 `SampleNativeBehaviour` 和项目命名空间的 `SampleBehaviour`，包含字段读写和双向方法调用示例；World 直接保存 C# 宿主。

游戏 C++ CMake 步骤先运行 MetaGen 生成反射/生命周期 thunk，再编译游戏模块。Editor 使用 DLL；Player 将游戏源码和生成代码编入目标。C# 项目使用 Core SDK；AOT 导出文件只保留固定阶段入口，游戏程序集需要作为裁剪根保留被反射访问的脚本成员。

本轮按用户要求仅进行编译验证，不执行 Editor/Player 运行、性能或 Undo 场景测试。构建结果和尚未执行的验收项见 [重构 TODO](ScriptSystemRefactor/ScriptBehaviourHostRefactorTODO.md)。