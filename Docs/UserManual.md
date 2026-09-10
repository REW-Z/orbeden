# Orbeden 用户手册

本文说明如何创建游戏项目、编写游戏逻辑、在 Editor 中运行，以及构建 Player。

## 1. 准备开发环境

Windows Editor 开发需要：

- .NET 10 SDK，用于编译 C# 脚本。
- Visual Studio 2022+ 的 MSBuild C++ 工具链，用于编译 C++ 游戏模块（游戏 C++ 代码使用 vcxproj 工程）。
- 已构建或正式分发的 Orbeden Editor。源码环境第一次使用时，应先构建 `OrbedenCore.vcxproj`，再构建 `OrbedenEditor.vcxproj`。

## 2. 创建和打开项目

### 创建项目

1. 启动 Orbeden Editor。
2. 选择 `Project > New...`。
3. 在 `Parent Path` 中选择项目的父目录。
4. 输入 `Project Name`，然后点击 `Create`。

项目名必须以字母或下划线开头，只能包含字母、数字和下划线。目标目录必须不存在或为空目录。

新项目会自动创建默认 World、资源、C# 脚本和 C++ 脚本，并直接在 Editor 中打开。

默认模板是自由飞行场景，包含飞机、第三人称相机、机场、分块噪声地形和飞行仪表。World 挂载 C++ `FlightController`、`FlightTerrainStreamer`、`FlightOrbitCamera` 与 C# `FlightHud`。

创建完成后，Editor 会自动运行 MetaGen、编译并加载项目 C++ 模块，再重新加载启动 World；不需要在 Editor 外手动运行命令。如果本机 C++ 工具链不完整，项目仍会打开，Build Game 面板会显示“Native scripts still need to be compiled”，修复环境后点击 `Build Game C++` 即可重试。

### 打开已有项目

选择 `Project > Load...`，然后选择包含 `.oeproj` 文件的项目根目录。

### 项目目录

```text
MyGame/
├─ MyGame.oeproj    项目配置和启动 World
├─ World/           场景、组件及其字段数据
├─ Resource/        模型、材质、贴图和 Shader
├─ Script/          C# 游戏代码
├─ Native/          C++ 游戏代码和 vcxproj 工程
├─ Managed/         C# 开发构建输出
└─ Aot/             Player 的 C# NativeAOT 构建输出
```

通常只需要编辑 `Script`、`Native` 和 `Resource`。`Managed`、`Aot` 以及 `Native/Build` 都是生成目录。

## 3. 编辑场景和组件

1. 在 Ens View 中选择场景对象。
2. 在 Inspector 中编辑名称、Transform 和其他组件。
3. 使用 `Add Component` 添加组件：
   - `[C#]` 表示 C# 脚本组件。
   - `[C++]` 表示原生 C++ 组件。
4. 使用 `Ctrl+S` 或 `Project > Save` 保存 World，其中包含场景组件及其字段数据。

模型、材质、贴图和 Shader 放在 `Resource` 目录中，再通过 Project 面板和 Inspector 使用。


`Ctrl+Z` 用于撤销，`Ctrl+Y` 或 `Ctrl+Shift+Z` 用于重做。

Play 模式中的属性修改只影响本次运行。停止 Play 后会重新加载磁盘上的 World，不会保留运行时修改。

## 4. 编写 C# 脚本

在 `Script` 目录中创建 `.cs` 文件，并继承 `Script`：

```csharp
using Orbeden;

namespace MyGame;

public sealed class MoveBehaviour : Script
{
    public float speed = 2.0f;

    public MoveBehaviour(Ens ens) : base(ens) {}

    private void OnStart()
    {
    }

    private void OnUpdate(float deltaTime)
    {
        vector3 position = Ens.Transform.GetLocalPosition();
        position.x += speed * deltaTime;
        Ens.Transform.SetLocalPosition(position);
    }

    private void OnEnd()
    {
    }
}
```

生命周期使用固定名称和签名，不要写 `virtual` 或 `override`。可用方法包括：

```text
OnStart()
OnUpdate(float deltaTime)
OnFixedUpdate(float fixedDeltaTime)
OnLateUpdate(float deltaTime)
OnDrawGUI()
OnEnd()
```

`OnDrawGUI()` 在引擎 GUI 帧内执行，除标准控件外还可使用 `GUI` 自由绘制 API（线段、圆弧、文本、裁剪区、固定位置窗口）绘制 PFD、仪表等 HUD，详见 [脚本系统](ScriptSystem.md)。

public 字段会进入序列化和 Inspector。private/protected 字段需要添加 `[SerializeField]`。

每个 C# 脚本都绑定一个独立的原生 `Script` 组件，`InstanceId` 就是宿主的原生 ObjectId。请通过 `ens.AddComponent<MoveBehaviour>()` 创建脚本，不要直接 `new`。构造函数内可以访问 `Ens`、`InstanceId` 和 `enabled`；场景行为应放入 `OnStart`，因为 Editor 添加组件时也会执行构造函数来取得字段默认值。

`ens.GetComponent<MoveBehaviour>()` 返回最先挂载的实例，`ens.GetComponents<MoveBehaviour>()` 返回按挂载顺序排列的全部实例。同一个 Ens 可以添加多个同类型脚本；用 `[UniqueComponent]` 限制唯一实例，用 `[DependsOnComponent(typeof(...))]` 声明依赖。

`[HideInInspector]` 只隐藏字段，字段仍可保存并随删除 Undo 恢复。不要声明名为 `domain`、`managedTypeName` 或 `enabled` 的序列化字段，这些名称由宿主管理。

C# 文件修改后：

1. 打开 `Views > Build Game`。
2. 点击 `Build Game C#`。
3. 构建成功后，在 Inspector 中添加对应的 `[C#]` 组件。

点击 Play 时，如果 C# 输出缺失或已经过期，Editor 也会自动尝试重新构建。

## 5. 编写 C++ 脚本

在 `Native` 目录中创建头文件和源文件：

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
    void OnStart();
    void OnUpdate(float32 deltaTime);
    void OnEnd();
};
```

```cpp
// MoveBehaviour.cpp
#include "MoveBehaviour.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/Transform.h"

OBJECT_TYPE_IMPLEMENT(MoveBehaviour, Script)

void MoveBehaviour::OnStart()
{
}

void MoveBehaviour::OnUpdate(float32 deltaTime)
{
    Transform* transform = GetEns()->Transform();
    vector3 position = transform->GetLocalPosition();
    position.x += speed * deltaTime;
    transform->SetLocalPosition(position);
}

void MoveBehaviour::OnEnd()
{
}
```

C++ 生命周期函数同样不能声明为 `virtual`。public 支持字段会自动进入元数据、序列化和 Inspector；非 public 字段可以在声明前添加 `ORBEDEN_SERIALIZE_FIELD`。

C++ 文件修改后：

1. 停止 Play。
2. 在 `Views > Build Game` 中点击 `Build Game C++`。
3. Editor 会自动运行 MetaGen、编译游戏 DLL 并重新加载组件。
4. 构建成功后，在 Inspector 中添加对应的 `[C++]` 组件。

不要手动编辑 MetaGen 生成的 `Reflection.Generated.cpp`。

Inspector 的添加菜单用 `[C++]` 和 `[C#]` 区分语言，每个 C# 脚本只显示一个组件卡片。多选添加会检查唯一性和依赖，失败时回滚本次创建。删除组件后 Undo 会恢复完整字段和原挂载位置。属性提交失败会显示错误信息。

两种语言的组件和字段都保存在 `.world` 中，按 `Ctrl+S` 保存。对象引用保存资源 Key 或 World 稳定路径；不要把运行时 `InstanceId` 当作持久化引用。缺少 C# 类型时会显示 `Missing Script`，已有字段保留，修复并重新加载程序集后可重新连接。

### C++ / C# 互操作

Object 派生的 C++ 组件由 MetaGen 自动生成强类型 C# 包装，直接 `ens.AddComponent<MoveBehaviour>()` 并使用类型化成员。对没有生成 Binding 的 C++ API（非 Object 派生类）使用 `ens.GetNativeComponent("MoveBehaviour")` 得到动态代理。例如 `proxy.SetField("speed", InteropValue.From(4.0f))`。C++ 调用 C# 脚本时，使用 `ScriptInterop::FindManagedComponent(ensId, "MyGame.MoveBehaviour", 0)`；最后一个参数选择同类型的第几个实例。

重复调用应缓存成员句柄 `MemberHandle`、`ComponentField` 或 `ComponentMethod`。按名称的动态 `Invoke` 适合低频工具调用；每帧大量互操作优先使用强类型 Binding 或批量 API。程序集或原生模块重载后必须重新获取 Wrapper 和代理，重载前取得的句柄会失效。完整示例见 [脚本系统](ScriptSystem.md)。

开发速度优先时使用 C#；需要大量计算或稳定高性能逻辑时使用 C++。

## 6. 运行和调试

顶部工具栏提供 Play、Pause 和 Stop：

- Play：保存当前 World，并使用 CLR 运行 C# 脚本。
- Pause：暂停游戏模拟。
- Stop：结束运行并恢复磁盘中保存的 World。

默认模板使用 PhysX 刚体和简化气动模型。机翼升力垂直于气流和翼展；垂直安定面根据尾部侧向气流产生回正力矩，配合偏航、滚转和俯仰阻尼。场景采用自由飞行，地图随飞机位置动态扩展。

- `Left Shift / Left Ctrl`：增加 / 减少油门，初始油门为零。
- `W / S`：压低 / 抬高机头。
- `A / D`：左倾 / 右倾（A 压低左翼，D 压低右翼）。
- `Q / E`：左 / 右方向舵，接地时同时控制前轮转向。
- `R`：返回机场并清空速度与油门。
- `F`：切换受力箭头，默认开启。
- 按住鼠标右键拖动：围绕飞机观察，松开后保持观察方向；相机跟随位置，不继承机身滚转。
- `C`：将环绕相机恢复到当前机尾方向。

Play 中的受力线从飞机原点出发：红色为阻力（包含垂尾侧向阻力），蓝色为升力，黄色为推力，黑色为重力。全部箭头使用相同的 `forceDrawScale`，默认 `0.0005` 米/牛顿，即 1 kN 对应 0.5 米；箭头关闭深度测试，便于从机身和地面遮挡中观察。箭头和 HUD 在物理更新后按当前姿态、速度重新计算瞬时受力，不额外施力，也不进行显示平滑；黄色推力箭头始终沿当前机头正前方，长度随油门及前向速度变化。HUD 同步显示四种力的 kN 数值、爬升率与侧滑角。重力读取 PhysX 配置，仅绘制，不重复施加；起落架地面支持力和舵面力矩不包含在这四个箭头内。

`FlightController` 的翼面积默认 24 m²、升力倍率 1.3，阻力使用 `Cd0 + k × Cl²`，推力随前向速度衰减。模板刚体关闭额外线性阻尼，使空中平移受力与箭头一致。可在 Inspector 调整 `verticalFinArea`、`sideForceSlope`、`yawDamping`、`liftMultiplier` 和 `inducedDragCoefficient`。垂尾消除侧滑，不会自动把滚转姿态摆平；转弯时升力倾斜，仍需保留空速并适当拉杆。

`FlightOrbitCamera` 挂在飞机下的相机实体上，启动时记录父级飞机并脱离层级，以世界竖直方向保持地平线稳定。可调整 `distance`、`sensitivity` 和 `defaultElevation`；停止时恢复原来的父级及局部姿态。

`FlightTerrainStreamer` 在 Play 中围绕飞机生成地形，每块 512 × 512 米、65 × 65 个高度样本，目标加载范围为 7 × 7 块，每个物理步最多新增 2 块。外围保留一圈卸载缓冲，机场块固定保留以供返回；CPU 网格、GPU 资源及碰撞体随远块卸载回收。邻块使用全局噪声坐标、连续法线和 UV，并共享同一张可平铺噪声纹理。编辑模式预览机场所在的一块地形，Play 才扩展周围地图。

飞行超过浮动原点阈值（默认 4096 米）时，世界根节点按整块距离平移，飞机速度保持不变，地形继续按原来的全局块索引生成。地图随飞行持续扩展；坐标与块索引仍受数值类型范围限制。分块生成在主线程按预算执行。`chunkSize` 与 `samplesPerSide` 在进入 Play 时确定；修改后重新进入 Play 生效。

原生脚本可通过 `RenderSystem::Current()->DrawLine(world, start, end, color, depthTest, drawLayer)` 提交世界空间线条。线条在当前帧的全部相机目标中绘制后清除，推荐从 `OnLateUpdate` 提交；调用者需检查当前渲染系统指针。

C++ 代码修改后必须先执行 `Build Game C++`。C# 代码可以手动执行 `Build Game C#`，也可以让 Play 检查并构建过期脚本。

构建结果和错误会显示在 Build Game 面板的状态区域以及日志中。

各生命周期阶段固定先执行 C++，再批量执行 C#。禁用后重新启用不会重复 Start；已启动脚本在移除、Ens 销毁或停止运行时执行一次 End。PIE Inspector 修改会同时更新原生宿主和当前 C# 对象；游戏代码直接修改普通 C# 字段只影响本次运行，不自动写回保存数据。

脚本只需在 `OnEnd()` 中释放自己持有的资源，无需手动注销运行时或断开 Wrapper。从未启动的脚本不会调用 `OnEnd()`。停止或重载后，旧 C# Wrapper 会失效，应重新获取组件引用。

## 7. 构建 Player

正式发布建议使用 Release 版 Editor，并首先以 `Windows x64` 验证。

1. 停止 Play。
2. 按 `Ctrl+S` 保存项目。
3. 打开 `Views > Build Game`。
4. 在 `Target Platform` 中选择目标平台。
5. 点击 `Build Player`。

构建过程会：

1. 将 Core C# 和游戏 C# 发布为 NativeAOT。
2. 运行 MetaGen，并把游戏 C++ 源码编译进 Player。
3. 重新编译 Player 版 OrbedenCore。
4. 链接最终 Player。

默认 Windows 输出位置（在被打包的项目目录内）：

```text
{项目目录}/Build/windows-x64/bin/OrbedenGame.exe
```

其他目标平台需要对应的编译器、系统库和 NativeAOT 工具链。FreeBSD 与 Switch 目前只是预留目标，不能作为完整发布流程使用。

### 当前打包限制

`Build Player` 当前完成代码构建，但不会自动生成完整的可分发目录。Player 在构建时绑定当前打开的项目路径，并从该项目读取 `.oeproj`、`World` 和 `Resource`。

产物适合在开发机器上验证，运行时需要保留构建时绑定的项目路径及项目数据，不能直接作为独立发行包复制到其他机器。

## 8. 常见问题

### 新项目提示需要编译 Native 脚本

这是 World 硬挂载项目 C++ 组件时的正常首次构建状态。Editor 会自动尝试 MetaGen、编译、加载 DLL 并重载 World；如果自动构建失败，打开 `Views > Build Game` 查看状态，修复 Visual Studio C++ 或 SDK 路径后点击 `Build Game C++`。项目本身仍然保持打开；在启动 World 完整重载前，Save 和构建前保存都不会覆盖磁盘中的 World。

### 项目提示内置 Shader 缺失

如果项目打开后出现 `shadow_depth.orbshader` 或 `skybox.orbshader` 缺失，请确认项目的 `Resource/Shader` 目录未被删除。

### Inspector 中看不到新 C# 脚本

先点击 `Build Game C#`，并确认脚本有 public 的 `ScriptType(Ens ens)` 构造函数。

### Inspector 中看不到新 C++ 组件

确认类包含 `OBJECT_TYPE_DECLARE` 和 `OBJECT_TYPE_IMPLEMENT`，然后点击 `Build Game C++`。

### 生命周期函数没有执行

检查名称、参数和返回类型是否完全正确，并确认方法没有声明为 `static`、`virtual` 或泛型方法。

### 修改在停止 Play 后消失

这是预期行为。需要持久化的修改应在非 Play 状态下完成并按 `Ctrl+S` 保存。

### 飞行地形材质和机轮参数

地形材质引用使用 `Resource/Mesh/ground.obj//Material/GroundMaterial`：MTL 中的材质属于 OBJ 导入产生的子资源，应使用 OBJ 下的材质资源路径。`WheelCollider.suspensionRestLength` 表示安装点到轮心的距离，轮半径单独参与接地计算。

更深入的实现说明见 [脚本系统](ScriptSystem.md)，平台工具链和目录说明见 [构建与打包](BuildAndPackaging.md)。
