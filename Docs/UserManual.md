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

如果项目由更早版本的 Orbeden 创建，编辑器会弹出升级对话框：

- **升级**：整块重建 `Content/` 之外的一切（引擎 SDK、工程文件与构建脚手架）并重建 `Content/Examples/` 示例。**`Content/` 里你自己的东西不会被改动。** 升级失败时对话框会保留并显示原因，版本号不会写入，可以重试或直接退出。
- **退出**：什么都不做，关闭对话框，编辑器保持原样（已经打开的项目不受影响）。

如果提示项目由更新版本的 Orbeden 创建，说明编辑器落后于项目版本，需要先更新 Orbeden；此时不支持升级。

### 项目目录

```text
MyGame/
├─ MyGame.oeproj          项目配置：启动场景和项目版本
├─ MyGame.csproj          工程文件直接放在项目根
├─ MyGameNative.vcxproj
├─ Directory.Build.props
├─ Lib/                   SDK 快照，由编辑器自动刷新
├─ Content/               内容根：你想放什么、怎么放都行
│   ├─ Meshes/  Materials/  Textures/  Shaders/  Scenes/  Scripts/
│   └─ Examples/FlightTraining/
└─ Build/                 全部构建产物
```

`Content/` 是游戏资产目录，其中的目录结构完全自由——可以随意增删改名，把资源、场景、脚本放到任何位置。`Content/` 内任何目录中的文件都会被自动收集。`Content/` 之外是引擎的地盘：工程文件、SDK 快照和 `Build/` 里的产物都由引擎维护，升级项目时会整块重建，请勿手工修改，也不要把自己的东西放在那里。


### Examples 目录

新建项目时会生成 `Content/Examples/FlightTraining/`，其中是完整的飞行模拟示例，也是新项目的启动场景。示例的场景、资源和脚本都会参与构建，可以直接在 Inspector 中查看和复用。

升级项目时 `Content/Examples/` 会被整目录重建，**你在其中做的修改会丢失**。要长期保留的内容请放到 `Content/Examples/` 之外。

## 3. 编辑场景和组件

1. 在 Ens View 中选择场景对象。
2. 在 Inspector 中编辑名称、Transform 和其他组件。
3. 使用 `Add Component` 添加组件：
   - `[C#]` 表示 C# 脚本组件。
   - `[C++]` 表示原生 C++ 组件。
4. 使用 `Ctrl+S` 或 `Project > Save` 保存 World，其中包含场景组件及其字段数据。

主窗口标题显示当前场景：`场景名 - 项目名 - Orbeden Editor`。场景相对磁盘文件有未保存改动时，场景名后面追加 `*`；保存成功后星号消失。Play 期间的改动不落盘，也不会标星号；切换场景、重新加载项目都会清掉星号。撤销回保存前的状态不会消掉星号。

层级面板支持 Ctrl 点击多选。移动节点保留世界变换，调整父级或同级顺序可以撤销与重做。

按住 `Alt` 点节点前的展开箭头时递归生效：展开会连同整棵子树一起展开，折叠会连同整棵子树一起折叠。

### 场景视图手柄

选中场景对象后，场景视口里会出现移动、旋转、缩放手柄，拖动即可直接改 Transform，Inspector 数值同步刷新。

- `W` / `E` / `R` 分别切换到移动、旋转、缩放手柄，视口左上角的三个图标按钮等价，另有一个按钮显示并切换当前坐标系。
- `X` 在 Global 与 Local 之间切换坐标系。Local 下手柄轴跟随主选中对象的自身旋转；缩放始终使用局部轴。
- 移动：拖单根轴沿该轴平移，拖轴之间的平面块在平面内平移，拖中心方块在屏幕空间自由平移。
- 旋转：拖任意一条圆环绕对应轴旋转，拖最外圈的屏幕朝向环做自由旋转。
- 缩放：拖轴端方块按该轴缩放，拖中心方块整体等比缩放。
- 多选时手柄出现在选择集包围盒中心，拖动整体生效。一次拖拽只记录一条撤销操作，`Ctrl+Z` 一次回到拖动前。
- 拖拽过程中按 `Esc` 取消并还原到拖动前；`Alt` 拖动仍然是相机操作；Play 模式下不显示手柄。

### Panel 外观与停靠

- 停靠组右上角的 `×` 关闭当前 Panel，可通过 Views 菜单重新打开；Panel 实例保留。
- 主窗口内的浮动 Panel 可以直接拖动标题栏停靠，不再需要内容区的 Dock 按钮。停靠标签释放到停靠区域外时转为浮动。
- 浮动标题栏右键菜单中的 `Merge into Main Window` 优先合回原停靠组；`Views > Merge Floating Panels` 合并所有可见浮动 Panel。原停靠组记忆随布局保存。
- `EditorTheme.Current` 统一定义窗口和控件颜色、内容边距、间距及分隔条尺寸，替换主题后下一帧生效。颜色采用 `0xAABBGGRR`。
- 把停靠标签拖到主窗口之外释放，会为该 Panel 创建独立的系统窗口；标签右键的 `Float in New Window` 效果相同。独立窗口使用系统标题栏与关闭按钮，内部不重复绘制 Panel 外壳。
- 拖动独立窗口的标题栏移动后松手，若光标落在主窗口客户区内则停回主窗口，优先回到原停靠组；也可通过 `Merge into Main Window` 菜单或 `Views > Merge Floating Panels` 合并。
- 独立窗口的位置、尺寸与浮动状态随布局保存。恢复时若窗口完全落在当前显示器工作区之外（例如副显示器被移除），会被移回主显示器。
- 独立窗口与主窗口之间可以直接拖放：把主窗口层级面板中的 Ens 拖到独立窗口里的 Inspector 引用框，或反向拖放 Project 资产，行为与同窗口一致。
- 场景视口只渲染在自己的 Panel 内容区内，与其它 Panel 一样参与停靠、拆分、浮动与并入标签页，也可以关闭后从 Views 菜单重新打开。

### 引用拖放与预制体

- Inspector 的 Object 引用框按字段声明类型筛选，支持清空、点击定位和搜索选择。将 Ens 拖到 Component 引用框时，单个匹配组件直接赋值，多个匹配组件弹出选择器。
- Project 中的资源可以拖到匹配类型的引用框；独立资源不接受场景对象引用。字段修改使用现有撤销和重做。
- 将 Ens 拖到 Project 的目录节点或右侧当前目录区域，会保存包含完整子树的独立 `.prefab`。内部引用保留，外部场景引用在文件中清空，源场景不变；重名自动加序号。
- 将 `.prefab` 拖到层级节点上方、中央或下方，分别插入节点之前、作为子级或插入之后；拖到空白区创建根实例。实例保留局部变换，整个实例化记录为一次撤销操作；重做恢复操作时的快照。
- Project 目录之间支持拖动移动资产和目录，不能把目录移入自身子目录。
- 目录树与层级面板一致：按住 `Alt` 点目录前的展开箭头，展开或折叠会连同整棵子树一起生效。

模型、材质、贴图和 Shader 放在 `Content/` 下（默认分别放 `Meshes/`、`Materials/`、`Textures/`、`Shaders/`），再通过 Project 面板和 Inspector 使用。


### Console 面板

原生 `Log::` 与托管 `Console` 两侧的输出都会进 Console 面板；操作系统控制台窗口照常打印，两处都不缺。`Console.Out` 记为信息级，`Console.Error` 记为警告级，错误级由编辑器自己的错误日志给出。

- 工具条一行排布：**搜索框在最左并占满左侧**，右端依次是 `Info` / `Warn` / `Err`（只做级别筛选，不显示条数）、`Collapse`（折叠连续重复并在行尾显示次数）、`Follow`（跟随最新一条）与 **`Clear`**，整组贴右。开关都是按钮形态（开启时用强调色底），不是勾选框。
- 搜索框不带标签，**输入即过滤**（不区分大小写的子串），不需要按回车或按钮。
- 列表隔行换底色，密集日志也能一眼分出行边界。
- **点一行才展开下方详情区**，里面是完整文本（异常堆栈在这里看）；再点同一行、或点进其它面板都会取消选中并收起。详情文本可以框选复制。
- 保留窗口保存最近 512 条、每条最多 2048 字节，超出后从最旧的开始丢弃。时间戳是本地时间。
- 日志一写入就唤醒消息循环，所以不需要用户操作也会即时显示；没有日志时编辑器完全不重绘。

### Profiler 面板

采集是自动的：面板打开或处于 Play 中就采集，两者都不成立时不采集，也不占内存。

- 走势图一帧一个像素宽，高度是该帧耗时，自下而上分三段：分类耗时（Script 蓝色、Render 黄色、Physics 橙色、FileIO 青色、Editor 紫色）、**限帧等待（深灰）**、**其它未统计事项（浅灰，即不在任何埋点里、也不是限帧等待的部分）**；横线是 16.7 ms 参考。宽度放不下时只画最近的若干帧。悬停显示帧号、帧耗时、实测工作量与占比最大的分类，点击选中该帧并停止跟随最新帧。
- 被限帧时深灰段会占绝大部分（工作往往只有一两毫秒，其余是在等下一个 1/60 秒节拍），这是正常的；**关键是看彩色段**——它涨到接近整条柱子并越过参考线才说明真的掉帧。
- `Hierarchy` 视图列出该帧的采样节点：`Total ms`、`Self ms`（不含子节点）、`Calls` 与占帧耗时的百分比。`Sort` 可选树序、Total、Self、Calls，除树序外都按降序。
- `Timeline` 视图是单帧甘特图：**一个深度一行**，同一深度的节点并排画在这一行上，子节点落到下一行；横轴是该帧的真实时间，每次调用画一个彩色条，名字写在条内（条太窄就裁掉）。悬停某条显示这次调用的耗时与起点，以及该节点在这一帧的合计。
- 甘特图支持**滚轮缩放**（以光标下的时刻为锚点，最大 50 倍），**双击**回到整帧视图；放大时工具条右侧显示当前倍数。窗口外的采样不绘制。
- 采样来自 C++ 侧 `PROFILE` 作用域。采样名用前缀标记分类（`Script/`、`Render/`、`Physics/`、`FileIO/`、`Editor/`），分类走势图只累计帧的**顶层**阶段，所以嵌套作用域不会重复计入；嵌套只影响层级表与甘特图的深度分层。
- 默认处于**停止**状态：打开面板不会自动开始采集，要按 `Record`。状态显示在按钮右侧（`Recording` 录制中、`Stopped` 已停止）。停止后已采集的数据保留，可以慢慢翻看；再次点 `Record` 从当前时间点继续采集。面板关闭且不在 Play 时也会自动停采。
- 走势图保留最近 1024 帧（60 帧/秒下约 17 秒），窗口内每一帧都带完整采样明细，点哪一帧看哪一帧。
- 采集数据每秒重读一次，不为走势图每帧去搬上千帧数据；面板在编辑器空闲时也会靠限时事件等待每秒醒一次（不需要用户操作），Play 中编辑器本来就连续出帧，走势图跟随每帧更新。
- 走势图刻度按窗口内帧耗时的 **95 分位**取值：偶发的一根超高柱子会被截平在顶部，真实峰值写在刻度文字后面（`max xx.xx ms`），这样单次尖峰不会把整幅图压平。悬停仍显示该帧的真实耗时。
- 每帧最多记录 128 个采样事件，超出会丢弃并在面板上提示丢弃数量；分类耗时不受丢弃影响。`Clear` 只清帧历史，不动采样树。


### 快捷键

编辑器快捷键由统一的一套快捷键系统分发，菜单右侧显示的就是实际键位。

- 全局键：`Ctrl+S` 保存、`Ctrl+Z` 撤销、`Ctrl+Y` 或 `Ctrl+Shift+Z` 重做。
- 视口键：鼠标位于场景视口内时，`W` / `E` / `R` 切换移动、旋转、缩放手柄，`X` 切换 Global 与 Local 坐标系。
- 拖拽手柄时按 `Esc` 取消本次拖拽并还原。

文本输入框获得焦点时，全局键不触发编辑器操作。Play 模式下整套快捷键失效，键盘输入交给游戏的输入系统；非 Play 模式下游戏输入系统完全屏蔽，输入只由编辑器处理。

Play 模式中的属性修改只影响本次运行。停止 Play 后会重新加载磁盘上的 World，不会保留运行时修改。

## 4. 编写 C# 脚本

在 `Content/` 下创建 `.cs` 文件（默认放 `Scripts/`），并继承 `Script`：

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

`[HideInEditor]` 只隐藏字段，字段仍可保存并随删除 Undo 恢复。不要声明名为 `domain`、`managedTypeName` 或 `enabled` 的序列化字段，这些名称由宿主管理。

C# 文件修改后：

1. 打开 `Views > Build Game`。
2. 点击 `Build Game C#`。
3. 构建成功后，在 Inspector 中添加对应的 `[C#]` 组件。

点击 Play 时，如果 C# 输出缺失或已经过期，Editor 也会自动尝试重新构建。

## 5. 编写 C++ 脚本

在 `Content/` 下任何目录中创建头文件和源文件（例如 `Scripts/`）：

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

public 支持字段会自动进入元数据、序列化和 Inspector；非 public 字段可以在声明前添加 `ORBEDEN_SERIALIZE_FIELD`。

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

C++ 代码修改后必须先执行 `Build Game C++`。C# 代码可以手动执行 `Build Game C#`，也可以让 Play 检查并构建过期脚本。

构建结果和错误会显示在 Build Game 面板的状态区域以及日志中。

各生命周期阶段固定先执行 C++，再批量执行 C#。禁用后重新启用不会重复 Start；已启动脚本在移除、Ens 销毁或停止运行时执行一次 End。PIE Inspector 修改会同时更新原生宿主和当前 C# 对象；游戏代码直接修改普通 C# 字段只影响本次运行，不自动写回保存数据。

脚本只需在 `OnEnd()` 中释放自己持有的资源，无需手动注销运行时或断开 Wrapper。从未启动的脚本不会调用 `OnEnd()`。停止或重载后，旧 C# Wrapper 会失效，应重新获取组件引用。

## 7. 构建 Player

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
5. 把 `Content/` 内的资源导入后打包成二进制产物，生成完整发布目录。

第 5 步会把内容根内全部资源导入当前进程，因此**构建结束后当前场景会重载一次**。

默认 Windows 输出位置（在被打包的项目目录内）：

```text
{项目目录}/Build/windows-x64/bin/
```

这是一个**自包含发布目录**，整个目录拷到别的路径或别的机器都能直接运行：

```text
bin/
├─ OrbedenGame.exe
├─ glfw3.dll
├─ {程序集名}.dll          游戏脚本 AOT 库
├─ {项目名}.oeproj         启动场景配置
└─ Content/
    ├─ <哈希>.orbo         资源产物，扁平存放
    ├─ cooked.index        文件名与资源名对照表
    └─ **/*.world          场景，保持原目录结构
```

发布目录里**没有** `.obj` / `.png` / `.mtl` / `.orbshader` 等原始资源，也没有脚本源码——它们已在打包时转换或编译。

其他目标平台需要对应的编译器、系统库和 NativeAOT 工具链。FreeBSD 与 Switch 目前只是预留目标，不能作为完整发布流程使用。

### 打包相关约定

- **不要把 `.orbo` 放进工程的 `Content/`。** 引擎解析资源时产物优先于源文件，且不会有任何提示；一旦 `Content/` 里出现同名产物，改源文件就不再生效。产物只会生成在 `ResourceCache/` 和发布目录。
- `ResourceCache/` 是打包用的中间缓存（相当于 Unity 的 `Library/`），可以随时删除，`Build Player` 会全量重建。Editor 不读它，开发时改资源即时生效。
- 改了资源的字段结构（增删字段、调顺序）后，**必须重新 `Build Player`**，旧发布目录不能再使用。
- `Build Player` 每次都会重新导入全部资源，项目资源多时这一步会比代码构建更久。

## 8. 常见问题

### 新项目提示需要编译 Native 脚本

这是 World 硬挂载项目 C++ 组件时的正常首次构建状态。Editor 会自动尝试 MetaGen、编译、加载 DLL 并重载 World；如果自动构建失败，打开 `Views > Build Game` 查看状态，修复 Visual Studio C++ 或 SDK 路径后点击 `Build Game C++`。项目本身仍然保持打开；在启动 World 完整重载前，Save 和构建前保存都不会覆盖磁盘中的 World。

### 项目提示内置 Shader 缺失

如果项目打开后出现 `shadow_depth.orbshader` 或 `skybox.orbshader` 缺失，说明引擎在内容根内按文件名没找到它们。把这两个文件放回 `Content/` 下的任意位置即可（新建项目默认在 `Content/Shaders/`）。

### Inspector 中看不到新 C# 脚本

先点击 `Build Game C#`，并确认脚本有 public 的 `ScriptType(Ens ens)` 构造函数。

### Inspector 中看不到新 C++ 组件

确认类包含 `OBJECT_TYPE_DECLARE` 和 `OBJECT_TYPE_IMPLEMENT`，然后点击 `Build Game C++`。

### 生命周期函数没有执行

检查名称、参数和返回类型是否完全正确，并确认方法没有声明为 `static`、`virtual` 或泛型方法。

### 改了内容但标题栏没有星号

检查是否处于 Play 模式：Play 期间的改动不落盘，不会标星号。只在场景里移动相机（右键、中键拖动或滚轮）不算改动，编辑器相机不进场景文件。

### 没改内容标题栏却有星号

星号的含义是"本次会话内动过场景"，不追踪"撤销回保存时的状态"。切换场景或保存一次即可清掉。
