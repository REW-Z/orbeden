# 项目规范

Orbeden 是一个 C++ 游戏引擎项目。核心原生代码在 `OrbedenCore/`，托管层在 `OrbedenCore/Managed/OrbedenCore.CSharp/`，编辑器在 `OrbedenEditor/`，玩家在 `OrbedenGame/`。本规范约束引擎代码的文件组织、命名、include、代码风格与代码生成，新代码按本规范编写，同时作为既有代码整理的依据。

---

# 文件结构

## Src 目录分层

`OrbedenCore/Src/` 是原生代码根，也是 include 的根。目录按角色划分，不允许随意新增顶层目录。

### 所有继承自 Object 的类型定义放到 Runtime/Object 下

`Runtime/Object/` 是**唯一**允许定义 Object 派生类的目录。继承自 `Orbeden::Object` 的类型，无论直接继承还是经 `Component` 间接继承，头文件与源文件都放在这里：

```
Runtime/Object/
├── Object.h/.cpp              # 根类型
├── Component.h/.cpp           # 组件基类
├── Transform.h/.cpp           # 组件：不需要 Component 后缀
├── Camera.h/.cpp              # 组件
├── StaticMeshRenderer.h/.cpp  # 组件
├── Collider.h/.cpp            # 组件：抽象基类 + 五个具体碰撞体同文件
├── RigidBody.h/.cpp           # 组件
├── Script.h/.cpp              # 组件：脚本基类
├── Material.h/.cpp            # 资源对象
├── Mesh.h/.cpp
├── Shader.h/.cpp
├── Skybox.h/.cpp
└── Texture2D.h/.cpp
```

反向约束同样成立：`Runtime/Object/` 里不放非 Object 派生类（`Object.h` 中的 `EnsId`、`Type`、`Ref<T>` 等基础定义属于 Object 自身的基础设施，是唯一例外）。

`Component` 基类位于 `Runtime/Object/Component.h`，句柄 `EnsId` 位于 `Runtime/EnsId.h`，两者不混在同一文件。

### 子系统拥有根目录层级的文件夹

继承 `IEngineSystem` 的子系统（以及以 `Manager`、`System` 结尾的引擎系统类）拥有 `Src/` 下的根目录层级文件夹，目录内是该子系统的全部实现：

| 子系统 | 目录 | 说明 |
|---|---|---|
| `FileSystem` | `Src/FileSystem/` | |
| `InputManager` | `Src/InputManager/` | |
| `PhysicsSystem` | `Src/Physics/` | 含 `PhysicsTypes.h`、`PhysicsReflection.*` |
| `Profiler` | `Src/Profiler/` | |
| `RenderSystem` | `Src/Rendering/` | 含 `GpuResourceManager`、`ForwardPipeline`、`Backend/` |
| `ResourceManager` | `Src/ResourceManager/` | |
| `ScriptSystem` | `Src/Scripting/` | 含 `ScriptInterop`、`NativeGameModule.h` |

约定：

- 目录名不必与子系统类名逐字相同，但必须与该模块的职责一致（如 `Rendering/` 就是 `RenderSystem` 的模块目录）。
- 模块内部的管理类（`GpuResourceManager`、`MemoryManager`）不算独立子系统，留在所属模块目录（`Rendering/`、`Memory/`），不单独建目录。
- 子系统不得散落在 `Runtime/` 等其他目录，不得把子系统实现文件平铺在 `Src/` 根下。

## 文件命名

- 一个文件承载一个主类；文件名与类名完全一致：`Transform.h` / `Transform.cpp`。
- 头文件 `.h` 与源文件 `.cpp` 成对出现，基名相同。
- 紧密相关的小型类型族可以合并到一个文件（如 `Collider.h` 内的 `Collider`、`BoxCollider`、`SphereCollider` 等），此时文件名取基类名。
- 所有头文件使用 `#pragma once`。

---

# 命名规范

## 类型

- 类型名使用 PascalCase，不加类别前缀或后缀。
- **Object 派生类禁止使用 `Component` 后缀**：`Transform` 而不是 `TransformComponent`，`RigidBody` 而不是 `RigidBodyComponent`。后缀不携带信息，类型名直接表达它在引擎中的角色。
- 抽象基类用业务名（`Collider`），具体类型用限定词（`BoxCollider`）。

## 成员

| 种类 | 规则 | 示例 |
|---|---|---|
| 成员变量 | camelCase，无前缀 | `localPosition`、`collisionMask` |
| 局部变量 | camelCase | `transformStorage` |
| 方法 / 函数 | PascalCase | `GetEns()`、`OnAttach()` |
| 常量 | PascalCase 或全大写（`static constexpr`） | `InvalidId` |
| 宏 | 全大写下划线 | `OBJECT_TYPE_DECLARE`、`ORBEDEN_BIND_IGNORE` |

不区分 private 与 public 的命名（不用 `m_`、`_` 前缀）。

## 数值类型

统一使用 `Defines/types.h` 的别名，不用裸 `int`、`unsigned`、`float`：

- `int8/int16/int32/int64`、`uint8/uint16/uint32/uint64`
- `float32`、`float64`
- `List<T>`（`std::vector<T>`）
- 引擎数学与句柄类型：`vector3`、`quaternion`、`matrix4x4`、`color`、`EnsId`
- `std::string` 直接使用，不另造别名

---

# Object 派生类型规范

Object 类型分三类，创建方式不同：

| 类别 | 示例 | 创建方式 |
|---|---|---|
| 组件（继承 `Component`） | `Transform`、`Camera`、`RigidBody` | `Ens.AddComponent<T>()`（C#） |
| 资源对象 | `Material`、`Mesh`、`Shader`、`Skybox`、`Texture2D` | `Resources.Load<T>(key)`（C#）/ `ResourceManager::Load<T>(key)`（C++） |
| 其他运行时对象 | `Object` 直接派生且非资源 | `Object.CreateInstance<T>()`（C#）/ `Object::CreateInstance<T>()`（C++） |

抽象类型只能查询（`GetComponent<T>()`），不能直接创建。

## 类型宏

每个 Object 派生类必须使用类型宏声明，并在对应 `.cpp` 中实现：

```cpp
//头文件
class Transform : public Component
{
    OBJECT_TYPE_DECLARE(Transform)          // 叶子类型
    // OBJECT_TYPE_DECLARE_BASE(X)          // 有派生类的基类
    // OBJECT_TYPE_DECLARE_ABSTRACT(X)      // 不可直接创建的抽象类型
    ...
};

//源文件
OBJECT_TYPE_IMPLEMENT(Transform, Component)
// OBJECT_TYPE_IMPLEMENT_ROOT(Object)
// OBJECT_TYPE_IMPLEMENT_ABSTRACT(Collider, Component)
```

**运行时类型名就是类名字符串**（宏内的 `#CLASS`），它参与场景序列化、托管包装注册与类型查找。改名会直接改变运行时类型名：

- 同步更新 `.world` 场景文件里的 `<Component type="...">`；
- 同步更新按字符串分派的代码（如 `OrbedenNativeApi.cpp` 的绑定种类判断、编辑器 `InspectorPanel.cs` 的属性表）；
- 同步更新 MetaGen 中按类名特判的逻辑；
- 不保留旧名兼容层。

## 组件约定

- 组件通过 `Component` 基类的 `GetEns()` / `GetEnsId()` 访问所属实体，不自行缓存 `Ens*`。
- 生命周期钩子按需重写：`OnAttach()`、`OnDetach()`、`OnWorldActiveChanged(bool)`，不要在构造函数里做注册。
- 一个实体只允许存在一个实例的组件（如 `Transform`）标注 `ORBEDEN_COMPONENT_UNIQUE`。
- 字段默认对外公开并被反射、序列化、Binding 收集；不需要暴露的成员用 `ORBEDEN_BIND_IGNORE`。

## 元数据注解

MetaGen 通过扫描头文件宏与注解生成 C# 绑定与反射，不解析实现：

| 注解 | 作用 |
|---|---|
| `ORBEDEN_BIND_IGNORE` | 不向托管 API 公开；不影响反射与序列化 |
| `ORBEDEN_BIND_ACCESSORS(GETTER, SETTER)` | 字段读写必须经显式 getter/setter（`None` 表示只读/只写） |
| `ORBEDEN_BIND_BUFFER(DATA, COUNT)` | 把指针+长度参数映射为一个托管 Span |
| `ORBEDEN_BIND_CHANGED(CALLBACK)` | 字段写入后执行通知，反射与 Binding 共用 |
| `ORBEDEN_COMPONENT_UNIQUE` | 组件单实例约束 |
| `ORBEDEN_SERIALIZE_FIELD` | 标记非 public 字段参与序列化 |

public 受支持字段自动进入元数据；不支持的公开签名会由 MetaGen 报错，要求显式排除或调整接口，不允许静默丢弃。

---

# 子系统规范

- 子系统继承 `IEngineSystem` 并实现所需生命周期：`OnInitialize(Application&)`、`FixedUpdate`、`Update`、`LateUpdate`、`OnWindowResize`、`OnShutdown`。
- 子系统之间不互相持有单例；依赖在 `OnInitialize(Application&)` 中经 `Application` 获取。
- 注册与注销成对出现：`OnAttach` 注册、`OnDetach` 注销，`OnShutdown` 释放全部持有资源。
- 子系统对外提供的静态入口（如 `ResourceManager::Load<T>()`）集中在子系统头文件，不散落到调用方。

---

# include 规范

- 一律使用 **Src 根相对路径**、正斜杠、双引号：

```cpp
#include "Runtime/Object/Component.h"
#include "Physics/PhysicsTypes.h"
#include "Rendering/RenderTypes.h"
```

- 禁止相对路径跳转（`../`），禁止裸文件名（`#include "Transform.h"`）。
- 头文件必须显式包含自己用到的定义，不依赖其他头文件的传递包含。例如类成员是 `Ref<Mesh>` 就包含 `Runtime/Object/Mesh.h`，不要指望别的头替你带进来。
- 标准库使用尖括号（`<string>`、`<vector>`、`<type_traits>`）；第三方按其 include 根沿用现状（imgui 用 `#include <imgui.h>`，cgltf 用 `#include "cgltf.h"`）。
- 顺序：自身头文件（源文件首行）→ 项目头文件 → 标准库 → 第三方。
- 文件移动或改名后，必须更新全仓库引用它的 include、工程文件清单（`.vcxproj` / `.vcxproj.filters`）以及 `CMakeLists.txt` 的源文件列表。

---

# 代码风格

- 缩进 4 空格，不使用 Tab。
- 大括号另起一行（Allman 风格），函数、类、控制流一致：

```cpp
bool Component::IsRenderSceneEligible() const
{
    if (enabled == false)
    {
        return false;
    }

    return true;
}
```

- 类内成员顺序：类型宏 → `private` 数据与私有方法 → `public` 对外字段 → `public` 方法。
- 空指针一律 `nullptr`；类型转换使用显式 `static_cast`。
- 注释使用中文 `//`，说明"做什么、为什么"，不复述代码：

```cpp
//设置启用状态并同步渲染场景注册
void SetEnabled(bool value);
```

- 每个类和每个公开方法都要有简短注释；实现中的关键分支、特殊约束同样需要注释。
- 未使用的参数在函数体内显式 `(void)param;` 标记，避免编译告警。

---

# 托管 C# 侧规范

- 手写代码与生成代码分离：`OrbedenCore/Managed/OrbedenCore.CSharp/Generated/` 下的文件由 MetaGen 生成，**禁止手工修改**。
- 每个 C++ Object 类型在托管侧对应一个 `partial class`，生成部分提供绑定成员，手写部分补充运行时逻辑。
- 命名空间：核心类型在 `Orbeden`，游戏模块在 `<项目名>.Native`；托管类名、成员名跟随 C++，改名时同步修改仓库调用方，不保留旧名兼容层。
- 常用入口：
  - 组件：`ens.AddComponent<T>()`、`ens.GetComponent<T>()`、`ens.TryGetComponent<T>(out var c)`
  - 资源：`Resources.Load<T>(key)`、`Resources.UnloadUnusedObjects()`
  - 非组件对象：`Object.CreateInstance<T>()`（要求具体类型且非组件）
- 托管脚本继承 `Orbeden.Script`，生命周期由 `ScriptSystem` 调度，不自行注册回调。

---

# 构建与代码生成

- 新增、移动、改名文件后同步更新 `OrbedenCore/OrbedenCore.vcxproj` 与 `OrbedenCore.vcxproj.filters`（显式文件列表）；同时维护 `CMakeLists.txt` 的 `ORBEDEN_CORE_SOURCES`。
- 新增 Object 派生类无需手写绑定：构建 `OrbedenCore` 时会先运行 `OrbedenMetaGen` 重新生成 `Src/Runtime/Generated/`（反射、C++/C# Binding、类型清单），再发布 SDK 头文件到 `OrbedenEditor/Sdk/`。
- 生成物提交到仓库，但只通过构建刷新；`Bindings.Manifest.json` 中的 schema hash 随签名变化属正常。
- 改名或移动文件后，旧文件不会从 `OrbedenEditor/Sdk/Native/Include/` 自动消失（发布过程只复制不删除），需要手工清理，避免模板工程误用旧头文件。
- 模板工程（`OrbedenEditor/Templates/`）的源码通过 SDK include 根引用引擎头文件，引擎侧目录或类名变动后必须同步更新模板。

---

# 变更纪律

- 大范围重命名/移动必须一次性覆盖：引擎源码、编辑器与玩家代码、模板与示例、测试、`Docs/`、工程文件与 CI 脚本。
- 提交前确认全仓库不存在旧类名与旧路径残留（排除 `ThirdParty/`、SDK 发布产物与构建目录）。
- 完成结构变更后至少构建通过 `OrbedenCore`、`OrbedenEditor`（Player 由编辑器构建流程驱动，需带 `-p:OrbedenProjectDir` 等参数）。
- 引擎处于开发期，不引入兼容层：旧场景、旧名称由使用方同步更新。
