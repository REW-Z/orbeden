# Orbeden 用户手册

本文说明如何创建游戏项目、编写游戏逻辑、在 Editor 中运行，以及构建 Player。

## 1. 准备开发环境

Windows Editor 开发需要：

- .NET 10 SDK，用于编译 C# 脚本。
- CMake 和 Visual Studio 2022 C++ 工具链，用于编译 C++ 游戏模块。
- 已构建或正式分发的 Orbeden Editor。源码环境第一次使用时，应先构建 `OrbedenCore.vcxproj`，再构建 `OrbedenEditor.vcxproj`。

## 2. 创建和打开项目

### 创建项目

1. 启动 Orbeden Editor。
2. 选择 `Project > New...`。
3. 在 `Parent Path` 中选择项目的父目录。
4. 输入 `Project Name`，然后点击 `Create`。

项目名必须以字母或下划线开头，只能包含字母、数字和下划线。目标目录必须不存在或为空目录。

新项目会自动创建默认 World、资源、C# 脚本和 C++ 脚本，并直接在 Editor 中打开。

### 打开已有项目

选择 `Project > Load...`，然后选择包含 `.oeproj` 文件的项目根目录。

### 项目目录

```text
MyGame/
├─ MyGame.oeproj    项目配置和启动 World
├─ World/           场景文件及 C# 脚本挂载数据
├─ Resource/        模型、材质、贴图和 Shader
├─ Script/          C# 游戏代码
├─ Native/          C++ 游戏代码和 CMake 配置
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
4. 使用 `Ctrl+S` 或 `Project > Save` 保存 World 和 C# 脚本挂载数据。
模型、材质、贴图和 Shader 放在 `Resource` 目录中，再通过 Project 面板和 Inspector 使用。


`Ctrl+Z` 用于撤销，`Ctrl+Y` 或 `Ctrl+Shift+Z` 用于重做。

Play 模式中的属性修改只影响本次运行。停止 Play 后会重新加载磁盘上的 World，不会保留运行时修改。

## 4. 编写 C# 脚本

在 `Script` 目录中创建 `.cs` 文件，并继承 `ScriptBehaviour`：

```csharp
using Orbeden;

namespace MyGame;

public sealed class MoveBehaviour : ScriptBehaviour
{
    public float speed = 2.0f;

    public MoveBehaviour(Ens ens) : base(ens) {}

    protected void OnStart()
    {
    }

    protected void OnUpdate(float deltaTime)
    {
        vector3 position = Ens.Transform.localPosition;
        position.x += speed * deltaTime;
        Ens.Transform.localPosition = position;
    }

    protected void OnEnd()
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

public 字段会进入序列化和 Inspector。private/protected 字段需要添加 `[SerializeField]`。

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

#include "Scripting/ScriptBehaviour.h"

class MoveBehaviour final : public ScriptBehaviour
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
#include "Runtime/Object/TransformComponent.h"

OBJECT_TYPE_IMPLEMENT(MoveBehaviour, ScriptBehaviour)

void MoveBehaviour::OnStart()
{
}

void MoveBehaviour::OnUpdate(float32 deltaTime)
{
    TransformComponent* transform = GetEns()->Transform();
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

开发速度优先时使用 C#；需要大量计算或稳定高性能逻辑时使用 C++。

## 6. 运行和调试

顶部工具栏提供 Play、Pause 和 Stop：

- Play：保存当前 World，并使用 CLR 运行 C# 脚本。
- Pause：暂停游戏模拟。
- Stop：结束运行并恢复磁盘中保存的 World。

C++ 代码修改后必须先执行 `Build Game C++`。C# 代码可以手动执行 `Build Game C#`，也可以让 Play 检查并构建过期脚本。

构建结果和错误会显示在 Build Game 面板的状态区域以及日志中。

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

默认 Windows 输出位置：

```text
OrbedenGame/Build/windows-x64-clang-cl/bin/OrbedenGame.exe
```

其他目标平台需要对应的编译器、系统库和 NativeAOT 工具链。FreeBSD 与 Switch 目前只是预留目标，不能作为完整发布流程使用。

### 当前打包限制

`Build Player` 当前完成代码构建，但不会自动生成完整的可分发目录。Player 在构建时绑定当前打开的项目路径，并从该项目读取 `.oeproj`、`World` 和 `Resource`。

因此当前产物适合在开发机器上验证。要复制到其他机器直接运行，还需要后续实现“复制项目数据并使用相对路径”的正式打包步骤。

## 8. 常见问题

### Inspector 中看不到新 C# 脚本

先点击 `Build Game C#`，并确认脚本有 public 的 `ScriptType(Ens ens)` 构造函数。

### Inspector 中看不到新 C++ 组件

确认类包含 `OBJECT_TYPE_DECLARE` 和 `OBJECT_TYPE_IMPLEMENT`，然后点击 `Build Game C++`。

### 生命周期函数没有执行

检查名称、参数和返回类型是否完全正确，并确认方法没有声明为 `static`、`virtual` 或泛型方法。

### 修改在停止 Play 后消失

这是预期行为。需要持久化的修改应在非 Play 状态下完成并按 `Ctrl+S` 保存。

更深入的实现说明见 [脚本系统](ScriptSystem.md)，平台工具链和目录说明见 [构建与打包](BuildAndPackaging.md)。
