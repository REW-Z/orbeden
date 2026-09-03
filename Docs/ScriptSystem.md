# Orbeden 脚本系统说明

本文用尽量直白的方式说明 Orbeden 当前的脚本机制，包括：

- C++ 游戏脚本是怎样被发现和调用的。
- C# 游戏脚本在 Editor CLR 模式和 Player NativeAOT 模式下是怎样被调用的。
- `OnStart`、`OnUpdate`、`OnEnd` 等生命周期在什么情况下发生。
- InspectorPanel 怎样同时显示和编辑 C++ 组件、C# 脚本以及已有 C# 绑定的原生组件。

## 一句话理解

Orbeden 不会在每一帧临时查找“这个脚本有没有 `OnUpdate`”。

它会在脚本加载时把具体函数预先找出来，然后按阶段整理成几张调用表。运行一帧时，`ScriptSystem` 只需要按顺序遍历这些表。

```text
ScriptSystem
├─ C++ 脚本域
│  ├─ Update 表：      {对象指针, C++ thunk}[]
│  ├─ FixedUpdate 表： {对象指针, C++ thunk}[]
│  ├─ LateUpdate 表：  {对象指针, C++ thunk}[]
│  └─ DrawGUI 表：     {对象指针, C++ thunk}[]
└─ C# 脚本域
   ├─ Update 表：      闭合 Action<float>[]
   ├─ FixedUpdate 表： 闭合 Action<float>[]
   ├─ LateUpdate 表：  闭合 Action<float>[]
   └─ DrawGUI 表：     闭合 Action[]
```

两种语言在各自运行域中的核心形式其实相同：

```text
脚本实例 + 已经解析好的具体函数
```

C++ 保存对象指针和普通函数指针；C# 保存 GC 能够跟踪的对象和闭合 delegate。

## 三种运行情况

| 脚本种类 | 常见使用场景 | 函数如何预解析 | 每阶段如何进入 |
| --- | --- | --- | --- |
| C++ 游戏脚本 | 高性能组件、底层玩法代码 | MetaGen 在编译期生成具体类型 thunk | `ScriptSystem` 直接遍历 C++ 调用表 |
| C# CLR 脚本 | Windows Editor、PIE、快速迭代 | 程序集加载时反射一次，再创建闭合 delegate | C++ 通过 CLR 绑定入口进入托管域一次 |
| C# NativeAOT 脚本 | 最终 Player | 与 CLR 相同，运行时初始化时反射一次并创建闭合 delegate | C++ 直接调用 NativeAOT 导出入口一次 |

CLR 和 AOT 不是两套 C# 生命周期实现。二者最后都会进入同一个 `ScriptRuntime`，区别主要在于“C++ 怎样进入 C#”。

## 总调度顺序

`ScriptSystem` 内部固定注册两个脚本域：

1. C++ Domain。
2. C# Domain。

因此同一个阶段总是先执行 C++ 脚本，再执行 C# 脚本。

一帧的关键顺序如下：

```text
固定步长循环：
    C++ OnFixedUpdate
    C#  OnFixedUpdate
    Physics 和其他系统的 FixedUpdate

普通帧：
    各系统 Update
        ScriptSystem 内部：C++ OnUpdate → C# OnUpdate

    各系统 LateUpdate
        ScriptSystem 内部：C++ OnLateUpdate → C# OnLateUpdate

渲染 Overlay：
    C++ OnDrawGUI
    C#  OnDrawGUI
```

脚本初始化和停止同样遵守 C++ 域在前、C# 域在后的顺序。

当前没有跨语言的 `executionOrder`。C++ 脚本在各自列表中保持原生组件挂载顺序，C# 脚本保持 sidecar 中的挂载顺序。

## C++ 脚本机制

### 游戏开发者怎样写 C++ 脚本

C++ 脚本继承原生 `ScriptBehaviour`。生命周期方法使用约定名称和固定签名，但不写 `virtual` 或 `override`。

```cpp
class MoveBehaviour final : public ScriptBehaviour
{
    OBJECT_TYPE_DECLARE(MoveBehaviour)

public:
    float32 speed = 2.0f;

protected:
    void OnStart();
    void OnUpdate(float32 deltaTime);
    void OnFixedUpdate(float32 fixedDeltaTime);
    void OnLateUpdate(float32 deltaTime);
    void OnDrawGUI();
    void OnEnd();
};
```

`ScriptBehaviour` 本身没有声明一组虚生命周期槽位。所以调用 `OnUpdate` 时不会先读取对象的 vptr，再从虚函数表中找槽位。

这里并不是说整个 `Component` 都没有虚函数。组件析构、类型系统和组件挂载通知仍可以使用必要的虚机制；只是高频脚本生命周期不走 vtable。

### MetaGen 做了什么

游戏 C++ 模块构建时，MetaGen 扫描继承自 `ScriptBehaviour` 的类型，并识别固定生命周期名称和签名。

对于实际声明了 `OnUpdate` 的类型，它会生成类似下面的静态 thunk：

```cpp
static void Script_MoveBehaviour_OnUpdate(
    ScriptBehaviour* script,
    float32 deltaTime)
{
    MoveBehaviour* instance =
        static_cast<MoveBehaviour*>(script);

    instance->MoveBehaviour::OnUpdate(deltaTime);
}
```

这里的 `MoveBehaviour::OnUpdate` 是限定名称调用。它明确指定调用哪个类型的方法，不会重新进入虚函数分派。

每个脚本类型会注册一张 `ScriptCallbackTable`：

```cpp
struct ScriptCallbackTable
{
    ScriptCallback start;
    ScriptUpdateCallback update;
    ScriptUpdateCallback fixedUpdate;
    ScriptUpdateCallback lateUpdate;
    ScriptCallback drawGUI;
    ScriptCallback end;
};
```

类型没有实现的阶段保存为 `nullptr`。如果派生脚本没有重新声明某个生命周期，运行时会沿父类链找到最近的父脚本 thunk，因此可以继承父类的行为。

### 游戏工程从哪里取得 MetaGen 和 C++ 头文件

游戏项目不保存一份自己的 OrbedenCore 源码，也不在创建项目时复制一套以后容易过期的头文件。`OrbedenCore.vcxproj` 的 x64 构建会统一发布 Native SDK：

```text
OrbedenEditor/Sdk/
├─ Native/Include/                         Core C++ 声明头文件
├─ Native/WindowsX64/Debug/OrbedenCore.lib Editor Debug 导入库
├─ Native/WindowsX64/Release/OrbedenCore.lib
└─ Tools/OrbedenMetaGen/OrbedenMetaGen.exe 元数据生成工具
```

因此，正式分发 Editor 时应把整个 `Sdk` 目录作为引擎安装内容一起分发，而不是给每个游戏项目单独复制 MetaGen。升级引擎以后只更新中央 SDK，所有指向这套引擎的游戏工程会使用同一版本的声明、库和工具。

新项目的 `Native/CMakeLists.txt` 包含完整构建步骤：

1. Editor 的 Build C++ 将 `ORBEDEN_ENGINE_ROOT` 和当前配置的 `OrbedenCore.lib` 传给 CMake。
2. CMake 从 `Sdk/Native/Include` 取得 `ScriptBehaviour`、`Ens`、内建组件和互操作 API 的声明。
3. C++ 源码发生变化时，先运行 SDK 中的 `OrbedenMetaGen`。
4. MetaGen 扫描项目 `Native` 目录，将 `Reflection.Generated.cpp` 写进 `Native/Build/Editor/Generated`。
5. 编译器同时编译游戏源码与生成代码，并链接 `OrbedenCore.lib`，输出游戏模块 DLL。

生成文件只存在于构建目录，不写回游戏源码目录。CMake 的 Visual Studio 工程同时承担普通 C++ 编译和语法检查；IDE 也可以使用相同的 include path 进行补全和诊断。

在引擎源码开发环境中，如果 SDK 尚未发布，模板仍可暂时回退到 `OrbedenCore/Src` 和 `Tools/OrbedenMetaGen`。正常流程仍是先构建一次 OrbedenCore，让声明、二进制与 MetaGen 版本保持一致。

### C++ 实例怎样进入调用表

原生脚本是真正的 `Component`，直接挂在 `Ens` 上并保存在 `.world` 中。

组件挂载时：

1. `World` 创建组件并调用组件的 `OnAttach`。
2. `ScriptBehaviour::OnAttach` 把实例注册到当前 `ScriptSystem`。
3. `ScriptSystem` 标记阶段表需要刷新。
4. 在下一个调度边界，系统根据 `enabled`、Ens 活动状态和该类型的回调表重新整理阶段列表。

稳定运行时，Update 表中的一项大致是：

```cpp
struct NativeScriptUpdateInvocation
{
    ScriptBehaviour* instance;
    ScriptUpdateCallback callback;
};
```

每帧调用循环只做连续数组遍历、一次空指针判断和一次函数指针间接调用。没有逐实例类型反射，也没有逐帧查找方法名称。

脚本抛出的 C++ 异常会在单个回调边界被捕获和记录，避免一个脚本直接中断整个阶段。

### C++ 脚本字段怎样持久化

C++ 脚本是原生组件，所以字段与组件一起写入 `.world`。

- 支持类型的 public 字段默认生成反射、序列化和 Inspector 访问代码。
- private/protected 字段需要加 `ORBEDEN_SERIALIZE_FIELD`。
- 显式要求序列化但类型不受支持时，MetaGen 会报错。
- 普通不支持字段不会成为可持久化字段，并产生警告。
- C++ 脚本允许在同一个 Ens 上添加多个同类型实例。

例如：

```cpp
class DamageArea final : public ScriptBehaviour
{
    OBJECT_TYPE_DECLARE(DamageArea)

public:
    float32 damage = 10.0f;

private:
    ORBEDEN_SERIALIZE_FIELD
    float32 cooldown = 0.5f;
};
```

MetaGen 会为可持久化字段生成文本 getter/setter，并把字段类型、名称、读写函数注册到原生 `Reflection`。

## C# 脚本的共同机制

### 游戏开发者怎样写 C# 脚本

C# 与 C++ 使用相同的“约定名称 + 固定签名”接口。`ScriptBehaviour` 基类不声明生命周期槽位：

```csharp
public sealed class MoveBehaviour : ScriptBehaviour
{
    public float speed = 2.0f;

    public MoveBehaviour(Ens ens) : base(ens) {}

    protected void OnStart() {}

    protected void OnUpdate(float deltaTime)
    {
        // 游戏逻辑
    }

    protected void OnEnd() {}
}
```

这些方法不是 `override`，也不能声明为 `virtual`。基类只提供 Ens、enabled 和运行时身份等公共能力，不提供空的生命周期虚函数。

### 程序集加载时只解析一次

`ScriptRuntime` 第一次遇到某个脚本类型时，会创建并缓存一个 `ScriptTypeFactory`：

1. 找到接收 `Ens` 参数的 public 构造函数。
2. 找到可选的序列化值应用方法。
3. 沿游戏脚本继承链查找最近声明、名称和签名精确匹配的 `OnStart`、`OnUpdate` 等非虚方法。
4. 为每个脚本实例创建绑定到该实例的闭合 delegate。

例如：

```csharp
Update = updateMethod.CreateDelegate<Action<float>>(script);
```

这个 delegate 已经同时记住了：

- 要调用哪个脚本对象。
- 要调用哪个具体方法。

反射发生在程序集/脚本实例加载阶段。普通帧循环只遍历 delegate 表，不使用 `MethodInfo.Invoke`、不按名称查找，也不经过生命周期虚函数分派。

构造函数调用和初始序列化值应用可以在加载阶段使用反射，因为它们不属于高频帧循环。

### C# 脚本实例从哪里来

C# 脚本挂载信息不写进原生 `.world` 组件区，而是保存在：

```text
{WorldPath}.scripts.json
```

一个挂载项包含：

```text
id        脚本挂载实例的唯一标识
stableId  所属 Ens 的稳定标识
type      C# 脚本完整类型名
enabled   是否启用
values    Inspector 序列化字段
```

运行时读取 sidecar，通过 `stableId` 找到 Ens，再创建脚本对象。`id` 用来区分同一个 Ens 上的多个脚本实例，包括多个同类型实例。

### C# 阶段调用表

每个脚本实例保存预先创建好的 delegate：

```text
ScriptInstance
├─ Script：脚本对象，保持 GC 可见
├─ Start：Action
├─ Update：Action<float>
├─ FixedUpdate：Action<float>
├─ LateUpdate：Action<float>
├─ DrawGUI：Action
└─ End：Action
```

只有实现了某个回调、当前可运行的实例才会进入对应阶段列表。

C# 引用必须保留在托管集合中，让 GC 能够追踪它们。因此 Orbeden 没有把 C# 对象引用直接塞进 C++ 调用表，也没有为每个 C# 实例建立一次 reverse P/Invoke。

## C# CLR 模式

CLR 模式主要用于 Windows Editor 和 PIE。

```text
Editor C++
  └─ 启动 CoreCLR
      ├─ 加载 OrbedenCore.CSharp.dll
      ├─ 加载游戏 CLR Assembly 的影子副本
      └─ 绑定 GameModule 的静态入口
```

游戏程序集使用可卸载的 `AssemblyLoadContext` 从文件流加载，目的是避免锁住正常构建输出，方便重新编译和重新进入 PIE。

Editor 把托管入口函数地址填入 `ScriptEntryPoints`。之后每个阶段的路径是：

```text
ScriptSystem C++
  → 一次 CLR 托管入口
    → ScriptRuntime.Update / FixedUpdate / ...
      → 遍历本阶段闭合 delegate 表
```

无论有 1 个还是 1000 个 C# Update 脚本，C++ 到 CLR 的阶段入口都是一次，不是 1000 次。

## C# NativeAOT 模式

NativeAOT 模式用于最终 Player。

项目中固定的 `OrbedenAotExports` 提供下列原生 ABI 入口：

```text
OrbedenGame_Initialize
OrbedenGame_Shutdown
OrbedenGame_Update
OrbedenGame_FixedUpdate
OrbedenGame_LateUpdate
OrbedenGame_EnsWorldActiveChanged
OrbedenGame_EnsDestroyed
OrbedenGame_DrawGui
```

这些入口只是一层很薄的转发：

```text
NativeAOT 导出
  → GameScriptRuntime
    → ScriptRuntime
      → 阶段 delegate 表
```

Player 的 C++ `ScriptSystem` 在链接时直接获得这些入口，所以 AOT 模式不启动 CoreCLR、不动态加载游戏 CLR DLL，也不在帧循环按名字查找托管方法。

NativeAOT 仍然包含托管 GC 和类型系统。游戏脚本程序集及生命周期方法会作为 NativeAOT/Trimmer 的根保留下来，运行时初始化时仍由同一个 `ScriptRuntime` 创建脚本实例和闭合 delegate。

Windows 当前使用 NativeAOT DLL 加导入库，以隔离 NativeAOT `/MT` 和现有原生依赖 `/MD` 的 CRT 差异；其他静态支持目标可以直接链接 NativeAOT 静态库。两种链接形式不改变每阶段一次跨域入口的调用机制。

没有使用 C# Source Generator。AOT 项目的固定导出文件只负责声明 ABI 入口，不为每个游戏脚本生成调用代码。

## 生命周期规则

### 什么叫“可运行”

脚本同时满足下面三个条件时才可运行：

```text
脚本仍然存在
&& 脚本 enabled == true
&& 所属 Ens 的 worldActive == true
```

`worldActive` 是 Ens 自己的 active 与父级层次状态合成后的实际活动状态。

### OnStart

脚本第一次变为可运行，并且即将参加某个脚本阶段前，调用一次 `OnStart`。

- 创建时已经启用且 Ens 活动：初始化阶段或首次参与阶段前调用。
- 创建时被禁用：暂时不调用。
- Ens 处于非活动层级：暂时不调用。
- 后来启用或变为活动：在参与下一个阶段前调用。

`OnStart` 一生只调用一次。脚本启动过以后，禁用再启用不会重复 Start。

### enabled 和 Ens 活动状态

脚本被禁用或 Ens 变为非活动时：

- 从活动阶段表中移除。
- 不再接收 Update/FixedUpdate/LateUpdate/DrawGUI。
- 不调用 `OnEnd`。
- 已经发生过的 `OnStart` 状态会保留。

重新启用或重新活动时，脚本重新进入阶段表；如果过去已经 Start，则直接继续运行。

原生 World 会主动通知 Ens 的 `worldActive` 变化和 Ens 销毁事件。脚本系统不需要在每一帧为每个实例调用 `Ens.IsValid` 做轮询。

### OnEnd

只有已经执行过 `OnStart` 的脚本才会执行 `OnEnd`。

以下情况会 End：

- 从 Ens 移除脚本组件。
- 销毁所属 Ens。
- 清空 World。
- 停止 PIE 或关闭脚本运行时。
- 卸载游戏 C++ 模块前清理 World 中的模块组件。

禁用脚本不会 End。一个实例最多 End 一次。

当前 C++ 关闭流程按原生挂载顺序结束；C# 运行时关闭时从挂载列表末尾向前结束。

### 回调期间增删组件或销毁 Ens

脚本可能在自己的回调里删除另一个组件，甚至销毁自己的 Ens。如果立即修改正在遍历的数组，容易出现漏调用、越界或悬空指针。

因此修改采用调度边界延迟处理：

```text
回调中提出删除/销毁
  → 记录到延迟队列或销毁标记
  → 当前域的阶段遍历安全结束
  → 进入下一个域或脚本阶段前应用
  → 重建受影响的调用表
```

由于 C++ 域在 C# 域之前，C++ 回调提出的 World 删除会在随后的 C# 域入口前生效。C# 域回调提出的修改通常在下一个脚本阶段入口前生效。

`enabled` 修改不会立刻重建正在遍历的列表，而是将列表标记为 dirty，在下一个对应域阶段开始前刷新。

## InspectorPanel 怎样显示这些组件

Inspector 实际处理三条不同的数据路径：

```text
1. 有 C# Binding 的引擎原生组件
   例：Transform、Renderer、RigidBody、Collider

2. 没有专用 Binding 绘制器的原生组件
   例：Camera、DirectionalLight、游戏 C++ ScriptBehaviour

3. 游戏 C# ScriptBehaviour
```

### 路径一：已有 C# Binding 的原生组件

Transform、Renderer、RigidBody、Collider 等组件的真实对象仍在 C++ World 中，但 Core C# 已经提供包装类型和较成熟的专用 Inspector UI。

Inspector 通过这些绑定读取和设置原生组件，然后使用专用控件绘制。`NativeTypesCoveredByDedicatedDrawers` 记录已经被完整覆盖的原生类型名。

通用原生组件枚举仍然能够看到这些对象，但绘制时会跳过对应的通用卡片，因此用户只会看到一个组件，不会同时出现“专用卡片”和“通用 C++ 卡片”。

### 路径二：通用 C++ 组件反射

游戏 C++ 脚本和没有专用绘制器的引擎组件走原生反射桥接：

```text
InspectorPanel.cs
  → EditorNativeComponents.cs
    → Editor Native Component API
      → ManagedEditorBridge.cpp
        → World / Object / Reflection
```

具体步骤如下：

1. C++ 根据选中的 `EnsId` 枚举 `Ens::GetComponents()`，返回每个实例的 `objectId` 和原生类型名。
2. Inspector 用 `objectId` 标识组件实例，因此同类型多个 C++ 脚本可以分别显示和删除。
3. C++ `Reflection::CollectFields` 从基类到派生类收集字段元数据；派生类同名字段覆盖父类字段。
4. Inspector 读取字段名、`FieldKind` 和文本值。
5. C# 根据 `FieldKind` 选择 Checkbox、数字框、Vector3、颜色或文本等控件。
6. 用户修改后，值以文本 ABI 传回 C++，由字段 setter 解析并写入真实组件对象。

这条 C++/C# Editor API 传递的是简单数字、对象 ID 和 UTF-8 文本，不会把 C++ 对象布局暴露给 C#。

通用卡片标题会标记 `[C++]`。Camera、DirectionalLight 和游戏 C++ 脚本默认使用这条路径。

添加组件菜单会从原生 Type 注册表枚举所有可创建的 `Component` 类型。对于 `ScriptBehaviour` 派生类，添加操作使用多实例接口；普通组件默认仍保持一个同类型实例。

### 路径三：C# 脚本反射

Inspector 使用自己的可卸载 `AssemblyLoadContext` 加载游戏 CLR Assembly。这个反射程序集服务于 Editor UI，不是每帧执行脚本所用的调用路径。

加载后，Inspector 找出所有满足下面条件的类型：

```text
不是 abstract
&& 继承 Orbeden.ScriptBehaviour
```

然后把这些类型与 `.world.scripts.json` 中当前 Ens 的挂载项对应起来。

C# 字段显示规则是：

- public 实例字段默认显示和序列化。
- 带 `[SerializeField]` 的非 public 实例字段显示和序列化。
- 带 `[HideInInspector]` 的字段不显示。
- static 字段不显示。
- public、非索引属性可以在运行态字段区显示；有 setter 时可以写回。

编辑非运行态脚本时，Inspector 修改的是 sidecar 中的 `values`。进入 PIE 后，`ScriptRuntime` 创建脚本实例并应用这些值。

运行态 Inspector 还会通过 `ScriptRuntimeRegistry` 找到当前 C# 脚本对象，用普通 C# 反射显示实时字段和属性。sidecar 挂载和运行态实例通过 `MountId` 对应，而不是只靠类型名，因此多个同类型脚本不会混在一起。

### Add Component 菜单

添加菜单合并两类来源：

- C++ Type 注册表中的可创建原生组件。
- 游戏程序集中的 C# `ScriptBehaviour` 类型，以及已有的 Core C# 原生绑定类型。

菜单项明确标记 `[C++]` 或 `[C#]`。

选择 C++ 类型时，直接在 World 中创建真实 Component。选择 C# 游戏脚本时，在 sidecar 中增加挂载记录；实际 C# 对象在运行脚本系统初始化时创建。

## 序列化位置对照

| 内容 | 保存位置 | Inspector 主要读取方式 |
| --- | --- | --- |
| C++ 游戏脚本组件 | `.world` | 原生 Reflection + Editor Native Component API |
| 引擎原生组件 | `.world` | 专用 C# Binding 或通用原生 Reflection |
| C# 游戏脚本挂载和字段 | `.world.scripts.json` | Editor CLR Assembly 反射 + sidecar |

这也是为什么 C++ 脚本类型必须在加载 `.world` 前完成模块和 Type 注册，而 C# 脚本可以先读取 sidecar，等游戏程序集可用后再获得完整字段编辑能力。

## C++ 与 C# 怎样互相访问组件

生命周期调度和主动互操作是两条独立路径：

- `Update` 等生命周期仍然走前文的批量阶段表，不会退化成逐组件跨域调用。
- 游戏代码明确调用另一个组件时，才会走 `ComponentProxy`。

`ComponentProxy` 可以理解成一个“可失效的组件遥控器”。它不把 C# 对象指针交给 C++，也不把 C++ 对象指针直接交给 C#，而是保存一个：

```text
组件域 + 实例槽位 + generation
```

字段和方法第一次按名字解析后会缓存成员句柄。按名称调用不会再扫描整张反射表，但仍需要构造名称/签名缓存键；重复或高频调用可以直接保存预解析句柄，完全跳过这一段。

C++ 示例：

```cpp
using namespace ScriptInterop;

ComponentProxy native = FindNativeComponent(
    ensId, "Game.MoveBehaviour", 0);
ComponentProxy managed = FindManagedComponent(
    ensId, "Game.HealthBehaviour", 0);

native.SetField("speed", Reflection::Value(4.0f));
managed.SetField("health", Reflection::Value(int32(80)));

constexpr Reflection::ValueKind damageSignature[]{
    Reflection::ValueKind::Int32
};
MemberHandle damageMethod;
managed.ResolveMethod("Damage", damageSignature, damageMethod);

Reflection::Value result;
Reflection::Value arguments[] = { Reflection::Value(int32(10)) };
managed.Invoke(damageMethod, arguments, result);
```

C# 示例：

```csharp
ComponentProxy? native = Ens.GetNativeComponent("Game.MoveBehaviour");
ComponentProxy? managed = Ens.GetManagedComponent<HealthBehaviour>();

native?.SetField("speed", InteropValue.From(4.0f));
managed?.SetField("health", InteropValue.From(80));

InteropStatus status = native?.TryResolveMethod(
    "Teleport", out ComponentMethod teleportMethod,
    InteropValueKind.Vector3) ?? InteropStatus.NotFound;
if (status == InteropStatus.Ok)
{
    status = teleportMethod.Invoke(out InteropValue result,
        InteropValue.From(new vector3(1, 2, 3)));
}
```

它支持四种组合：

| 调用方向 | 实现方式 |
| --- | --- |
| C++ → C++ | C++ Reflection getter/setter 或 MetaGen 方法入口 |
| C++ → C# | 托管运行时注册的函数表，进入 CLR/NativeAOT 后访问缓存元数据 |
| C# → C++ | `OrbedenNativeApi` 中的原生互操作函数表 |
| C# → C# | 同一套托管实例表和缓存元数据，不跨原生边界 |

字段和方法匹配有意保持严格：名字区分大小写；重载按“方法名 + 参数 `InteropValueKind` 列表”精确匹配；不做隐式整数、浮点转换。六个生命周期名称属于运行时保留名称，即使写成 public 也不会进入通用 `Invoke` 表。

### 为什么不直接返回跨语言函数指针

真正的裸函数指针不能安全统一 C++、CLR 和 NativeAOT：C# 实例可能被 GC 移动，普通实例方法没有可长期暴露的稳定 C ABI；程序集卸载和 C++ 游戏 DLL 热重载也会让旧地址直接变成悬空地址。NativeAOT 可以导出少量静态 C 入口，但不能把任意游戏脚本实例方法都当作普通 C++ 函数地址使用。

`MemberHandle`（C++）和 `ComponentMethod`（C#）是这里的安全“函数引用”：

- 名称和精确参数签名只解析一次；
- 重复调用直接携带组件句柄与方法槽位进入调度；
- 每次调用仍校验 generation，模块或程序集重载后返回 `StaleHandle`，不会跳进旧地址。

这会消除重复字符串查找，但不会消除跨域 ABI、参数编码，以及 C# 通用动态方法的 `MethodInfo.Invoke`/装箱成本。对每帧大量调用的固定 API，仍应写明确的强类型 C# Binding 或 AOT 静态入口；生命周期继续使用专门的批量阶段表。

当前可跨域传递的值包括 `bool`、`int32`、`uint32`、`uint64`、`float32`、UTF-8 字符串、`StringId`、`vector3`、`color4`、`quaternion`、`EnsId` 和以 ObjectId 表示的原生对象引用。数组、列表、自定义结构体、泛型方法、委托以及 `ref/out` 参数不属于这条通用通道。
### TypeId 在 Core、游戏 C++ 和 C# 之间是什么关系

原生 `TypeId` 是当前进程里 `Object/Type` 注册表分配的运行时编号。Core 内建类型与游戏 C++ 模块注册的类型进入同一张注册表，因此在同一次 Editor 或 Player 进程中可以直接用同一个 `TypeId` 查找组件。

需要注意：

- `TypeId` 只保证本次进程、本次类型注册周期内有效。
- 游戏模块卸载和重新注册后，不能把旧 `TypeId` 当作持久身份继续使用。
- `.world`、Undo 快照和热重载恢复都保存完整原生类型名，而不是保存 `TypeId`。
- 通过字符串找到类型后，可以在当前进程内缓存 `TypeId` 或成员句柄以减少查找。

普通 C# 脚本没有原生 `TypeId`。托管域使用 `System.Type`、完整类型名和 Assembly 元数据识别类型，实例身份由托管 `{slot, generation}` 句柄表示。C++ 查找 C# 组件时传完整类型名，托管函数表负责定位对应实例；不会伪造一个 Native `TypeId` 塞进原生注册表。

### occurrence 为什么存在

一个 Ens 可以挂载多个相同类型的 C++ 或 C# 脚本。`occurrence=0` 表示挂载顺序中的第一个同类型实例，`occurrence=1` 表示第二个，以此类推。它是一次查找条件，不是永久实例 ID；组件顺序改变后应该重新查找代理。

### 调用失败怎样处理

代理操作不抛出跨域异常，而是返回统一状态：

| 状态 | 含义 |
| --- | --- |
| `Ok` | 操作成功 |
| `NotFound` | 组件、字段或方法不存在 |
| `StaleHandle` | World、组件、程序集或模块变化后句柄已经过期 |
| `InvalidArgument` | 名称、参数数量或 ABI 输入不合法 |
| `TypeMismatch` | 字段值或方法参数的 `InteropValueKind` 不匹配 |
| `AmbiguousMethod` | 方法名和参数种类仍匹配到多个重载 |
| `UnsupportedType` | 数组、自定义结构等当前通道不支持的类型 |
| `WrongThread` | 在非主线程调用同步互操作 |
| `ReentrancyLimit` | 跨域嵌套调用超过 32 层 |
| `InvocationFailed` | getter、setter 或目标方法内部抛出异常 |

推荐先判断代理是否为空，再检查每次重要操作的返回状态。不要只在第一次获取代理时检查 `IsValid`，因为目标可能在后续脚本阶段被删除。

### Inspector 使用哪套反射

Inspector 的三类组件最终都转成属性目标，但取元数据的方式不同：

- 没有 C# Binding 的 C++ 组件使用 MetaGen 生成的 `FieldInfo` 和直接 getter/setter。
- 有完整 C# Binding 的引擎原生组件继续使用现有业务属性，通用原生卡片会被去重，不显示两份。
- C# 脚本在游戏 Assembly 加载时建立一次字段元数据缓存；public 字段和 `[SerializeField]` 字段可序列化，`[HideInInspector]` 字段不显示但仍可被序列化和互操作。

因此 PropertyDocument 是编辑事务层，不是新的序列化格式，也不是另一套运行时反射系统。


### 为什么句柄会失效

组件被删除、World 被重新加载、C# Assembly 被卸载或 C++ 游戏模块被热重载以后，旧代理不能继续指向“刚好复用了同一个内存地址”的新对象。为此组件句柄和成员句柄都带 generation。旧句柄会返回 `StaleHandle`，调用者应重新按类型查找。

互操作只允许主线程同步调用，允许 C++ → C# → C++ 这样的嵌套，最大重入深度是 32。异常会在域边界内被捕获并转换成 `InvocationFailed`，不会穿过 C ABI。

### 什么时候不该使用 ComponentProxy

按名称的 `ComponentProxy.Invoke` 适合低频和工具代码；重复调用应至少先取得 `MemberHandle`/`ComponentMethod`。托管动态方法最终仍使用缓存的 `MethodInfo.Invoke`，参数也可能产生装箱。每帧对大量组件进行高频调用时，仍应使用：

- 生命周期函数表；
- 明确编写的 C# Binding；
- 明确的 NativeAOT 静态、blittable 批量入口；
- 或把高性能逻辑整体放在 C++ 组件内部。

## PropertyDocument：Inspector 的统一编辑事务

Inspector 不直接修改 `.world` 文件字节。它先把活对象或 sidecar 包装为属性目标，再通过 `PropertyDocument` 编辑：

```text
C++ 活组件 ──────┐
C# sidecar mount ├─→ IPropertyTarget[]
PIE C# 活对象 ───┘        ↓
                  PropertyDocument.Update()
                           ↓
                    PropertyValue 暂存值
                           ↓
                  ApplyChanges("Undo 名称")
```

一次 Apply 的过程是：

1. 先确认每个目标仍有效，并验证字段类型。
2. 读取所有旧值作为回滚和 Undo 快照。
3. 把新值写到全部目标。
4. 任意目标失败时，把已经写入的目标恢复为旧值，不生成 Undo。
5. 全部成功后才记录 Undo，并标记相应数据 Dirty。

多选时，Inspector 只显示所有目标共同拥有、并且值类型相同的组件和字段。原生组件按“精确类型名 + 同类型 occurrence”匹配；C# 脚本按“完整类型名 + 同类型挂载 occurrence”匹配。值不同时显示 mixed，用户输入新值会一次写入全部目标。

`PropertyDocument` 统一覆盖：

- 通用 C++ 反射组件；
- C# sidecar 脚本；
- PIE 中的 C# 活脚本；
- 已有 C# Binding 的 Transform、Renderer、RigidBody、Collider 等原生组件；
- Ens 名称和 `ScriptBehaviour.enabled`。

添加、删除组件和属性修改都进入同一个 Editor 历史；历史最多 256 条。连续数值修改在短时间内按目标和字段合并，菜单及快捷键支持 Ctrl+Z、Ctrl+Y 和 Ctrl+Shift+Z；文本输入框正在编辑时不会抢走文本控件自己的 Undo。

### Dirty、保存和 PIE

Edit 模式下：

- 修改原生组件或 Ens 名称会设置 `WorldDirty`。
- 修改 C# mount 会设置 `SidecarDirty`。
- Apply 不立即写磁盘。
- Save/Ctrl+S 会统一保存 `.world` 和 `.world.scripts.json`。
- sidecar 先写同目录临时文件，再用原子替换提交；失败时 Dirty 状态保留。

PIE 模式下，PropertyDocument 指向运行副本。修改会立刻影响 C++/C# 活组件，但不会修改 sidecar，也不会设置项目 Dirty。停止 PIE 后运行 World 被销毁并从磁盘重新加载，PIE 历史随之清空，所以运行期修改不会偷偷写回工程文件。

当前 PropertyDocument 只处理顶层字段，不递归展开数组、列表或任意嵌套结构。这是编辑模型的范围限制，不影响普通脚本字段和组件生命周期调度。

## 性能上避免了什么

稳定帧循环有意避免下面这些操作：

- 不按方法名搜索 C++ 或 C# 生命周期函数。
- 不使用 `MethodInfo.Invoke` 调用每个 C# 生命周期函数。
- 不为每个 C# 脚本做一次 C++/C# 边界跳转。
- 不为 C++ 生命周期读取对象虚函数表槽位。
- 不逐帧轮询每个脚本所属 Ens 是否仍然有效。
- 调用表没有变化时不重建阶段列表。

真正的每帧核心工作是：

```text
C++：遍历连续列表 → 函数指针调用
C#：一次域入口 → 遍历闭合 delegate 列表
```

## 当前边界和注意事项

- 当前固定为 C++ 域先于 C# 域，没有跨语言 execution order。
- C++ 和 C# 生命周期函数都必须使用受支持的固定名称和签名，并且不能声明为 `virtual`；C# 不写 `override`。
- C# 脚本需要 public `ScriptType(Ens ens)` 构造函数。
- C# 脚本需要所属 Ens 有稳定 `stableId`，否则无法持久化到 sidecar。
- C++ 显式序列化字段必须使用受支持的反射类型。
- 原生通用 Inspector 依赖字段 getter/setter；不受支持或没有 setter 的字段不能有效写回。
- Inspector 对游戏 C# Assembly 的反射不在 Player 帧循环中，不应与运行时生命周期 delegate 调度混为一谈。
- Editor 热重载 C++ 模块时会先停止 PIE、保存并清空 World、注销旧模块类型，再加载影子 DLL 和恢复 World；新模块缺少当前 World 所需组件类型时会尝试回滚旧模块。

- PropertyDocument 首版只支持顶层属性，不展开数组、List 或任意嵌套对象。
- Native 组件删除后的 Undo 会恢复组件类型和持久化字段；当前不保证精确恢复跨类型组件的原全局挂载序号。
- 当前没有在切换、重载或关闭项目时提供完整的 Save/Discard/Cancel 模态确认流程；应先显式保存重要修改。
- 通用 Object 字段使用 ObjectId 传递；Inspector 写回时会做目标字段类型校验，但选择器尚未针对每个具体派生类型做完整过滤。
- OrbedenCore 与 OrbedenEditor 的 Debug x64 工程构建已经通过；完整 CLR/NativeAOT 游戏工程运行和热重载行为仍应继续做集成验证。

## 主要源码入口

- 统一域调度：`OrbedenCore/Src/Scripting/ScriptSystem.cpp`
- C++ 脚本基类和回调表：`OrbedenCore/Src/Scripting/ScriptBehaviour.h`
- C++ 回调注册和继承解析：`OrbedenCore/Src/Scripting/ScriptBehaviour.cpp`
- MetaGen thunk 和字段元数据生成：`Tools/OrbedenMetaGen/Program.cs`
- C# 脚本实例和 delegate 表：`OrbedenCore/Managed/OrbedenCore.CSharp/ScriptRuntime.cs`
- C# 脚本开发接口：`OrbedenCore/Managed/OrbedenCore.CSharp/ScriptBehaviour.cs`
- AOT 公共转发层：`OrbedenCore/Managed/OrbedenCore.CSharp/GameScriptRuntime.cs`
- Editor CLR/PIE 入口绑定：`OrbedenEditor/Src/Editor/EditorPlayMode.cpp`
- Inspector 主逻辑：`OrbedenEditor/Managed/Orbeden.Editor/Panels/InspectorPanel.cs`
- 原生组件 C# 桥：`OrbedenEditor/Managed/Orbeden.Editor/EditorNativeComponents.cs`
- 原生组件 Editor API：`OrbedenEditor/Src/Editor/ManagedEditorBridge.cpp`
- C++ `ComponentProxy` 与互操作 ABI：`OrbedenCore/Src/Scripting/ScriptInterop.h`
- C++ 互操作调度：`OrbedenCore/Src/Scripting/ScriptInterop.cpp`
- C# `ComponentProxy`：`OrbedenCore/Managed/OrbedenCore.CSharp/ComponentProxy.cs`
- C# 类型缓存与互操作函数表：`OrbedenCore/Managed/OrbedenCore.CSharp/ManagedScriptInterop.cs`
- Inspector 编辑事务：`OrbedenEditor/Managed/Orbeden.Editor/PropertyDocument.cs`
