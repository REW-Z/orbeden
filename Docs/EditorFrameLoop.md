# 编辑器出帧与唤醒

编辑器是事件驱动的：没有输入、没有请求时它完全不出帧，CPU 占用为 0。这份文档说明这套调度由哪几层构成、每一轮循环在等什么，以及写面板与弹窗时必须遵守的约束。

这是用复杂度换来的性能：省下的是空闲 CPU，付出的是两件事——屏幕上那幅画面在空闲时是「死」的（不重画就不响应任何东西），以及**任何需要连续几帧的表现都得有人主动要帧**：自己写的动画要在推进时每帧请求（见「写代码时的规则」第 1 条），框架自带的那几处（模态暗化层、Play）由 `continuousRepaint` 统一认领。

---

## 分层：谁负责什么

| 层 | 负责 | 不负责 |
| --- | --- | --- |
| ImGui | 即时模式的两条属性：一帧就是一次全量重建（没有脏区域、没有局部重绘）；一切动画与时间推进都是 `io.DeltaTime` 的函数 | 事件循环、休眠与唤醒。库内没有任何等待调用，`io.DeltaTime` 是宿主喂进来的输入 |
| GLFW | 阻塞与唤醒的原语：`glfwWaitEvents`、`glfwWaitEventsTimeout`、`glfwPostEmptyEvent` | 何时该阻塞、何时该出帧 |
| 本项目 | 整个调度：`editor_main.cpp` 的循环、两个重绘标志、宽限期、限帧、`EditorSystem::RequestRepaint` 与 `WakeEventLoop`、日志唤醒 | — |

因此「出帧等于绘制」「动画逐帧推进」是 ImGui 的固有属性，而「空闲不出帧、要连续表现就自己请求重绘」是本编辑器框架的选择。**ImGui 自己从不主动出帧，出帧永远是宿主的责任**：它没有定时器、没有脏区域、也不会在后台推进任何东西。ImGui 官方示例是一路连续出帧（靠 vsync 限速），不会遇到这里的代价；凡是刻意做条件出帧的宿主都会遇到，代价得自己承担。

## 出帧与绘制是同一件事

Immediate GUI 里控件不是对象，而是函数调用：`EditorGUI.Button("OK")` 不创建任何东西，它只是「在这一帧里画一次这个按钮，顺便看一眼鼠标是不是点在它上面」。所以界面没有任何跨帧的记忆，一切都得每帧重新描述一遍。

于是「出帧」就是「拿起粉笔把整块黑板重写一遍」，ImGui 没有只擦一角的能力；`NewFrame → 各面板 DrawContent → Render → Present` 这条链就是重写加显示。屏幕上那幅看起来静止的界面是**最后一帧的残影**：它挂在显存里不会消失，但已经不再知道鼠标在哪、有没有新日志、又过去了多久。想让界面对任何事情作出反应，唯一办法是要一帧。

调节手段因此只有一个：**这一轮画不画**。

## 主循环

`OrbedenEditor/Src/editor_main.cpp`：

```
每轮循环：
  1. 取两个标志：continuousRepaint（需要连续重绘吗）、repaintRequested（有人请求重绘吗）
  2. 三选一等待
       PollEvents()                 本来就要画，不等
       WaitEventsTimeout(1.0)       宽限期内：最多等 1 秒
       WaitEvents()                 其余情况：死等，CPU 归零
  3. 画一帧：Tick → Update → Render → RenderEditorGUI → Present
  4. 需要连续重绘就按目标帧率节流：WaitForNextFrame（默认 60，targetFrameRate 为 0 时不限速）
```

三点容易误解的地方：

- **它不是状态机在检测「我是否空闲」**，而是每轮现算一次「这轮有没有必须画一帧的理由」，没有就选阻塞等待那条分支。
- **睡的是主循环线程，不是窗口**。`WaitEvents` 把当前线程挂起直到窗口事件队列里有东西；窗口本身照旧存在，屏幕上最后一帧也照旧由系统合成显示。
- **那 1 秒不是后台定时器**，是这一次等待选择「最多等 1 秒」，到点自己返回。没有定时器线程、没有周期性回调，因此不产生额外开销。

### 什么时候会画一帧

| 理由 | 来源 |
| --- | --- |
| 有输入事件 | 键鼠、窗口消息 |
| 有人请求重绘 | 面板自己（每秒刷新的 Profiler）、日志写入、自走的动画（聚焦飞行） |
| 连续重绘中 | 有控件处于活动或拖拽中、浮动窗口在交互、模态暗化层正在淡入淡出、Play 中且未暂停 |
| 到点了 | 宽限期内的 1 秒超时；连续重绘时按目标帧率节流 |

排查「某个表现卡住不动」时，起点就是这张表：**这段时间里有没有人在要帧。**

## 两个标志

| 标志 | 语义 | 谁置位 |
| --- | --- | --- |
| `repaintRequested` | **一次性**：每轮开头取走并清零。取到就保证这一轮画一帧、且不阻塞等待 | `EditorSystem::RequestRepaint()`；托管侧入口是 `EditorApplication.RequestRepaint()`，面板基类的 `RequestPeriodicRepaint` 也用它 |
| `continuousRepaint` | **持续性**：`NeedsContinuousRepaint()`——有控件处于活动或拖拽中、有浮动窗口在交互、模态暗化层正在淡入淡出、Play 中且未暂停 | `ImGui::IsAnyItemActive()`、`PanelManager::IsAnyFloatingItemActive()`、`ImGuiContext::DimBgRatio` 未达目标值、Play 状态 |

`continuousRepaint` 成立时每轮都 `PollEvents` 并按目标帧率节流——这是「拖手柄时画面跟手、松手后回到休眠」的来源。

模态暗化层是其中判据最讲究的一条。`DimBgRatio` 每帧才推进一点（涨 6/s、退 10/s），隔帧绘制会让它卡在几乎看不见的位置，所以**淡入淡出期间**必须连续出帧。但判据是「**还没走到目标值**」而不是「模态开着」：涨满只要 0.17 秒，按模态开着算会让弹窗停留的整段时间都跑满帧率，而新建/载入项目这类弹窗可能开着好几分钟。关窗那一帧模态已经不在栈里，判据改为「该退到 0 却还没退净」，所以淡出也接得上，不依赖任何输入事件。

## 三条唤醒路径

1. **输入事件**：键鼠消息让阻塞中的等待立刻返回。
2. **代码请求重绘**：`RequestRepaint()` 置一次性标志，同时 `WakeEventLoop()`（`glfwPostEmptyEvent()`）往事件队列塞一个空事件，让正在死等的循环立刻醒来。日志写入走的就是这条路（`Log::SetWakeHandler`），所以空闲时日志也能立即显示。
3. **时间**：宽限期内 1 秒一次的超时，或连续重绘下节流到点。

`RequestRepaint()` 是线程安全的（原子标志加唤醒），可以从任意线程调用；已经在等待重绘时重复调用不会重复唤醒。

## 宽限期

每当有一轮取到重绘请求，宽限截止时间就被推到 2100 毫秒之后；在截止之前用「最多等 1 秒」代替「死等」。这是留给「每秒刷新一次」的面板（Profiler 的 `RefreshIntervalSeconds = 1.0`）的活路：它每秒只请求一次重绘，若请求完就立刻回到死等，下一轮的请求就再也发不出来。宽限期过后回到死等，空闲占用归零。

## DeltaTime 是真实时间

`io.DeltaTime` 由 GLFW 后端设置后，又被编辑器用真实时间差覆盖（`EditorGUI::BeginFrame`）。因此空闲唤醒那一帧给出的可能是约 1 秒的 `DeltaTime`，ImGui 的动画会一步跳到位而不是平滑推进。喂给模拟（`Application::Tick`）的 delta 则是另一回事：空闲唤醒帧传 0，物理与脚本不会被唤醒本身推进。

## 写代码时的规则

1. **要连续几帧才完成的表现，必须自己每帧请求重绘**——动画、进度、需要连续刷新的自绘都算。相机聚焦动画就是这么做的：`EditorScene::Update` 每帧推进曲线的同时由 `IsAnimatingFocus()` 让 `EditorSystem::Update` 请求重绘，0.4 秒走完自然停下，不需要收尾逻辑。
   这条代价很实在：缺少请求时表现不会报错，只会「等一秒突然到位」——空闲唤醒那一帧的 `DeltaTime` 是真实的约 1 秒，靠逐帧累积的动画一步就顶满了。（模态暗化层同样需要连续帧，但它已经归入框架的 `continuousRepaint`，见上文，弹窗自己不必再请求。）
2. **不要无事每帧请求重绘**，那会把空闲 CPU 从 0 拉起来。只请求需要的次数：一次性的刷新请求一次，周期性的用 `EditorPanel.RequestPeriodicRepaint`。
3. **不要把时间推进放在空闲帧上**。空闲唤醒帧的模拟 delta 是 0，靠帧数量累积的逻辑（计时、动画曲线）在空闲时会停住。

## 相关代码

| 位置 | 作用 |
| --- | --- |
| `OrbedenEditor/Src/editor_main.cpp` | 主循环、宽限期、三种等待的选择 |
| `OrbedenEditor/Src/Editor/EditorSystem.cpp` | `RequestRepaint` / `TakeRepaintRequest` / `NeedsContinuousRepaint`、日志唤醒注册 |
| `OrbedenCore/Src/Platform/GlfwWindow.cpp` | `WaitEvents` / `WaitEventsTimeout` / `WakeEventLoop` 的实现 |
| `OrbedenCore/Src/Application.cpp` | `WaitForNextFrame` 与 `WaitUntilFrameTime`：限帧、容差与自旋窗口 |
| `OrbedenEditor/Managed/Orbeden.Editor/EditorApplication.cs` | 托管侧 `RequestRepaint()` |
| `OrbedenEditor/Managed/Orbeden.Editor/Panels/EditorPanel.cs` | `RequestPeriodicRepaint` |
| `OrbedenEditor/Src/Editor/EditorScene.cpp` | 相机聚焦动画：自行请求连续帧的示例（`IsAnimatingFocus`） |
| `OrbedenEditor/Managed/Orbeden.Editor/EditorDialog.cs` | 统一的模态确认弹窗；出帧由上面的暗化层规则统一负责 |
