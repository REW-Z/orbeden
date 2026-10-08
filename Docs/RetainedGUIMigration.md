# RetainedGUI：向 uGUI 式扩展架构迁移

日期：2026-10-03。状态：引擎侧已按第 10 节五阶段实施；游戏工程侧的扩展案例验收待执行。

## 1. 目标与范围

目标是让游戏开发者通过 C# 继承、组件组合与材质资源扩展 UI，达到 uGUI 的常用扩展程度。
原生层继续提供渲染执行、引擎对象、资源与平台服务；不以 C# 文件数量或代码占比作为验收指标。

保留现有控件名称、场景数据和功能范围：Overlay、WorldSpace、Offscreen、全部控件、软遮罩、中文输入法、触摸、手柄及 Bitmap／SDF／MSDF 字体。
迁移以现有实现为基础，逐项补齐扩展入口，不重写整个 UI 系统。
本文件只规定迁移工作；现有空间约定、生命周期、序列化与构建规则遵循 [ProjectConventions.md](ProjectConventions.md)。

## 2. 参考依据与适用边界

已下载官方 [Unity-Technologies/uGUI](https://github.com/Unity-Technologies/uGUI) 的 main 分支快照，包内版本为 7.0.0。
本地目录：`C:/Users/leona/AppData/Local/Temp/orbeden-ugui-reference-20261003/uGUI-main/com.unity.ugui/`。
下载日期为 2026-10-03；这是分支快照，不是固定发布标签。
ZIP SHA256：`A095236FE2F4EFC02995EBADC3AF715D5F1514F5D946E8E61C77EE7EBEF8232E`。

以下文件路径相对于该包的 `Runtime/UGUI/`：

| 参考文件 | 采用的架构原则 |
|---|---|
| `UI/Core/Graphic.cs` | C# 生成网格，执行网格与材质修改器，再提交给渲染器 |
| `UI/Core/CanvasUpdateRegistry.cs` | 集中登记布局与图形脏标记，按阶段重建 |
| `UI/Core/Layout/LayoutRebuilder.cs` | 水平测量、水平排列、垂直测量、垂直排列 |
| `UI/Core/Mask.cs`、`RectMask2D.cs`、`MaskableGraphic.cs` | C# 管理遮罩关系与裁剪策略，调用底层渲染能力 |
| `EventSystem/` | C# 负责输入模块、命中、事件处理接口与导航 |
| `UI/Core/Text.cs` | 托管控件可以依赖引擎文字生成服务，不要求字体计算全部托管化 |

[CanvasRenderer 绑定](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Modules/UI/ScriptBindings/CanvasRenderer.bindings.cs)、[Canvas 绑定](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Modules/UI/ScriptBindings/UICanvas.bindings.cs) 和 [RectTransform 绑定](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Runtime/Transform/ScriptBindings/RectTransform.bindings.cs) 表明这些类型具有原生实现边界。
公开绑定不是 Unity 的 C++ 实现，不能据此推断其内部缓存与批处理算法。
Orbeden 保留自己的 `Canvas`、`UILayout` 和批量 ABI，不要求照搬 Unity 的类型位置、逐对象调用方式或 stencil 遮罩实现。

## 3. 当前实现与迁移判断

本次核对范围为当前 UI 的公开接口、重建流程、输入路由与渲染提交边界，未浏览 Git 工作区差异。
下表的“新增接口”当时均为计划；实施结果见第 12 节。

| 当前实现 | 判断 | 迁移动作 |
|---|---|---|
| `UIVisual.PopulateMesh`、`ModifyDrawState`、脏标记 | 已有继承扩展基础 | 补组合式修改器，修正状态重算边界 |
| `UIMeshBuilder` 公开集合只读，支持追加几何 | 修改既有网格不方便 | 增加受控编辑接口 |
| `UIDrawState` 只有纹理、固定材质种类、颜色与距离参数 | 自定义 shader 无法通过通用材质入口接入 | 接入引擎 `Material` |
| `UIControl` 已提供虚拟指针、滚轮、导航与焦点回调 | 控件继承可用 | 补独立组件事件接口 |
| `UIInputModule` 与 `SetInputModule` 已存在 | 已符合输入策略可替换方向 | 保留并验证外部使用 |
| `UIRaycaster` 固定实现并从节点寻找 `UIControl` | 普通脚本难以独立接收 UI 输入 | 事件接收与控件状态分离，补命中过滤器 |
| 布局接口与注册表位于 C# | 分层正确 | 验证自定义布局可直接接入 |
| 离屏依赖收集只识别 `Image.GetTexture()` | 自定义图形可能漏排依赖 | 改为按最终绘制资源收集 |
| 原生 `UIRenderer` 执行渲染和资源管理 | 分层正确 | 仅增加通用材质提交支持 |

## 4. 迁移后的职责分配

| C# 保持或补齐 | C++ 保持 |
|---|---|
| 控件行为、事件、焦点、导航、拖动阈值与连发策略 | 平台鼠标／键盘／触摸／手柄采样、IME 和剪贴板服务 |
| UI 层级、布局、脏标记与重建调度 | Ens／Transform、对象句柄、引擎生命周期服务 |
| 网格生成、网格效果、材质选择与参数策略 | GPU buffer、纹理上传、shader、材质绑定和绘制执行 |
| 遮罩关系、裁剪区域与命中规则 | Scissor、软遮罩纹理、渲染目标池、深度读取 |
| 文本编辑、排版策略与现有字形缓存策略 | FreeType／距离场等字体服务及资源读取 |
| 画布提交顺序、离屏依赖和输入呈现快照 | 渲染通道接入、GPU 缓存寿命与延迟释放 |

不搬迁原生渲染目标池、GPU 缓存、资源对象或平台后端。
不因为 Unity 的 RectTransform 在原生侧，就把当前 C# `UILayout` 反向重写为 C++。
布局与文字的现有功能无需因本次迁移重新实现。

## 5. 图形与网格扩展

修改位置：`OrbedenCore/Managed/Orbeden.UI/Runtime/` 下的 `UIVisual.cs`、`UIMeshBuilder.cs`，新增 `IUIMeshModifier.cs`。

新增公开接口 `IUIMeshModifier.ModifyMesh(UIMeshBuilder mesh)`。
挂在图形同一 Ens 上、启用且实现该接口的托管组件参与修改；按组件序列化顺序执行，不使用反射发现顺序。
组件增删、启停、顺序变化时刷新缓存并标记顶点脏；修改器参数变化由修改器调用目标图形的 `SetVerticesDirty()`。

`UIMeshBuilder` 增加按下标读取／写入顶点的入口，以及保留片段状态的三角流读取／重建入口。
网格效果必须保留索引合法性、片段顺序与片段状态，不能把多纹理文字压成一个默认材质片段。
允许修改颜色和 UV、追加阴影几何；继续遵循引擎 CCW 绕序。

共享网格的重建顺序固定为：

1. 清空工作网格，设置画布的光栅缩放；WorldSpace 固定为 1。
2. 调用 `PopulateMesh`，完成图形自身片段。
3. 依次运行网格修改器，完成并校验修改后的片段。
4. 保存基础片段状态，计算最终绘制状态。
5. 更新网格版本，交给现有几何缓存上传。

任何修改器失败时清空本图形本次产物并记录组件信息，不提交半套几何。
执行修改器过程中新增的脏标记留到下一轮，不递归重建。
`UIVisual.IsGeometryInvalidated()` 调整为外部派生类可覆盖的 `protected internal` 扩展点；引擎仍控制调用时机。
材质变化不重新执行网格修改器；布局、几何或网格效果变化才进入几何重建。

验收：游戏代码新增圆环图形、阴影组件、顶点渐变组件，可分别挂载和组合，不改引擎 C++，不修改 `Image`／`Text` 源码。

## 6. 材质扩展与原生提交

修改位置：`UIDrawState.cs`、`UIVisual.cs`、`UIFrameBuilder.cs`、`RetainedGuiNative.cs`，以及原生 `RetainedGuiTypes.h`、`RetainedGuiBridge.*`、`UIRenderer.*`。
新增 `IUIMaterialModifier.cs`，接口为 `ModifyMaterial(ref UIDrawState state)`。
材质修改器同样按同 Ens 上启用组件的顺序执行；其参数变化标记材质脏。

`UIVisual` 新增序列化材质引用及 `GetMaterial()`／`SetMaterial(Material? value)`。
`UIDrawState` 增加 `Material? material`；空值使用现有内置 UI 材质。
保留 `UIMaterialKind` 作为内置图片与字体 shader 的模式参数，不再要求每种用户效果新增枚举值。
现有场景未指定材质时，外观和行为保持一致。

最终状态按以下顺序生成：

1. 从基础片段状态复制工作状态。
2. 执行图形的 `ModifyDrawState`，保留图片／文字自身的纹理与字体模式规则。
3. 应用图形显式材质覆盖，再运行材质修改器。
4. 合成图形颜色与控件状态色，生成不可在提交后继续修改的帧内状态。

当前 `RebuildMaterial` 会在已有片段上再次调用状态修改逻辑；迁移时必须分离基础状态与最终状态。
反复刷新同一颜色时不能重复乘色，也不能累积材质修改器的结果。
修改器不得直接改写共享材质资源；需要独立参数时使用明确归属、可释放的运行时材质实例。
实例创建沿用引擎现有资源能力；如缺少克隆能力，补通用材质能力，不建立 UI 专属资源系统。

提交协议增加材质对象标识，原生校验对象类型与有效性，并在已接收帧的有效期内保持资源引用。
原生通过现有材质／GPU 资源管理器解析 shader 与参数，C# 不持有 GPU 指针。
修改 ABI 时同步版本、结构尺寸、字段偏移、托管声明与原生校验；版本不符立即拒绝建立上下文。

自定义 UI shader 使用固定的 UI 顶点布局与公共参数约定：矩阵、主纹理、颜色、裁剪和软遮罩采样。
明确哪些参数由提交器写入；材质参数不能覆盖这些保留参数。
深度策略由画布模式决定，混合与软遮罩输出约定由 UI 渲染协议约束。
不允许材质的普通场景队列打乱 UI 层级顺序；不兼容 shader 给出诊断并使用默认 UI 材质。
提供一份可复制的 UI shader 模板，覆盖三种画布模式及软遮罩。

批次匹配必须包含有效材质身份与参数版本、纹理、裁剪状态及现有模式参数。
禁止跨遮罩命令、不同材质或不同深度状态合并；不新增一套 C# GPU 批处理器。

验收：新增溶解或灰度 UI 材质，仅需 C# 行为脚本和材质／shader 资源；无需新增原生材质枚举或控件分支。

## 7. 输入事件的组件组合

修改位置：`UIInputRouter.cs`、`UIRaycaster.cs`、`UIInputTypes.cs`、`UIControl.cs`；新增公开事件接口文件。
保留 `UIInputModule.Process`、`SetInputModule` 和 `StandardUIInputModule`，不重写平台采样。
保留现有业务事件队列、持久化绑定与代码订阅顺序；本节补齐的是输入处理入口。

按已有回调拆出指针、滚轮、导航、提交／取消、焦点接口，命名采用 `IUIPointerDownHandler` 等明确形式。
`UIControl` 实现对应接口，其现有虚拟方法继续作为控件派生入口。
普通托管组件可实现所需接口，不必继承 `UIControl`，也不必拥有可绘制网格。
处理器缓存随组件增删、启停和层级变化更新，不逐输入事件扫描程序集。

事件目标由命中图形所在 Ens 向祖先按事件类型寻找；首个具有有效处理器的 Ens 为该事件的处理节点。
同一节点按组件顺序执行；回调期间销毁或禁用的组件在调用前跳过。
明确区分“输入处理节点”和“负责状态／导航的 UIControl”，不能因找不到 UIControl 就丢弃所有事件。
内置控件经接口路径接收一次事件，删除重复的直接回调路径。

滚轮保留现有向祖先传播至被消费的行为。
指针捕获记录稳定的处理节点及组件身份，不只记录 `UIControl`；节点失效时执行取消并释放捕获。
指针按下／抬起的配对规则和控件点击判定保持不变，不能在新路由和控件内部各产生一次点击。
键盘焦点和自动导航仍由 `UIControl` 维护；输入接口本身不自动赋予组件可导航资格。
拖动起止、取消和多指针隔离沿用同一个路由状态机，避免每个处理器自行推断捕获状态。

新增 `IUIRaycastFilter.IsRaycastLocationValid(vector2 localPoint)`。
默认命中先通过呈现快照、裁剪与 `UIVisual.Raycast`，再检查图形及祖先的有效过滤器；坐标转换到各过滤器本地空间。
任一过滤器返回 false 即排除候选，继续检查下层图形；过滤器不能绕过既有遮罩与世界深度遮挡。
本次不增加多种 raycaster 插件框架；自定义形状继续通过 `UIVisual.Raycast` 与过滤器实现。

验收：普通 C# 脚本可给已有图片附加悬停提示、拖动行为和自定义命中过滤；原有按钮、滚动框与输入框不重复收事件。

## 8. 布局、遮罩与文字

保留 `IUILayoutMeasure`、`IUILayoutController`、`UILayoutGroup`、`UILayoutRegistry` 的职责和四阶段调度。
检查注册过程是否按接口收集用户组件；遇到内置类型白名单时改为接口发现，避免新增布局必须修改注册表。
保留水平先于垂直、测量自下而上、排列自上而下的规则。
增加一个游戏侧自定义布局示例，验证动态增删子项、父尺寸变化和禁用恢复即可，不另造布局系统。

遮罩关系、嵌套裁剪与命中过滤仍在 C#；原生继续执行 scissor 与软遮罩绘制。
材质扩展不得绕过遮罩；网格扩展不得破坏遮罩命令的压栈／出栈顺序。
不为了模仿 Unity 的 stencil 路径而替换已有 R8 软遮罩能力。

文字排版、TextField 编辑行为与字体模式全部保留。
继续调用原生字体服务，不迁移 FreeType／SDF／MSDF 算法，不更改现有字形缓存归属。
`Text` 和 TextField 附加几何需要沿用一致的材质提交协议；附加几何是否接受用户网格效果必须明确，不隐式重复执行。
本次规定：网格修改器作用于所在 Ens 的 `UIVisual`；控件附加网格保持控件自身管理。

## 9. 自定义图形与离屏依赖

修改位置：`UIWorldContext.CollectCanvasDependencies`、`UIFrameBuilder` 与绘制状态资源收集逻辑。
移除只通过 `Image.GetTexture()` 识别画布依赖的做法。
依赖来源改为重建后的最终片段纹理，以及有效材质的全部纹理引用；包含控件附加网格与遮罩使用的纹理。
先完成脏图形／材质重建并收集资源，再计算离屏拓扑顺序，最后提交画布。
这样自定义 `UIVisual` 与材质修改器引用离屏输出时，也能进入同一依赖图。

纹理或材质参数变化必须使依赖信息失效，即使没有几何变化也要重新收集。
材质资源修改通过资源版本或变更通知追踪，不依赖用户每次手工刷新画布。
禁止读取正在写入的同一渲染目标；自依赖和环依赖给出画布链诊断，并拒绝该环涉及画布的本帧提交。
环外无依赖画布继续执行，不通过任意调整顺序掩盖反馈环。

验收：自定义图形和自定义材质分别引用离屏画布输出，顺序正确；动态换纹理后下一帧生效。

## 10. 分步实施与交付

各阶段完成后保持可构建、可运行，不同时保留两套长期渲染或事件管线。

| 阶段 | 工作 | 阶段完成条件 |
|---|---|---|
| 1 | 固定现有功能基线，补网格修改器与基础／最终状态分离 | 自定义图形及两个组合效果通过，反复改色不累积 |
| 2 | 接入 Material、材质修改器与原生 ABI | 自定义 shader 在三种画布及软遮罩下正确 |
| 3 | 扩展离屏资源依赖收集 | 自定义图形／材质依赖正确，环依赖可诊断 |
| 4 | 增加输入处理接口、捕获目标与命中过滤器 | 普通脚本可接收输入，内置控件行为不回归 |
| 5 | 验证布局扩展、编辑器与热重载，清理特判 | 用户扩展不需要修改引擎源码 |

编辑器需识别用户派生图形／控件和新增修改器，展示序列化字段，允许添加组件并调整顺序。
材质引用、修改器字段和事件接收组件应支持保存、重开、复制及热重载；运行时缓存不序列化。
热重载与世界销毁时注销处理器和修改器缓存，释放运行时材质实例，取消捕获与 IME 会话。
保留现有控件类型与字段名；新增材质为空、没有修改器时使用现有默认行为。

清理范围限于被替代的路径：重复输入直调、Image 专属依赖收集、在最终状态上累积修改的逻辑。
`UIMaterialKind` 的内置模式、现有桥接服务与原生缓存均继续使用。
更新 `Docs/RetainedGUI.md` 的接口与边界描述，使其与完成后的实现一致；迁移完成前不要将本文件计划标记为已实现。
影响游戏工程的接口／ABI 改动按项目规范递增实际版本并记录升级项；生成绑定通过构建刷新，不手改 Generated。

## 11. 验收与停止条件

以游戏工程消费公开 API 的方式验证扩展，不把示例写进引擎内部来绕过访问限制。

- 扩展：自定义图形、组合网格效果、自定义 UI 材质、自定义布局、输入模块及独立输入处理脚本均可使用。
- 渲染：Overlay／WorldSpace／Offscreen、多相机文字、嵌套硬／软遮罩和透明顺序保持正确。
- 交互：鼠标、多指触摸、手柄导航、滚轮传播、捕获取消、中文 IME 和焦点切换保持正确。
- 状态：相同输入反复刷新不累积颜色／几何；修改器启停、材质替换、节点销毁和热重载无残留。
- 缓存：无脏标记时不重建网格、不重复上传；只有材质变化时不触发字体重排或几何重建。
- 资源：运行时材质、纹理与 GPU 缓存按原有寿命约定释放；离屏反馈环不提交非法读写。
- 协议：托管／原生结构布局验证一致，错误 ABI 版本明确失败。

实施时运行受影响的托管构建、原生构建及现有 UI 检查；针对上述跨边界行为补必要回归验证。
对比同一静态 UI 和动态 UI 场景的重建次数、上传量、批次数与分配量，解释新增扩展机制带来的差异。
全部扩展案例无需增加原生控件逻辑即可完成，即达到本次迁移目标。
此后不再以“还能搬到 C#”为理由继续移动底层渲染、资源或平台代码。


## 12. 实施记录（引擎侧）

### 阶段 1：网格修改器与状态分离

- 新增 `OrbedenCore/Managed/Orbeden.UI/Runtime/IUIMeshModifier.cs`：`ModifyMesh(UIMeshBuilder)`。
  挂在图形同 Ens 上、处于启用状态的组件按挂载顺序运行；顺序取组件挂载顺序，不做反射发现。
- `UIMeshBuilder` 新增 `GetVertex`/`SetVertex`、`TriangleCount`/`TryGetTriangle`/`GetTriangleState`、
  `ClearTriangles`。重建三角流时先 `SetDrawState` 给出首段状态，之后每换一段再设一次；
  重放按状态是否变化决定切段，片段顺序与状态不会被压平。
- `UIVisual.Rebuild` 的共享网格重建顺序固定为：清空 → `PopulateMesh` → `Complete` → 网格修改器 → 校验索引 →
  记版本。任一修改器失败即清空本次产物并按组件名报错。
- 基础／最终状态分离：片段里保存的始终是 `PopulateMesh` 产出的基础状态；
  最终状态由 `UIVisual.PrepareDrawStates` 按材质脏标记更新，命令发射只读取缓存。
  `UIVisual.SetMaterialDirty()` 标记颜色、材质和修改器参数变化；每次从基础状态重算，不累积乘色。
  几何与布局独立标脏；自定义图形改变期望尺寸时同时调用 `SetLayoutDirty()`。
- `IsGeometryInvalidated` 与 `ModifyDrawState` 改为 `protected internal` 扩展点，引擎控制调用时机。

### 阶段 2：材质与原生提交

- 新增 `IUIMaterialModifier.cs`：`ModifyMaterial(ref UIDrawState)`；`UIDrawState` 增加 `Material? material`，
  批次匹配把它算进身份。`UIVisual` 增加序列化材质引用与 `GetMaterial`/`SetMaterial`。
- 最终状态顺序：基础状态 → `ModifyDrawState` → 显式材质覆盖 → 材质修改器 → 颜色合成。
  修改器参数变化后调用目标图形的 `SetMaterialDirty()`；静态帧不重跑，动画效果更新后主动标脏。
- 提交协议：`UIDrawCommand` 增加 `materialObjectId`（0 表示内置 UI 材质）。
  ABI 层按标识核对对象类型并持有 `Ref<Material>` 直到该帧被替换；渲染器用材质的 shader 程序绘制，
  按名字绑定颜色与浮点槽，纹理槽从 2 起（0/1 保留给 UI 纹理与软遮罩覆盖率）。
  保留参数（`u_Model`、`u_ViewProjection`、`u_Texture`、`u_Tint`、`u_MaterialKind`、`u_DistanceRange`、
  `u_Coverage*`、`u_Viewport*`）由提交器写入，材质同名参数被跳过。
  材质缺失、类型不符或 shader 不可用时退回内置材质并按来源报一次诊断。
- 运行期实例：`Material::CopyFrom` 复制 shader、队列与全部槽位；托管侧用
  `Object.CreateInstance<Material>()` + `CopyFrom` 得到可改可释放的实例。
- 模板：`OrbedenEditor/Templates/Builtin/Shaders/ui_custom_template.orbshader`，覆盖三种画布模式与软遮罩，
  含灰度与溶解两个示例参数。

### 阶段 3：离屏资源依赖

- `CollectCanvasDependencies` 改为按最终绘制资源收集：最终片段状态的纹理、显式材质引用的全部纹理、
  控件附加网格的纹理与遮罩形状纹理；不再识别 `Image.GetTexture()`。
- 顺序：脏图形重建 → 更新脏最终状态 → 计算拓扑顺序 → 提交。资源依赖仍每帧读取，
  直接修改 Material 的纹理槽会在下一帧进入依赖图；修改器改变输出纹理时需调用 `SetMaterialDirty()`。
- 环依赖与自环仍按下标整环判定：环内画布清透明并输出画布链诊断，环外下游照常执行。

### 阶段 4：输入事件的组件组合

- 新增 `UIInputHandlers.cs`：`IUIPointerDown/Up/Move/Enter/Exit/CancelHandler`、`IUIScrollHandler`、
  `IUINavigationHandler`、`IUISubmitHandler`、`IUICancelHandler`、`IUIFocusHandler`、`IUIRaycastFilter`。
  `UIControl` 实现全部输入接口，其 `public virtual` 回调即接口实现，控件派生入口不变。
- `UIHandlerCache`（挂在 `UIWorldContext` 上，换世界清空）按组件集合版本失效，覆盖增删、重排、启停与字段事务，
  派发时按启用状态逐个判断，不逐事件扫描程序集。
- 事件从命中图形所在节点向祖先找第一个有实现者的节点：同一节点按组件顺序执行，
  回调里销毁或停用的组件在调用前跳过；找不到 `UIControl` 不再丢弃事件。
- 捕获记录处理节点（`EnsId`）与节点上的控件：`UIPointerState.downTarget/captureTarget` 改为
  `UIEventTarget`，`CancelObject(objectId)` 改为 `CancelNode(EnsId)`，调用方同步更新。
  拖动阈值、按下／抬起配对、点击判定与多指针隔离仍由同一个路由状态机负责。
- `IUIRaycastFilter.IsRaycastLocationValid` 在裁剪与 `UIVisual.Raycast` 之后检查图形与祖先，
  画布空间的点换算到各过滤器所在节点的局部空间，任一否决即排除该候选并继续看下层。

### 阶段 5：编辑器与清理

- 组件右键菜单增加 “Move Component Up/Down”，经新原生槽位 `moveComponent` 调 `Ens::MoveComponent`，
  保留组件身份；顺序决定修改器与输入处理器的执行次序。撤销按逆序回退。
- 用户派生图形／控件／修改器本来就是普通托管脚本：检视面板按序列化字段展示，
  “Add Component” 列表来自程序集扫描，无需为它们改引擎。
- 布局发现本就是接口路径（`UILayoutGroup` 与 `IUILayoutMeasure`/`IUILayoutController`），没有内置类型白名单。
- 清理：重复输入直调、Image 专属依赖收集、在最终状态上累积修改的逻辑、材质脏标记管线均已删除。

### ABI 变更记录

- `RetainedGuiApi`：新增槽 18 `ReadDisplaySize(width, height)`；槽 10 `ReadDepth` 增加 `viewerId` 参数。
- `UIView` 保留 `viewerId` 供多相机命中；`UICanvasSubmission` 不携带相机身份，尺寸 112 字节；`UIDrawCommand` 包含 `materialObjectId`。
  结构尺寸与字段偏移断言同步更新，托管镜像与 `AbiLayoutTests` 同步。
- 编辑器：`EditorGuiNativeApi` 增加 `setCursorScreenPos`（84→85）、`EditorGizmoApi` 增加 `projectPoint`（5→6）、
  新增 `EditorInputNativeApi`（键盘状态）、`EditorComponentNativeApi` 增加 `moveComponent`（24→25）；
  `EditorManagedApi` 总槽位 166→171，偏移断言逐项更新。
- RetainedGuiApi 主版本为 2，UI 包版本为 3；旧包需要同步并重编译。

### 待验收（游戏工程侧）

第 11 节的扩展案例需要以游戏工程消费公开 API 的方式执行，未写进引擎内部：
圆环图形、阴影与顶点渐变两个网格效果、溶解／灰度材质、自定义布局、独立输入处理脚本，
以及无脏标记不重建、只有材质变化不重排文字等缓存判据。
