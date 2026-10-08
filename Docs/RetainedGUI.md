# 需求/目标

本文定义 Orbeden RetainedGUI 的实现边界、公共接口、算法、编辑器接入、发布方式与验收。
UI 组件、布局、网格生成、文字排版、输入路由、控件状态与事件派发由 C# 实现。
C++ 负责 GPU 执行、字体解析与光栅化、平台输入、原生资源及通用引擎桥接。
设计遵守 Docs/ProjectConventions.md；坐标保持右手系，X 向右、Y 向上、Forward 为 -Z。

| 范围 | 确定内容 |
|---|---|
| 对象 | UI 节点为 Ens，具有 Transform、UILayout |
| 画布 | Canvas；Overlay、WorldSpace、Offscreen |
| 图形 | Image、Text；普通图片、九宫格 |
| 交互 | Button、CheckBox、RadioButton、Slider、ScrollBar、ScrollBox、ComboBox、TextField |
| 布局 | LayoutBox：水平/垂直；GridBox：固定列/固定行/自适应列 |
| 遮罩 | Mask：矩形、图片 Alpha、嵌套软遮罩 |
| 字体 | TTF、OTF、TTC；Bitmap、SDF、MSDF；动态 Atlas |
| 输入 | 鼠标、键盘、触摸、手柄；中文 IME、剪贴板 |
| 事件 | C# 订阅、Inspector 持久化绑定 |
| 编辑器 | 创建、属性、矩形手柄、像素预览、Undo/Redo |
| 发布 | Editor CLR 与 Player NativeAOT |

首版不实现富文本、复杂文字塑形、双向重排、字体回退、彩色 Emoji。
TextField 仅单行，不实现密码模式、屏幕键盘。
不实现图片平铺/填充、主题、数据绑定、动画框架、列表虚拟化或嵌套 Canvas。
平台输入落地到当前 Windows/GLFW；其他平台复用事件合同，不在本次交付范围。
全部上述交付功能属于同一验收范围，实施次序不代表功能可被省略。

**扩展验收**
- 继承 UIVisual 能用 C# 增加图形；继承 UIControl 能增加交互控件。
- 实现布局接口能增加布局；替换 UIInputModule 能改变输入策略。
- GUI 源码随 SDK 发布，可在游戏工程覆盖并编译。
- 修改控件、排版、布局与路由不运行 C++ 编译，不改变原生函数表。
- 自定义组件能挂载、保存、复制、Undo、热重载并运行于 NativeAOT。
- 暖缓存且画面未变时，布局/几何重建、字形生成与顶点上传计数均为零。
- 无 Camera 时 Overlay/Offscreen 正常；WorldSpace 使用场景深度遮挡。
- 事件回调删除源、目标或请求换世界时不访问失效对象。

UGUI 的借鉴点是托管图形生成、分阶段重建与原生渲染桥接，不复制 Unity 的底层类型。
参考： https://github.com/Unity-Technologies/uGUI/blob/main/com.unity.ugui/Runtime/UGUI/UI/Core/Graphic.cs
参考： https://github.com/Unity-Technologies/uGUI/blob/main/com.unity.ugui/Runtime/UGUI/UI/Core/CanvasUpdateRegistry.cs
参考： https://github.com/Unity-Technologies/uGUI/blob/main/com.unity.ugui/Runtime/UGUI/UI/Core/Layout/LayoutRebuilder.cs

# 项目现状

以下为本次读取代码核实的基线；行号用于定位，实施时同时核对符号。
路径缩写只用于本文；每个新增主类型与文件同名，紧密关联的 POD/枚举放在对应主类型文件。

| 缩写 | 目录 |
|---|---|
| CoreCS | OrbedenCore/Managed/OrbedenCore.CSharp/ |
| Package | OrbedenCore/Managed/Orbeden.UI/ |
| UI | Package/Runtime/ |
| UIEditor | Package/Editor/ |
| Native | OrbedenCore/Src/ |
| EditorCS | OrbedenEditor/Managed/Orbeden.Editor/ |

| 已核实事实 | 源码位置 | 设计处理 |
|---|---|---|
| Script 绑定原生 Script 宿主 | CoreCS/Script.cs:10 | UI 复用宿主 |
| Ens 区分原生/托管工厂 | CoreCS/Ens.cs:213 | 沿用 AddComponent<T> |
| 已有依赖与单实例属性 | CoreCS/InspectorAttributes.cs:3 | 补齐统一验证 |
| 生命周期扫描拒绝 virtual | CoreCS/ScriptRuntime.cs:401 | UI 使用独立接口 |
| 宿主创建时立即应用字段 | CoreCS/ScriptRuntime.cs:290 | 改为全体构造后恢复引用 |
| 已有一维数组/List 持久化 | CoreCS/ManagedScriptHostFields.cs:58 | 复用集合编码 |
| 托管类型分类缺少 vector2 | CoreCS/ManagedScriptInterop.cs:57 | 补齐值协议 |
| 原生字段分类缺少 vector2 | Native/Runtime/Object/Script.cpp:261 | 同步分类 |
| 编辑初始化创建临时包装 | CoreCS/Script.Native.cs:214 | 编辑世界保持包装 |
| Inspector 自有加载上下文 | EditorCS/Panels/InspectorPanel.cs:13 | 统一程序集会话 |
| ScriptRuntime 自有加载上下文 | CoreCS/ScriptRuntime.cs:19 | 统一程序集会话 |
| Update 受模拟/暂停门控 | Native/Application.cpp:321 | UI 独立帧阶段 |
| Transform setter 通知 World | Native/Runtime/Object/Transform.cpp:14 | 增加派生位置 |
| World 有代次和脏抑制 | Native/Runtime/World.h:115 | 绑定上下文代次 |
| 输入目前为状态查询 | Native/InputManager/InputManager.h:70 | 增加有序事件 |
| RenderSystem 有无相机分支 | Native/Rendering/RenderSystem.cpp:387 | 接入屏幕 UI |
| Texture2D 持有 CPU 像素 | Native/Runtime/Object/Texture2D.h:22 | 扩展动态/目标纹理 |
| 组件编辑器统一编辑目标 | EditorCS/ComponentEditor.cs:6 | 复用编辑框架 |
| 属性文档负责事务和历史 | EditorCS/PropertyDocument.cs:158 | UI 修改走事务 |
| 游戏源码集中加入 Compile | Tools/OrbedenMetaGen/Orbeden.Bindings.targets:35 | 接入源码包 |
| AOT 导出编进游戏主程序集 | OrbedenEditor/Templates/Shared/GameAotExports.cs:6 | 同步新增阶段 |
| 当前项目版本为 36 | Native/Defines/Version.h:8 | 本次升级为 37 |

Docs/ScriptSystem.md 的集合字段说明与当前实现不一致，本次同步更新。
当前尚未具备下述 RetainedGUI 系统，路线图均保持未完成。

# 总体路线图

- [x] 托管组件基础系统更新
- [x] GUI源码包与程序集系统落地
- [x] UI对象与帧调度系统落地
- [x] 矩形与自动布局系统落地
- [x] 图形与原生提交系统落地
- [x] 字体与文字系统落地
- [x] 裁剪与渲染系统落地
- [x] 输入与事件系统落地
- [x] 控件系统落地
- [x] UI编辑器系统落地
- [ ] 发布与验收系统落地

# 关于托管组件基础系统

所有 UI 组件继承 Orbeden.Script，每实例对应独立原生 Script 宿主。
原生宿主只保存身份、managedTypeName、enabled 与字段快照，不保存控件状态机。
示例：同一 Ens 上为 Transform、Script→UILayout、Script→Image、Script→Button。
场景仍使用 Component type="Script" 与完整 managedTypeName，例如 Orbeden.Button。
稳定组件路径用于持久化引用；ObjectId、包装指针与数组下标只用于当前进程。

**两阶段实例构造**
1. 构造全部可解析宿主并登记包装；构造函数仅初始化字段，不调用扩展回调。
2. 应用字段并解析引用，随后统一附着和发送初始活动通知。
循环组件引用在步骤二通过已登记包装解决，不递归构造对端。
找不到托管类型时保留宿主和原字段，显示 Missing Script；不删除数据。
单个构造失败断开该实例；引用它的字段保留未解析路径，不阻塞其他实例。

新增 CoreCS/IManagedComponentLifecycle.cs：
```csharp
public interface IManagedComponentLifecycle
{
    void OnComponentAttached();
    void OnComponentActiveChanged(bool active);
    void OnComponentFieldsChanged();
    void OnComponentDetached();
}
```
ScriptRuntime 直接调用接口，不使用按方法名扫描。
普通游戏生命周期保留现有语义；编辑模式不执行 OnStart/Update/FixedUpdate/LateUpdate/DrawGUI/End。
接口附着在引用恢复后执行；销毁前先发送 active=false，再发送 Detached，均至多一次。
活动条件为 enabled && Ens.WorldActive；UI 另检查 Canvas、层级和配置有效性。

**持久化与配置权威**
GUI 配置使用 [SerializeField] private 字段；缓存保持 private 且不标注。
每个配置 T x 自动对应 public T GetX()、public void SetX(T value)；本文不重复列出访问器。
集合仅开放本文明确列出的操作方法，不返回可写 List。
Setter 先校验、再比较、再赋值和标脏；相同值不产生事件或重建。
运行时权威值在 C#；原生字段表是保存、复制与编辑事务使用的快照。
保存、复制、Prefab、进入 Play、程序集重载前调用 FlushHostFields。
普通 setter 不逐次调用原生字段写入；派生矩形、选区、捕获、网格、委托不持久化。
Inspector 事务先转换全部值，成功后一起应用，最后调用一次 FieldsChanged。
转换失败整批拒绝；验证失败回滚该事务，不保存部分值。
加载配置非法时保留原数据，标记该组件不可运行并显示错误；用户修复后重新验证。
未解析引用单独记录字段/元素对应的稳定路径；未被明确编辑为 null 前不能刷新成空路径。

新增 ComponentConstraintAttribute(Type exclusiveBaseType)，Inherited=true、AllowMultiple=true。
同一 Ens 最多一个可赋值给 exclusiveBaseType 的组件；检查范围包括 C# 派生类型。
UniqueComponent 用于 UILayout、Canvas、Mask；家族互斥用于 UIVisual、UIControl、UILayoutGroup。
用户添加时补齐依赖，失败逆序撤销新增组件；场景加载只验证、不插入依赖。
单删被依赖组件被拒绝；整 Ens 销毁放行；Undo/复制恢复在整批对象建完后验证。
字段协议追加 vector2，不移动已有枚举值；标量序列化名称为 vector2，值为 invariant 的 x y。
一维集合继续采用已有 UTF-8 字节长度前缀协议；不增加任意托管对象图序列化。

# 关于GUI源码包与程序集系统

Package/Runtime 编译进游戏主程序集，Package/Editor 编译进 <Game>.Editor。
包内类型使用 Orbeden 命名空间；编辑器类型使用 OrbedenEditor。
CoreCS 只提供合同和桥接，不引用游戏程序集中的具体 UI 类型。
源码发布为 Sdk/Packages/Orbeden.UI/{Runtime,Editor,Orbeden.UI.targets,Package.version}。
游戏项目覆盖位置固定为 <Project>/Packages/Orbeden.UI/，存在时整包覆盖 SDK 来源。
OrbedenUIPackageRoot 指向唯一来源；禁止把 SDK 与项目包同时加入 Compile。
Package.version 首行为 3，声明所需 RetainedGuiApi 主版本 2；不匹配则构建失败。
项目覆盖包不被 SDK 刷新覆盖；普通用户扩展放 Content/，框架修改放覆盖包。
Runtime 不引用 Orbeden.Editor；Editor 源码不进入 Player/AOT。
Core 的 MetaGen 只处理底层原生能力；新增 UI C# 类型不参与原生反射生成。

新增 CoreCS/ManagedAssemblySession.cs：
```csharp
public static Assembly? GetGameAssembly();
public static Assembly? GetEditorAssembly();
public static ulong GetGeneration();
public static bool Load(string gameAssemblyPath, Assembly? editorContract);
public static void Unload();
```
该静态类持有唯一可回收 GameLoadContext，按简单程序集名缓存依赖。
共享 OrbedenCore.CSharp 和传入的 Editor 合同程序集；其余依赖从游戏输出解析。
游戏/编辑扩展使用流加载，释放文件锁；模块初始化在创建组件前完成。
同一会话内反复 Load 相同路径只返回成功，不重复注册模块；路径变化必须先分离世界。
Inspector、CustomEditor 和运行时使用同一游戏 Type 实例，不各加载一份游戏程序集。
进入/退出 Play 更换世界实例，不重新加载程序集；一次只附着一个活动世界上下文。
退出 Play 恢复编辑快照并重建编辑包装，运行期间的控件值不写回编辑内容。
Load 只允许在世界已分离后替换会话；编译失败不调用 Load，继续使用当前会话。
加载失败保留场景字段并显示错误；不尝试运行部分注册成功的程序集。
卸载顺序：停止帧→取消输入/IME→释放 UI 上下文→断开包装→清理编辑缓存/菜单→注销绑定→Unload。
所有保存 Type、delegate、Component、Editor 实例的静态表必须在卸载路径清空。

# 关于UI对象与帧调度系统

类型关系：
```text
Script → UIElement
  ├─ UILayout、Canvas、Mask
  ├─ UILayoutGroup → LayoutBox、GridBox
  ├─ UIVisual → Image、Text
  └─ UIControl → Button、CheckBox、RadioButton、Slider、ScrollBar、ScrollBox、ComboBox、TextField
```
四个中间基类为 abstract；具体控件可继承；构造函数统一 public X(Ens ens)。
基类构造为 protected；UIElement 显式实现 IManagedComponentLifecycle。
除 UILayout 自身外，具体 UI 类型依赖 UILayout；Image 与 Button 可以同节点存在。
UI 树内部不跨越缺少 UILayout 的节点；Canvas 的外部父节点允许是普通 Ens。
无 Canvas 的 UI 保留数据但不绘制；嵌套 Canvas 子树报错并停止提交。
UI Ens 禁止 static；加载非法 static UI 不自动改数据，显示配置错误。

UIElement 扩展接口：
```csharp
protected virtual void OnUIAttached();
protected virtual void OnUIEnabled();
protected virtual void OnUIDisabled();
protected virtual void OnUIValidate();
protected virtual void OnUIDetached();
public bool IsUIActive();
public UILayout? GetLayout();
public Canvas? GetCanvas();
```
接口实现先维护注册表，再调用扩展方法；扩展不必调用 base 才能完成核心注册。
异常记录组件路径与方法；当前组件停止该帧操作，其他组件继续。
卸载先撤销输入与缓存引用，再执行 OnUIDetached。

新增 CoreCS/IManagedFrameSystem.cs：
```csharp
void AttachWorld(ulong worldRevision, bool editorMode);
void ProcessInput(float deltaTime);
void PrepareRender(float deltaTime);
void DetachWorld();
```
ManagedFrameSystems.Register(string id, Func<IManagedFrameSystem> factory) 登记工厂。
Unregister(string id) 仅在世界分离后执行；重复 id 报错，顺序为 id 的 ordinal 升序。
UIRuntimeBootstrap 通过模块初始化注册 id="Orbeden.RetainedGUI"。
UIWorldContext 实现接口；核心保留接口，不硬编码 UIWorldContext 类型。

UIWorldContext 状态：
| 字段 | 类型/含义 |
|---|---|
| worldRevision、managedGeneration | ulong；世界与会话代次 |
| nativeContext | ulong；原生执行上下文 |
| nodes | Dictionary<EnsId,UINode> |
| canvases | List<Canvas> |
| layoutRegistry、graphicRegistry | 布局与图形脏集合 |
| inputRouter、eventDispatcher | 输入与事件队列 |
| fontAtlas、frameBuilder | 字形缓存与帧构建 |

UINode 为 Ens 的托管索引，保存父子身份、UILayout、UIVisual、UIControl、Mask、UILayoutGroup、Canvas。
不提供第二套 reparent API；World 结构/Transform 通知使受影响索引失效。
初始附着批量读取树；后续按变更记录更新，不每帧扫描全部原生组件。
结构记录类型为 Added、Removed、Reparented、ActiveChanged、TransformChanged、FieldsChanged。
每条记录带世界代次、序号与 ObjectId/EnsId；队列溢出设置 FullResync，下一边界重建整个索引。

帧顺序：平台采集→UI 输入/事件→游戏 Fixed/Update/Late→UI 布局/图形/提交→渲染。
UI 输入与 PrepareRender 不受暂停或 simulationEnabled 门控，使用非缩放 deltaTime。
编辑模式执行结构同步、布局与渲染，不执行交互路由和游戏事件。
UI 预览在 Play 中可交互；暂停 Play 中菜单仍可响应。
任何用户回调都在 ScriptSystem 的延迟删除保护区内执行。
换世界请求在阶段结束执行；代次改变立即丢弃剩余事件和提交。
零尺寸视图清空命中快照且停止该视图布局输出；无 Camera 不阻止屏幕画布。

# 关于矩形与自动布局系统

UILayout 配置：
| 字段 | 默认值 |
|---|---|
| anchorMin、anchorMax、pivot : vector2 | 均为 {0.5,0.5} |
| offset : vector2 | {0,0} |
| sizeDelta : vector2 | {100,30} |
| fitWidth、fitHeight、ignoreLayout : bool | false |

UIRect 为 readonly struct，含 vector2 min、size；构造 UIRect(vector2 min, vector2 size)。
Contains(vector2 point) 左/下包含，右/上不包含；零尺寸不命中。
父矩形 Pmin/Psize；逐分量计算：
```text
Amin = Pmin + Psize * anchorMin
Amax = Pmin + Psize * anchorMax
S = max(0, Amax - Amin + sizeDelta)
Q = Amin + (Amax - Amin) * pivot + offset
rect.min = -pivot * S
rect.size = S
```
anchor 与 pivot 限于 [0,1]；写 min 不超过当前 max，写 max 不小于当前 min。
sizeDelta 可为负；非有限浮点写入拒绝。
pivot setter 不补偿位置；编辑器“保持矩形”操作一次修改 pivot 与 offset。

Transform 作者位置与布局位置分开：最终 XY=Q+作者 localPosition.xy，Z 使用作者 localPosition.z。
屏幕画布的直接子节点还要叠加画布根矩形的枢轴居中偏移（−pivot×根矩形尺寸）：根逻辑矩形从 {0,0} 起算，
而画布对象落在枢轴上，这段偏移与场景预览矩阵在画布原点上的居中是同一个值，两边因此逐点重合。
WorldSpace 根矩形已按枢轴解析，不叠加。
容器驱动矩形覆盖自身锚点解析结果；当前实现仍叠加作者 localPosition，驱动结果不覆盖原配置。
原生 Transform 增加非持久化派生位置覆盖，世界矩阵使用覆盖值；原 GetLocalPosition 仍返回作者值。
退出 UI、禁用、销毁与域卸载时清除覆盖，恢复作者值。
C# 一次批量写派生位置，原生使用 DirtySuppressionScope；通知包含派生写标志，避免反馈重建。
驱动优先级固定为父布局组→控件内部驱动→自身 fit→自身锚点；同轴两个同级 owner 视为配置错误。
父组对子节点的排列为外层驱动；控件只驱动其明确引用的内部子节点。
UI 节点的旋转/缩放影响渲染与命中，不用于计算布局期望包围盒。
布局与普通子 Ens 共用解析后的 Transform 世界矩阵。

编辑规则（版本 51）：SceneView 的 Rect 工具（T）修改 UILayout 的 offset 与 sizeDelta；Move/Rotate/Scale（W/E/R）修改 Transform。普通 UI 的最终位置是“父矩形中的锚点落点 + UILayout.position + Transform.localPosition”。未拉伸时 Width/Height 等于 sizeDelta；拉伸时矩形尺寸等于锚点跨度加 sizeDelta，Inspector 显示相应边距。localScale 在布局完成后缩放网格、文字及子树，不修改 Width/Height，也不会让文字按缩放后的宽度重新换行。常规排版保持 localPosition=(0,0,0)、localScale=(1,1,1)，用 Rect 调位置与尺寸；额外位移或缩放动画使用 Transform。父布局组驱动的子矩形应修改布局组或设置 ignoreLayout。Overlay 根 Canvas 的 Transform 只参与 SceneView 空间预览，PIE 的大小由 Canvas 缩放配置与视口决定；WorldSpace 根 Canvas 的 Transform 则决定真实世界位置和尺寸。

预览空间（版本 53）：Overlay 画布在场景中的大小由**画布根 Transform 的缩放**给出，新建画布写 0.01，与 WorldSpace 同刻度；预览矩阵只再按枢轴居中一次，不再另乘缩放。逻辑单位到场景世界单位的换算因此只有一个来源，UI 子节点的世界矩阵就等于它在场景里的绘制位置——W/E/R 手柄、双击聚焦（F）、选择包围盒与绘制三者对齐。居中偏移同时写进派生位置（见上），两边的偏移同源。旧项目的 Overlay 画布根缩放是 1，需在画布根上手工设为 0.01。根逻辑矩形仍为 min={0,0}、size=logicalSize，屏幕正交投影与字形光栅缩放仍不读根 Transform。

默认字体（版本 51）：Text 与 TextField 的 Font 字段为空时，实际排版使用 `Builtin/Fonts/Default.otf`（Noto Sans SC Regular）。组件字段仍保留空值，默认资源按世界延迟加载并缓存；显式字体优先，输入框正文、占位文字、组合文字共用该规则。字体及 OFL 许可证随 Builtin 内容发布；旧项目需要同步新增 Fonts 目录并导入，Player cook 会收录字体资源。

场景编辑（版本 52）：编辑器加载游戏程序集前检查 UI 包与运行时/Editor DLL 的更新时间，源码包更新后自动构建并重载；共享手柄入口也限制 UILayout 只能在 Rect 模式交互。活动 Canvas 边框常显，子 UI 边框只在选中时显示。点击场景时，源码包场景扩展按 Image/Text 的实际预览变换求射线和平面交点，再检查解析矩形与祖先遮罩；运行时 RaycastTarget 不参与编辑器选择。先过滤被更近 MeshRenderer 遮挡的 UI，再按 Ens 树深度优先选取更“叶子”的节点，同层级比较相机距离。离屏画布显示其空间边框，实际离屏图元不参与 SceneView 拾取。

Canvas 枚举：CanvasRenderMode={Overlay=0,WorldSpace=1,Offscreen=2}。
CanvasScaleMode={ConstantPixel=0,ReferenceResolution=1}。
配置：renderMode=Overlay、scaleMode=ConstantPixel、referenceResolution={1920,1080}。
配置：matchWidthOrHeight=0.5、scaleFactor=1、outputSize={512,512}、sortOrder=0、drawLayer=1。
drawLayer 为位掩码：WorldSpace 与相机 drawLayerMask 按位与后非零才绘制；0 表示不绘制，1 为默认层。
scaleFactor≥0.01；参考分辨率各轴≥1；match 在 [0,1]；outputSize 各轴取整数且≥1。
```text
ConstantPixel: scale = scaleFactor
ReferenceResolution:
  scale = scaleFactor * exp2((1-match)*log2(width/ref.x) + match*log2(height/ref.y))
logicalSize = targetPixelSize / scale
```
Overlay/Offscreen 根 Transform 不参与屏幕投影；根逻辑矩形 min={0,0}、size=logicalSize。
Overlay 根 Transform 的缩放决定画布在场景中的大小，新建为 0.01（与 WorldSpace 同刻度）。
WorldSpace 根矩形由根 sizeDelta/pivot 解析，使用真实世界矩阵，不执行分辨率缩放。
新建 WorldSpace Canvas 为 800×600，Transform 缩放 {0.01,0.01,0.01}。

布局接口：
```csharp
public interface IUILayoutMeasure
{
    float MeasureWidth();
    float MeasureHeight(float availableWidth);
}
public interface IUILayoutController
{
    void ArrangeHorizontal();
    void ArrangeVertical();
}
```
执行顺序：自底向上测宽→自顶向下排 X/宽→自底向上测高→自顶向下排 Y/高。
同节点容器测量优先于图形；无来源用 max(sizeDelta,0)；父驱动轴优先于 fit。
UILayoutGroup 是 UIElement 的抽象派生并实现两个接口，缓存有效直接子节点。
有效子节点为活动且 ignoreLayout=false；重建中不调用 GetComponents 分配临时数组。

LayoutBox：orientation={Vertical=0,Horizontal=1}，默认 Vertical。
spacing 与 paddingLeft/Right/Top/Bottom 为 float，默认 0，最小 0。
crossAlignment={Start=0,Center=1,End=2,Stretch=3}，默认 Stretch。
主轴尺寸为子期望尺寸之和+max(N-1,0)*spacing+padding，交叉轴取最大期望尺寸+padding。
Horizontal 从左向右，Vertical 从上向下；主轴不压缩，不按权重分配。
交叉轴可用长度夹紧为 0；Start/Center/End 使用起点/中心/终点，Stretch 使用全长。

GridBox：constraint={FixedColumns=0,FixedRows=1,AutoColumns=2}，默认 FixedColumns。
constraintCount:int=1；cellSize={100,100}；spacing={0,0}；四向 padding=0。
constraintCount≥1，cellSize 各轴≥1，spacing/padding≥0。
FixedColumns：columns=count，rows=ceil(N/columns)，column=i%columns，row=i/columns。
FixedRows：rows=count，columns=ceil(N/rows)，row=i%rows，column=i/rows。
AutoColumns：columns=max(1,floor((availableWidth+spacing.x)/(cellSize.x+spacing.x)))。
AutoColumns 后续使用 FixedColumns 索引；禁止 fitWidth，错误配置停止本组驱动。
N=0 时期望尺寸仅含 padding；单元格从左上开始。
组驱动的尺寸/位置保存在解析缓存，不修改子节点锚点。

UIDirtyFlags 为位枚举：Hierarchy=1、Layout=2、Transform=4、Geometry=8、Material=16、Clip=32、Input=64。
布局脏请求提升至受尺寸依赖影响的最高根，祖先入队时移除后代请求。
每帧最多在输入后和渲染前各执行一次布局刷新；重建期间产生的新请求排到下一刷新。
不迭代寻找固定点；禁止 AutoColumns-fitWidth 自依赖，测量只依赖已确定的上阶段数据。
布局尺寸变更标记该图形几何和子布局；仅颜色变化不标记 Layout。

# 关于图形与原生提交系统

UIVisual 配置为 tint:color={1,1,1,1}、raycastTarget:bool=true。
Text 创建菜单将 raycastTarget 设为 false；Image 默认阻挡指针。
接口：
```csharp
protected UIVisual(Ens ens);
public void SetVerticesDirty();
public void SetMaterialDirty();
public virtual bool Raycast(vector2 localPoint);
protected abstract void PopulateMesh(UIMeshBuilder mesh);
protected virtual void ModifyDrawState(ref UIDrawState state);
public virtual float MeasureWidth();
public virtual float MeasureHeight(float availableWidth);
```
默认 Raycast 判定解析矩形；材质/几何缓存分别标脏；不在每帧调用 PopulateMesh。

实现约定（2026-10-04）：`SetVerticesDirty()` 只刷新几何与命中，自定义图形改变期望尺寸时另调 `SetLayoutDirty()`；内置 Text 内容与字号变化已同时标记布局。`SetMaterialDirty()` 只刷新最终绘制状态，修改器参数和动画变化后主动调用；设置图形颜色、材质或 Image 纹理会自动标记。最终状态始终从基础片段重算，静态帧复用；Material 纹理槽依赖仍每帧读取。组件扩展缓存使用集合版本，覆盖同数量替换、重排与启停；图形重建期间产生的新脏请求保留到下一轮。
派生图形可重写 Raycast 与网格生成；生命周期注册仍由 UIElement 实现。

UIMeshBuilder 为可复用 class，方法：
```csharp
public void Clear();
public int AddVertex(vector3 position, vector2 uv, color tint);
public void AddTriangle(int a, int b, int c);
public void AddQuad(UIRect rect, vector2 uvMin, vector2 uvMax, color tint);
public void SetTexture(Texture2D? texture, UIMaterialKind kind);
```
SetTexture 结束当前片段并开始新片段；同状态空片段不输出，Text 可跨 Atlas 页。
索引使用 uint32；每次 AddTriangle 验证范围；四边形从 +Z 观察为 CCW。
UIVertex={vector3 position,vector2 uv,color tint}，偏移 0/12/20，stride=36。
UIMaterialKind={ImageStraight=0,ImagePremultiplied=1,Bitmap=2,SDF=3,MSDF=4}。
UIDrawState 含 Texture2D? texture、UIMaterialKind materialKind、color tint、float distanceRange。
distanceRange 默认 4，仅距离场材质使用；纹理颜色空间由资源决定。

Image 配置：texture=null、mode={Simple=0,NineSlice=1} 默认 Simple。
uvMin={0,0}、uvMax={1,1}；四向 border 为 float，默认 0。
Simple 生成 4 顶点/6 索引，不自动保宽高比；空纹理使用引擎白纹理。
NineSlice 生成 16 顶点/54 索引；border 单位为所选源区域像素。
源 border 总和超尺寸时按比例缩小；目标小于 border 总和时再次同比缩小目标边框。
UV 使用已夹紧的源边框；中心宽高不小于 0，不生成翻转四边形。
UV 原点为左下；图片存储朝向在纹理上传/采样合同处统一。
Image 测量尺寸为选定源区域像素尺寸；空纹理使用 UILayout.sizeDelta。

UIFrameBuilder 完全在 C# 中构建画布顺序、网格片段、裁剪命令和命中记录。
Canvas 按 sortOrder 升序，同值按稳定组件路径 ordinal 排序。
Canvas 内为 Ens 前序遍历，父图形先于子图形；弹层在普通内容之后。
只合并相邻且目标、画布、纹理、材质、distanceRange、裁剪栈、深度模式相同的片段。
C++ 只执行命令，不遍历控件树，不重新排布局或重新决定批次。
图形缓存键为上下文代次+组件 ObjectId+片段编号；所有摄像机共用网格，网格变化推进 revision。
删除图形提交网格删除；无变化图形只在帧命令中引用 meshId。
原生绘制实例变化不要求重新上传局部顶点，变换矩阵独立提交。

**跨语言数据合同**
数据定义在 CoreCS/RetainedGuiTypes.cs 与 Native/Runtime/Gui/RetainedGuiTypes.h。
以下按声明顺序布局，Pack=8；C# bool 不进入 ABI，枚举存 uint32。
C# int/uint/ulong/float 对应 C++ int32/uint32/uint64/float32。
vector2/3/color/matrix4x4 使用引擎现有连续 float 布局。
所有结构计算 sizeof/offsetof 并在 C++ static_assert 与 C# 初始化验证。
| 结构 | 按顺序的字段 |
|---|---|
| UIMeshUpdate | ulong meshId,revision; uint vertexOffset,vertexCount,indexOffset,indexCount |
| UIDrawCommand | ulong meshId; int textureObjectId; uint materialKind,matrixIndex,commandKind,firstIndex,indexCount; float distanceRange,threshold; color tint; ulong materialObjectId |
| UICanvasSubmission | ulong frameId,canvasId,viewId; int outputTextureObjectId; uint renderMode; int sortOrder; uint drawLayer; int width,height; matrix4x4 viewProjection |
| UIDerivedPosition | EnsId ens; vector3 position; uint clear |
| UIView | ulong viewId,presentedFrame,viewerId; int width,height; vector2 logicalOrigin,logicalSize; matrix4x4 view,projection; uint flags |
| UIInputRecord | ulong sequence,textSession; double timestamp; uint windowId,pointerId,kind,device,key,modifiers; vector2 position,delta; float value; uint textOffset,textLength,caretScalar |
| UIGlyphRequest | int fontObjectId; uint scalar,glyphIndex,rasterMode,pixelSize; ulong fontRevision |
| UIGlyphResult | uint glyphIndex; float advance,bearingX,bearingY,width,height; int bitmapWidth,bitmapHeight,channels,rowStride; float originX,originY; uint byteOffset,byteCount |

UIMeshUpdate 为 32 字节；UIDrawCommand 为 64 字节；UICanvasSubmission 为 112 字节，viewProjection 偏移 48；UIVertex 为 36 字节。
commandKind={Draw=0,PushRectangle=1,PushImageAlpha=2,Pop=3}。
裁剪命令用 meshId/matrixIndex 表示形状，纹理/threshold 表示 Alpha 来源与阈值。
Pop 的其余字段清零；矩阵使用每画布 matrix4x4 数组中的索引。
UIView.flags：1=主显示目标、2=编辑预览、4=WorldSpace 相机、8=有效呈现；位可组合。
UIView.viewerId：这条呈现视图属于哪台相机（Ens ID），屏幕与离屏为 0，仅用于命中与深度读取。
UICanvasSubmission 不指定相机；WorldSpace 每块画布只提交一次，任意 LayerMask 匹配的相机均可绘制。
用于屏幕画布的视图使用单位 view 与正交 projection。

RetainedGuiApi 表头为 uint version=2、uint structSize；随后指针槽顺序固定：
| 槽 | C# 薄层方法合同 |
|---|---|
| 0 | ulong CreateContext(ulong worldRevision, ulong managedGeneration) |
| 1 | void DestroyContext(ulong context) |
| 2 | bool UpdateMeshes(ulong context, ReadOnlySpan<UIMeshUpdate> updates, ReadOnlySpan<UIVertex> vertices, ReadOnlySpan<uint> indices) |
| 3 | bool RemoveMeshes(ulong context, ReadOnlySpan<ulong> meshIds) |
| 4 | bool SubmitCanvas(ulong context, in UICanvasSubmission canvas, ReadOnlySpan<UIDrawCommand> commands, ReadOnlySpan<matrix4x4> matrices) |
| 5 | bool EndFrame(ulong context, ulong frameId) |
| 6 | int ReadViews(ulong context, Span<UIView> output) |
| 7 | int ReadInput(ulong context, Span<UIInputRecord> output, Span<byte> text, out int textBytes) |
| 8 | void ConsumeInput(ulong context, ReadOnlySpan<ulong> sequences) |
| 9 | bool ApplyDerivedPositions(ulong context, ReadOnlySpan<UIDerivedPosition> positions) |
| 10 | bool ReadDepth(ulong context, ulong viewId, ulong viewerId, ulong presentedFrame, int x, int y, out float depth) |
| 11 | bool QueryGlyphs(ulong context, ReadOnlySpan<UIGlyphRequest> requests, Span<UIGlyphResult> output) |
| 12 | int RasterizeGlyphs(ulong context, ReadOnlySpan<UIGlyphRequest> requests, Span<UIGlyphResult> output, Span<byte> pixels) |
| 13 | float GetKerning(int fontObjectId, uint leftGlyph, uint rightGlyph) |
| 14 | void SetTextInput(ulong context, ulong token, bool active, int x, int y, int width, int height) |
| 15 | int ReadClipboard(Span<byte> output) |
| 16 | bool WriteClipboard(ReadOnlySpan<byte> text) |
| 17 | int ReadChanges(ulong context, Span<UISceneChange> output, out bool fullResync) |
| 18 | bool ReadDisplaySize(out int width, out int height) |

深度来源按"画布 + 观察者"分槽：世界空间下同一块画布被多台相机看到时各存各的目标与深度。
ReadDisplaySize 给的是主显示目标的像素尺寸，屏幕画布的首帧视口引导用它：
渲染器每帧在绘制屏幕画布前报一次，托管侧据此给还没有视图的画布补上视口。
UISceneChange={ulong sequence,worldRevision; EnsId ens,parent; int objectId; uint kind,flags}。
ReadChanges 空缓冲只查询数量；复制成功才确认排空；fullResync 表示必须重新枚举。
底层 C ABI 将每个 Span 展开为指针+int32 数量，in/out 展开为结构指针。
底层 bool 返回和参数用 uint8，out bool 用 uint8*；所有函数为 Cdecl。
ReadViews 返回所需条数，容量不足不部分写；视图不被读取操作删除。
ReadInput 返回所需条数并写所需 textBytes；容量不足不部分写、不消费事件。
RasterizeGlyphs 返回所需字节数，输出容量不足不发布像素；请求结果缓存到本次查询完成。
ReadClipboard 返回所需 UTF-8 字节数，容量不足不部分写；字符串不含终止零。
负返回值代表错误，薄层抛含操作名的异常；原生入口捕获所有 C++ 异常。
非法 context/世界代次拒绝调用；指针只在当前调用期间有效，原生不保存托管地址。

两套根 API 尾部追加 GetRetainedGuiApi 指针，根 abiVersion=3。
Engine 指针槽总数 71，Game 总数 112；原有槽位不移动。
（托管基础阶段给 EnsBind 追加了 GetDontSave/SetDontSave 两个槽，排在它后面的表各后移 2 位；
这两处槽位是本阶段唯一的 ABI 变更，实施 RetainedGui 表时按当前值起算。）
GetRetainedGuiApi 返回进程期稳定只读表；原生 UI context 生命周期独立。
SubmitCanvas 暂存本帧，EndFrame 原子发布；长度、索引、矩阵、裁剪栈全部校验成功才交换。
提交失败停止该上下文当前帧绘制并清命中快照，不显示半帧。
原生复制上传数据后返回；mesh 更新才上传顶点；无变化网格不重复上传。
原生帧持有资源 Ref；收集器把 context 的 Ref 清单作为资源根。
字体批量请求期间同样固定 Font；页面纹理由托管缓存登记资源根，回收时解除，不能依赖 C# 包装被 GC 来保活。
DestroyContext 释放帧、网格、派生位置、视图快照和资源根，重复销毁无副作用。
首版在主线程提交与渲染；GPU 删除依托后端资源释放，不从终结器执行 GL 调用。

# 关于字体与文字系统

分工：Font/FontRasterizer 用 C++，UITextLayout/FontAtlasCache/Text 用 C#。
原生职责为字体字节、字体面、glyphIndex、度量、kerning、轮廓光栅化；不执行换行或 Atlas 装箱。
Font 位于 Native/Runtime/Object/Font.h/.cpp，其余字体服务位于 Native/Runtime/Fonts/。
依赖固定 FreeType VER-2-14-3、msdfgen v1.13；保留 FTL 与 MIT 授权文件。
https://github.com/freetype/freetype/releases/tag/VER-2-14-3
https://github.com/Chlumsky/msdfgen/releases/tag/v1.13
FreeType 关闭外部 ZLIB/BZIP2/PNG/HarfBuzz/Brotli；msdfgen 使用 core-only。
msdfgen 不构建 standalone、OpenMP、Skia、安装目标；通过 FT_Outline_Decompose 转换轮廓。

Font 持久化 sourceBytes:List<uint8>、faceIndex:uint=0；字体元数据从字节解析。
元数据为 familyName、styleName、unitsPerEm、ascender、descender、lineHeight。
revision 为运行时 ulong，重新导入成功后递增；Font 不保存 FT_Face、Atlas 或 GPU 句柄。
导入 .ttf/.otf/.ttc；越界 faceIndex 导入失败；Player 使用打包字节，不读系统字体。
cooked 保存格式版本 1、faceIndex、字节长度与字节；先校验和解析，再替换资源。
FontRasterMode={Bitmap=0,SDF=1,MSDF=2}；Atlas 通道分别为 R8、R8、RGB8，均为线性。

字形度量使用未 hint 字体单位；按 fontSize/unitsPerEm 缩放，三模式 advance 相同。
Bitmap 光栅字号取四舍五入并限于 [1,512]，留白 1 texel，处理正/负 pitch。
位图字号 = 逻辑字号 × 光栅缩放；Overlay/Offscreen 使用 Canvas scale，WorldSpace 光栅缩放固定为 1。
WorldSpace 的布局、Image/Text 网格、控件附加网格与文字排版均与摄像机无关。
相机移动、增删与分辨率变化不重建 WorldSpace 几何；需要跨距离保持清晰时使用 SDF/MSDF，shader 按 UV 导数处理边缘。
每台相机分别发布呈现快照，命中检测使用对应相机矩阵、视口和深度，不生成相机网格变体。
SDF/MSDF 为 64 pixels/em、range=4、padding=6、edge angle=3 radians、seed=0。
空格只缓存度量，不分配 Atlas；空轮廓同样不生成像素。

Atlas 键：fontObjectId、fontRevision、glyphIndex、rasterMode、bitmapPixelSize。
SDF/MSDF 的 bitmapPixelSize=0；页面保存 pageId、generation、texture、row cursor、lastUsedFrame。
普通页 1024×1024；逐行装箱，宽不足换行，高不足换页，不旋转、不移动已有字形。
超大字形分配能容纳它的二次幂独立页；超设备上限使用缺字图形并记录错误。
64 MiB 为软预算：先收集本帧全部视图字形并固定命中页，再生成冷字形。
只驱逐未固定的 LRU 页；当前工作集超过预算允许超出，禁止本帧反复驱逐。
页面回收推进 generation，依赖几何失效；GPU 资源待原生帧释放引用后销毁。
新增字形只上传占用矩形；首版在主线程同步生成，不承诺冷缓存无耗时峰值。

缺字：glyphIndex=0 时不查备用字体、不用 .notdef，生成统一空心方框。
方框 advance=0.6em、宽=0.5em、高=0.8em、线宽=0.05em；Font=null 同样处理。
方框由 C# 四条矩形组成，避免缺字仍依赖字体光栅化；行高使用 1em。

Text 配置：font:Font?=null、text:string=""、fontSize:float=16、rasterMode=Bitmap。
wrap=false、lineSpacing=1；horizontalAlignment={Left,Center,Right} 默认 Left。
verticalAlignment={Top,Center,Bottom} 默认 Top；fontSize≥1、lineSpacing≥0。
C# 内部使用 string/Unicode 标量，缓存标量到 UTF-16 范围；桥接文本为 UTF-8。
非法代理项替换 U+FFFD；CRLF/CR 归一 LF；Tab 前进到四空格制表位。
拉丁单词优先空白断行，超长词允许标量间断行；汉字允许标量间断行。
行首禁“，。！？、；：）》】”，行尾禁“（《【”；换行断点必须使当前行消费至少一标量。
单标量超过行宽仍置入该行；行尾折叠空白不计视觉宽度。
kerning 只作用于同一 Font 的相邻有效 glyph；控制字符或缺字处清空前字形。
对齐只平移结果，不改变测量宽度；每行基线使用 ascender 与 lineHeight。
TextLayoutResult 保存行范围、标量映射、glyphIndex、advance、位置、测量尺寸。
HitTestCaret 找最近行后按相邻 advance 中点选择标量边界。

# 关于裁剪与渲染系统

MaskMode={Rectangle=0,ImageAlpha=1}。
Mask 配置：mode=Rectangle、texture=null、uvMin={0,0}、uvMax={1,1}、hitTestThreshold=0.1。
阈值限于 [0,1]；Mask 作用于同节点图形与子树，自身不输出颜色。
ImageAlpha 只接受保有 CPU 像素的普通 Texture2D，不接受渲染目标。
未设置 Alpha 纹理时覆盖率为零，Inspector 报错；只有 R8 的遮罩读取 R，RGB 视为全 1。
RGBA 读取 A，双线性采样、边界外零；纹理缓存按对象身份+资源 revision 失效。
C# UIClipStack 保存局部矩形、逆矩阵、UV 与阈值；奇异矩阵视为不可见/不可命中。
累计覆盖率为各层乘积；每层累计值达到该层阈值才允许命中。
ScrollBox 内部矩形只裁剪 content 子树，不裁剪独立滚动条。

原生覆盖率池按视口尺寸分类，活动嵌套层各保有一个 R8 颜色目标。
Push：取目标清零→投影裁剪形状→写 parentCoverage*ownCoverage→恢复 UI 颜色目标。
Pop：恢复父覆盖率→归还子目标；根覆盖率为 1。
WorldSpace 每台相机分别投影遮罩；遮罩过程不写场景深度。
覆盖率分配失败跳过整段裁剪子树，不绘制无遮罩替代结果。

TextureAlphaMode={Straight=0,Premultiplied=1}；普通图片 Straight，Canvas 输出 Premultiplied。
Tint 为线性颜色；彩色纹理按自身 colorSpace 解码，字形/遮罩不执行 sRGB 解码。
Straight：a=sample.a*tint.a*coverage，rgb=sample.rgb*tint.rgb*a。
Premultiplied：a 同上，rgb=sample.rgb*tint.rgb*tint.a*coverage，不能再乘 sample.a。
BlendMode::PremultipliedAlpha=2；RGB/A 均使用 ONE、ONE_MINUS_SRC_ALPHA。
SDF coverage=clamp(screenRange*(r-0.5)+0.5,0,1)；MSDF 把 r 换为 median(rgb)。
screenRange 由 UV 导数、Atlas 尺寸和 distanceRange 计算，下限为 1。

Offscreen 在相机场景之前绘制，清透明黑，输出线性预乘 Texture2D。
C# 按输出纹理依赖拓扑排序；检测强连通分量，自环或环内画布清透明并报告路径。
非循环下游仍执行并采样透明；禁止同一次 draw 读写同一纹理。
Canvas.GetOutputTexture():Texture2D? 返回输出；resize 保持对象身份、推进 GPU 代次。
外部持有输出纹理时 Canvas 销毁后保留末次内容；无人持有时随资源回收释放。
普通场景材质通过内置 ui_surface.orbshader 以无光照预乘混合显示该纹理。

WorldSpace 在相机场景后、输出转换前执行，LessEqual、关闭深度写、关闭背面剔除。
匹配相机 drawLayerMask；不计算光照，但参与曝光和输出转换。
Overlay 在相机输出后、ImGui 前：复制目标→解码线性 RGBA16F→混合 UI→编码回目标。
Overlay 不再次执行曝光；每个显示目标只进行一轮解码/编码。
无相机分支仍执行 Offscreen/Overlay；编辑器只绘制到显式注册的 GameView/Preview 目标。

后端新增 GpuVertexLayout::UI=4、GpuRenderTargetFormat::R8=2。
Texture2D 增加 CreateDynamic(w,h,channels)、CreateRenderTarget(w,h)、UpdateRegion、ResizeRenderTarget。
Dynamic 保有 CPU 数据，局部写同时更新 CPU 与 GPU 脏区域；RenderTarget 不要求 pixels。
动态/目标纹理不得通过 Resources.Load 创建；静态工厂通过 MetaGen 生成绑定。
纹理附件归 render target 所有，释放只发生一次；context 仅持有资源 Ref。
resize/销毁先使命中快照失效，再释放 GPU 目标。

# 关于输入与事件系统

原生记录类型按以下顺序从 0 赋值：
PointerMove、PointerDown、PointerUp、PointerCancel、Wheel、KeyDown、KeyUp、TextCommit、
CompositionStart、CompositionUpdate、CompositionCommit、CompositionCancel、WindowFocusLost、GamepadState。
sequence 递增且保留到达顺序，Down/Up 不折叠；文本使用同帧 UTF-8 池。
pointerId=0 为鼠标，触摸 id 从 1 开始映射；device={Mouse=0,Touch=1,Gamepad=2,Keyboard=3}。
指针 key 为 Left=0/Right=1/Middle=2，触摸固定 Left；手柄 key 为 AxisX=0/AxisY=1/Submit=2/Cancel=3。
原始坐标为窗口逻辑坐标；UIInputRecord 的 modifiers 位为 Shift=1、Control=2、Alt=4。
GamepadState 用 key 标识轴/键，value 保存数值；断连生成全部释放并取消导航重复。
原生负责设备事实与兼容鼠标去重，C# 负责 UI 策略。

UIInputModule 为 abstract class：
```csharp
public abstract void Process(ReadOnlySpan<UIRawInputEvent> events, float deltaTime, UIInputRouter router);
public virtual void Reset();
```
UIRawInputEvent 为托管 record，含 UIInputRecord data 与 string text。
UIWorldContext.SetInputModule(UIInputModule module) 先 Reset 旧模块、取消捕获，再替换。
StandardUIInputModule 为默认；手柄死区 0.25，首次重复 0.4 秒，后续间隔 0.1 秒。
Submit/Cancel 用按下边沿；模拟暂停不停止 UI 时间。

命中使用最近一次成功呈现的快照：视图、相机、变换、绘制序、裁剪与场景深度同一呈现序号。
首次呈现前指针不命中；resize、世界/会话变更后清快照。
窗口逻辑点先减目标显示区域 origin，再乘 framebufferSize/logicalSize，最后翻转 Y。
指针位置不在视口内直接不命中；不得拿编辑器整个窗口尺寸作为 GameView 尺寸。
Overlay 按画布和图形逆序命中；顶部 raycastTarget 图形阻挡后方。
从命中图形沿祖先寻找 UIControl；无控件的阻挡图形仍消费指针按下序列。
WorldSpace 反投影相机射线，与候选图形平面求交；平行、负 t、奇异变换跳过。
交点转换局部坐标执行 Raycast/Mask，再与已呈现场景深度比较。
深度读取按 viewId+presentedFrame+像素缓存，一次输入阶段同像素只回读一次。
NDC z 转窗口深度使用 (z+1)/2，容差 1e-5；读取失败不允许该候选命中。
视图从最后合成的相机向前查询；Overlay 优先于 WorldSpace。
Offscreen 仅接受 Canvas.InjectPointer(in UIPointerEvent input)，下一输入阶段排空队列。

UIPointerEvent 保存 pointerId、phase、button、position、delta、timestamp、viewId、sequence。
UIHitResult 保存 UIVisual visual、UIControl? control、Canvas canvas、vector2 localPoint、ulong presentedFrame。
UIPointerState 保存 downTarget、captureTarget、pressPosition、lastPosition、dragging、button。
拖动阈值为 6 个窗口逻辑像素；离开目标后释放不构成点击。
滚动手势超过阈值时优先当前控件可处理轴，否则交最近可处理该轴的 ScrollBox。
移交先 Cancel 原按下目标，再建立新捕获；同一范围控件只接受一个拖动指针。
滚轮从命中节点向祖先传播，当前容器在该方向已到边界则交外层。
失焦、隐藏、禁用、销毁、视图切换立即取消捕获；Up 不得恢复已取消点击。

Tab 使用画布/层级顺序，Shift+Tab 反向；方向导航优先显式引用。
自动候选位于方向半平面，score=forwardDistance+2*perpendicularDistance，最小者获焦点。
距离相同按绘制顺序；仅候选 interactable && IsUIActive()。
UI 消费 Down 后占有该键/指针直到 Up/Cancel；held/up 同样不进入游戏查询。
Input.Key/KeyDown/KeyUp 使用消费后状态；RawKey/RawKeyDown/RawKeyUp 给编辑器和模块。
GUI 取消占有而物理键仍按下时继续屏蔽至释放，避免游戏看到无 Down 的 held。

IME 每个文本焦点使用递增 textSession；离焦取消组合，旧 token 提交被丢弃。
Windows 文本/触摸共享窗口子类入口；字符回调只生成 TextCommit，不从键码推字符。
IME 已提交内容对应的兼容字符消息去重；剪贴板使用 Unicode，桥接转换 UTF-8。
光标矩形转换到窗口客户区逻辑坐标供 IME 候选窗定位。

UI 事件统一进 FIFO；每事件先更新状态/视觉，再持久化行顺序，再代码订阅顺序。
回调生成新事件入队尾，不递归派发；单帧上限 4096，超限清余项并报错。
代码事件监听器在开始派发时快照；回调增删订阅从下一事件生效。
回调前验证源、目标与代次；删除源终止该源余下调用；单监听器异常不阻断其他有效目标。
持久化 UIEventBinding={int eventId,Component? target,string methodName,bool useArgument}。
底层四列表 bindingEvents/Targets/Methods/UseArguments 长度必须相同，事务原子更新。
目标方法为 public 实例 void，无参数或精确匹配事件参数；禁止猜重载和隐式数值转换。
方法解析缓存绑定精确 ComponentHandle+generation，不按“该类型第一个实例”调用。
无效目标/方法保留配置并标红，运行时跳过且每次绑定代次只记录一次错误。

# 关于控件系统

UIControl 配置：interactable=true、targetVisual:UIVisual?=null、四方向 navigation:UIControl?=null。
状态色 normal={1,1,1,1}、hover={0.9,0.9,0.9,1}、pressed={0.7,0.7,0.7,1}、disabled={0.5,0.5,0.5,0.5}。
状态色乘入提交时 tint，不改 targetVisual 持久化颜色。
UIControl 扩展方法全部 public virtual，默认无操作；bool 默认 false：
```csharp
void OnPointerEnter(in UIPointerEvent input);
void OnPointerExit(in UIPointerEvent input);
void OnPointerDown(in UIPointerEvent input);
void OnPointerMove(in UIPointerEvent input);
void OnPointerUp(in UIPointerEvent input);
void OnPointerCancel(in UIPointerEvent input);
bool OnScroll(vector2 delta);
bool OnNavigate(UINavigation direction);
void OnSubmit();
void OnCancel();
void OnFocusChanged(bool focused);
```
UINavigation={Up=0,Down=1,Left=2,Right=3}；路由统一维护 pressed/hover/focus。
提供 public void Focus()、public bool HasFocus()；Focus 只接受可交互活动控件。
补充 protected virtual void PopulateOverlay(UIMeshBuilder mesh)，默认空。
控件附加图形在同节点 UIVisual 之后、子节点之前绘制；TextField 用它绘制文本/选区/光标。
附加网格独立缓存，由 protected void SetOverlayDirty() 标记。

| 控件 | 代码事件 | eventId |
|---|---|---|
| Button | event Action? Clicked | 0 |
| CheckBox、RadioButton | event Action<bool>? CheckedChanged | 1 |
| Slider、ScrollBar | event Action<float>? ValueChanged | 2 |
| ScrollBox | event Action<vector2>? ScrollChanged | 3 |
| ComboBox | event Action<int>? SelectionChanged | 4 |
| TextField | event Action<string>? TextChanged、Submitted | 5、6 |

Button：有效同目标 Up 或 Submit 发 Clicked；Cancel/禁用/离焦清按下状态。
CheckBox：isChecked=false、checkmark:UIVisual?=null。
SetChecked(bool value,bool notify=true) 先更新值和标记的运行时可见性，再排事件；同值不通知。
运行时可见性覆盖不改 checkmark.enabled；源失效时撤销覆盖。
RadioButton：上述字段加 groupRoot:EnsId=Null；未设分组取直接父 Ens，无父取 Canvas 根。
选中时先更新全组，再按层级顺序发其他项 false，最后发自身 true。
用户点击已选项保持选中；代码可设置 false，使组为空。
加载多项选中保留配置并显示冲突，运行状态仅启用层级最前项；不标脏场景。

Slider：minimum=0、maximum=1、value=0、wholeNumbers=false、orientation=Horizontal、reverse=false。
track、thumb、fill 为 UILayout?，默认 null；缺 track/thumb 则保留值但禁用指针拖动并显示错误。
SetValue(float value,bool notify=true) 夹紧范围，整数模式中点远离零舍入，再通知。
SetRange(float minimum,float maximum) 原子设置两界；min>max 拒绝，零区间值固定为 min。
归一化为 (value-min)/(max-min)，零区间为 0；reverse 使用 1-normalized。
thumb 中心在扣除 thumb 长度的轨道内移动；有效长度≤0 时拖动不改变值。
点击轨道直接跳转；导航步长为 wholeNumbers?1:区间长度*0.1。
fill 从方向起点延伸至归一位置；thumb/fill 的驱动矩形不修改配置。

ScrollBar：value=0、pageSize=1、orientation=Horizontal、reverse=false、track/ thumb=null。
value/pageSize 限于 [0,1]；thumbLength=trackLength*pageSize。
点击 thumb 外侧按方向移动 pageSize；拖动使用 trackLength-thumbLength。
pageSize=1 时 value 保持 0；每控件只允许一个 pointerId 拥有拖动。

ScrollBox：content:UILayout?、horizontalBar/verticalBar:ScrollBar? 均 null。
horizontal=false、vertical=true、scrollOffset={0,0}、wheelStep=40、deceleration=10。
content 必须是直接子节点，左上锚点/pivot、单位旋转/缩放；无 content 显示空容器并报配置错误。
range=max(contentSize-viewportSize,0)，关闭轴固定零；驱动偏移为 {-offset.x,+offset.y}。
触摸释放保留最近位移/时间速度；每帧 offset+=velocity*dt，velocity*=exp(-deceleration*dt)。
dt≤0 时不积分；到边界对应速度归零，速度绝对值<0.1 时归零；不实现弹性越界。
内容变化先夹紧，再同步条；pageSize=min(1,viewport/content)，零内容取 1。
bar.value 与 offset/range 双向转换，用不通知写入防递归；一次最终变化只发一次 ScrollChanged。
滚动裁剪挂在 content 子树，独立滚动条不受影响。

ComboBox：options:List<string>=[]、selectedIndex=-1、label:Text?=null、popupHeight=180、itemHeight=30。
公开 GetOptionCount/GetOption/InsertOption/SetOption/RemoveOption/ClearOptions/Open/Close。
签名中索引为 int、文本为 string；GetOption 返回 string，计数返回 int，其余返回 void。
索引越界抛 ArgumentOutOfRangeException；null 文本归一为空串。
SetSelectedIndex(int index,bool notify=true) 允许 -1；非空选择更新 label 后发事件。
删除当前项后选原索引处新项，越界取最后项，空表为 -1；删除前面的项保持原条目。
弹层使用同 Canvas 的临时 ScrollBox/Button 列表，不创建嵌套 Canvas。
临时 Ens 带通用 DontSave 标志，复制/保存/Prefab 枚举跳过；UIWorldContext 负责销毁。
弹层默认向下，不足且上方更大则向上，最后夹紧 Canvas 根矩形。
外部点击关闭并消费该序列；Cancel 关闭恢复原有效焦点；改变 options 立即关闭。
打开第二个 ComboBox 前关闭当前 Canvas 弹层；Open/Close 重复调用无副作用。

TextField：text=""、font=null、fontSize=16、rasterMode=Bitmap、maxLength=0、readOnly=false。
placeholder=""、textTint={1,1,1,1}、selectionTint={0.2,0.4,1,0.5}、caretTint={1,1,1,1}。
maxLength 按 Unicode 标量计数，0 为无限制；SetText(string text,bool notify=true) 共用输入归一流程。
支持方向、Home/End、Shift 选区、Backspace/Delete、Ctrl+A/C/X/V、Enter 提交。
readOnly 允许选区/复制，拒绝改值、剪切、粘贴；程序 SetText 仍可设置。
所有索引均为标量索引，实际 string 修改通过 UTF-16 映射，禁止切断代理对。
粘贴将 CRLF/CR/LF/Tab 转成空格；按替换选区后的剩余配额截断。
Composition 独立于实际 text，只有 Commit 替换选区一次；Cancel 不改变实际值。
组合期间 Enter 交给 IME，不另发 Submitted；非组合 Enter 发一次 Submitted。
绘制顺序为背景 Image→选区→文本/组合→光标，附加矩形裁剪约束所有内部图形。
光标周期 1 秒，前 0.5 秒显示；输入与移动重置；横向滚动让光标距边界≥1 逻辑单位。
placeholder 仅在实际文本为空且没有组合时显示，不参与选择或提交。

# 关于UI编辑器系统

UIEditor 包含 UICreationMenu、UILayoutEditor、CanvasEditor、MaskEditor、FontEditor。
另含 UIControlEditor、UIEventBindingEditor、UILayoutGizmos、UIPreviewController。
UIPreviewPanel 位于 EditorCS/Panels，仅引用公共 IUIPreviewProvider。
CustomEditor 注册支持 UI 派生类型，且使用统一 ManagedAssemblySession 的 Type。
运行时包不直接引用 Editor 历史/面板类。

创建菜单使用 EnsContextMenuRegistry；无所属 Canvas 时先创建 Overlay Canvas。
| 菜单 | 根组件与子节点 | 默认尺寸 |
|---|---|---|
| Canvas | UILayout+Canvas | 目标尺寸 |
| Image | UILayout+Image | 100×100 |
| Text | UILayout+Text | 160×30 |
| Button | UILayout+Image+Button；子 Text | 160×30 |
| CheckBox/RadioButton | UILayout+Image+控件；子 Mark(Image)、Label(Text) | 160×24 |
| Slider | UILayout+Slider；子 Track(Image)、Fill(Image)、Thumb(Image) | 160×20 |
| ScrollBar | UILayout+ScrollBar；子 Track(Image)、Thumb(Image) | 160×16 |
| ScrollBox | UILayout+Image+ScrollBox；子 Content、VerticalBar | 200×160 |
| ComboBox | UILayout+Image+ComboBox；子 Label(Text)、Arrow(Image) | 160×30 |
| TextField | UILayout+Image+TextField | 160×30 |
| LayoutBox/GridBox/Mask | UILayout+对应组件 | 200×200 |

每个子节点也带 UILayout；默认作者 Transform XY=0，Z=0，单位旋转/缩放。
文本子节点双轴拉伸，offset=0、sizeDelta={-8,-4}；文本默认为控件类名。
Mark 为左侧 16×16，Label 留左边距 24；勾选图形采用纯色矩形。
Slider Track 双轴拉伸且高 4，Thumb 16×20；Fill 由 Slider 驱动。
ScrollBar Track 拉伸，Thumb 由 pageSize 驱动；VerticalBar 右侧宽 16。
ScrollBox Content 占根减去右侧 16 的视口，初始内容高 320，左上锚点/pivot。
Arrow 为右侧 12×12 纯色矩形；用户可替换纹理，首版不新增矢量图标系统。
创建过程先建对象，再配引用，成功后记录整子树快照和选中；失败撤回本次对象。
Undo/Redo 使用稳定 ID 与序列化快照，不闭包保存已销毁包装。

所有属性编辑走 PropertyDocument；新增公开 EditorPropertyHistory.RecordAction(string label,Action undo,Action redo)。
矩形移动写 offset，尺寸调整写 sizeDelta，锚点预设一次提交五组布局字段。
“保持矩形修改 pivot”根据起始矩形公式重算 offset，不累计使用中间拖动值。
驱动轴只读并显示 owner；Scene 手柄使用解析世界矩阵与射线平面交点。
拖动开始保存基线，结束合成一条历史，Escape 恢复基线；解析变换不写历史。
事件表一行事务同时提交四列表；显示无效引用/方法但不自动删除。
FontEditor 显示 faceIndex、元数据与三模式字形预览；预览不得将系统字体路径写入资源。

IUIPreviewProvider 位于 Orbeden.Editor 公共合同：
```csharp
Texture2D? GetPreviewTexture();
void SetResolution(int width,int height);
void SetVisible(bool visible);
void InjectPointer(in PreviewPointer input);
void CancelInput();
```
PreviewPointer 为 readonly record，含 uint pointerId,phase,button；vector2 position,delta；double timestamp。
注册入口 UIPreviewRegistry.SetProvider(IUIPreviewProvider? provider)，替换前 CancelInput。
EditorGUI 增加 DrawTexture(Texture2D texture,vector2 size)，内部解析资源身份，不公开 GL 句柄。
面板显示实际 UIRenderer 输出；不使用线框代替像素预览。
Play 才注入交互；编辑状态只显示布局/渲染；隐藏/失焦/resize/切目标时 CancelInput。
编辑器文本框持焦点时字符、IME、导航不转发给游戏 UI。
Preview 输出注册为独立视图，隐藏后停止额外目标更新。

# 关于发布与验收系统

所有原生 Object 派生类放 Runtime/Object；非 Object 服务不放该目录。
新增函数提供简短中文 XML 说明；C++ 声明采用相同语义的紧邻中文注释。
Generated、Bindings、API 文档通过生成器刷新，禁止手工修补生成结果。
项目格式版本升为 37，BuildAndPackaging 记录 ABI、包覆盖、托管编辑上下文与 vector2。
SDK 同时发布 GUI 源码、版本文件、内置 shader、第三方授权和接口文档。
CoreCS 文档生成继续描述桥接；GUI 包的公开 API 从游戏编译输出与 XML 文档合并投影。
投影按命名空间/完整类型名去重，文档只收录包源码类型和项目公开扩展。
GUI 包改动不得触发 C++ 或原生 Binding 生成；首次基础接入仍需要构建引擎。

统计项：Layout、TextLayout、GlyphRaster、AtlasUpload、Geometry、Clip、Input、EventDispatch、NativeSubmit、Draw。
计数项：布局节点、几何重建、冷字形、上传字节、Atlas 字节、批次、裁剪目标峰值、深度回读。
性能验收使用暖缓存、固定分辨率、无属性修改场景，连续采集 120 帧。
布局重建/几何重建/冷字形/顶点上传须为零；帧描述和 draw 允许继续提交。
不以语言文件数量比例验收，以“新增控件不改原生”作为架构判据。
每完成下列步骤并通过其检查立即勾选；系统全部通过后勾选路线图。

# 详细步骤

- [x] 托管基础：补齐 vector2 的反射、持久化、互操作与 Inspector。
  修改 CoreCS/ComponentProxy.cs、ManagedScriptInterop.cs、ManagedScriptHostFields.cs。
  修改 Native/Runtime/Reflection.h/.cpp、Runtime/Object/Script.cpp、Scripting/ScriptInterop.cpp。
  新增 InteropValue.From(vector2)，值枚举尾部追加 Vector2，payload 前 8 字节写 x/y、剩余清零。
  修改 MetaGen 的 BindingTypes/字段投影与 Inspector 对应分支，标量/数组都验证往返。

- [x] 托管基础：实现 IManagedComponentLifecycle 与两阶段构造。
  ScriptRuntime 新增 ConstructHost(IntPtr)、ApplyHostState(ScriptInstance)、AttachHost(ScriptInstance)。
  Construct 只登记身份；Apply 恢复字段；Attach 统一通知；批量初始化按阶段遍历。
  DestroyScript 先取消活动再 Detached；同一实例不重复通知，异常不阻止释放。

- [x] 托管基础：修复精确实例引用与引用快照。
  修改 Script.Native.ResolveReference 与 OrbedenNativeApi.cpp 的 ResolveScriptReference。
  托管类型路径先定位宿主再取包装，禁止原生类型表查找 Orbeden.Button。
  为未解析引用保留路径表；明确用户清空才丢弃，复制重映射同时更新该表。
  在 WorldSerializer 与编辑器复制/Prefab 的引用重映射中覆盖 Ref 和 Ref 集合。

- [x] 托管基础：实现字段原子事务与保存刷新。
  ManagedTypeMetadataCache 新增 FlushHostFields(Script,IntPtr):void。
  新增 ApplyHostFieldsTransaction(Script,IntPtr,IReadOnlyList<string>):void。
  转换全部输入后赋值、Validate；异常回滚基线；最外层只发一次 FieldsChanged。
  保存、复制、Prefab、Play/重载快照先 Flush，再调用原生序列化。

- [x] 托管基础：统一组件约束。
  InspectorAttributes 新增 ComponentConstraintAttribute，Ens 新增 ValidateComponentSet(IReadOnlyList<Type>,Type):void。
  所有添加入口复用检查；加载/Undo 在全体恢复后检查；整 Ens 删除放行依赖。
  为临时对象增加通用 Ens 的 DontSave 标志，世界序列化/复制/Prefab 排除该节点及子树。
  DontSave 为运行时标志，不从场景文件恢复；删除父节点仍正常销毁临时子节点。

- [x] 包与程序集：建立 Package 目录和版本合同。
  新增 Orbeden.UI.targets、Package.version；修改两份 MetaGen targets/csproj 的 Compile 项。
  原生 Binding 生成设置原生文件/导入 manifest/生成器版本增量输入。
  记录原生源文件清单指纹以检测删除/改名；C# 文件不进入该指纹。
  构建验证 SDK 包/覆盖包只能命中一个来源。

- [x] 包与程序集：实现 ManagedAssemblySession。
  按“GUI源码包与程序集”方法合同实现缓存、流加载、依赖解析和卸载。
  移除 InspectorPanel/GameLoadContext 的独立所有权，改用同一会话。
  ScriptRuntime.ResolveType 只查当前会话与 Core；禁止扫描历史 AppDomain 程序集。
  清理 CustomEditor、菜单、属性、方法、工厂及绑定缓存后才 Unload。

- [x] 对象与调度：增加托管帧系统合同及 UIWorldContext。
  （含原生 context 订阅 World 结构/活动/Transform 通知、队列上限与 FullResync。）
  （托管侧已完成：IManagedFrameSystem、ManagedFrameSystems、UIRuntimeBootstrap、UIWorldContext、UINode、
  变更队列与 FullResync、布局重建与派生位置提交口。仍缺原生 context 订阅 World 结构/活动/Transform 通知，
  它随 RetainedGuiApi 的 CreateContext/ReadChanges 一起落地。）
  新增 IManagedFrameSystem、ManagedFrameSystems、UIRuntimeBootstrap、UIWorldContext、UINode。
  工厂注册不创建世界对象；AttachWorld 后构造节点索引，DetachWorld 逆序释放。
  UINode 父子关系来自 Ens；结构变化按 ReadChanges 更新，FullResync 重建索引。
  原生 context 订阅 World 结构/活动/Transform 通知；每帧最多保留 65536 条，超出置 FullResync。

- [x] 对象与调度：扩展 ScriptSystem 与跨域阶段。
  新增 ScriptExecutionMode={Editor=0,Play=1}、Initialize(ScriptExecutionMode):bool。
  新增 ProcessManagedInput(float32):void、PrepareManagedRender(float32):void。
  新增 DispatchExternalCallbacks(const std::function<void()>&):void，以作用域恢复派发标志。
  ScriptEntryPoints.initialize 改为 (void*,uint32)，尾部追加 processInput、prepareRender。
  同步 GameModule、GameScriptRuntime、GameAotExports、EditorPlayMode、game_main 的绑定。

- [x] 对象与调度：接入 Application 与编辑世界。
  在 FixedUpdate 前处理 UI 输入；所有 LateUpdate 后、Render 消费前准备 UI。
  非模拟与暂停路径仍执行 UI 两阶段；Editor 初始化只构造包装与帧系统。
  退出 Play 释放实例后恢复编辑快照；重新附着编辑世界不重新加载程序集。
  阶段退出应用删除/换世界，再检查代次；失效帧不能发布。
  （Application::Update 里 ProcessManagedInput 排在 FixedUpdate 之前、PrepareManagedRender 排在
  全部 LateUpdate 之后，两者都在暂停门控之外，脚本阶段只在 Editor 模式跳过输入路由。
  ScriptRuntime 建完包装后按 worldRevision 附着帧系统，Editor 模式只保留包装与帧系统、
  不跑游戏生命周期；Shutdown 先 DetachWorld 再断开包装，程序集由 ManagedAssemblySession 持有，
  进出 Play 换的是世界实例。UIWorldContext 把 worldRevision 与托管会话代次绑进原生上下文句柄，
  结构记录按代次过滤，换世界后失效的帧不会发布。）

- [x] 对象与调度：实现 UIElement。
  新增 UIElement.cs、UIDirtyFlags.cs；显式接口先维护索引，再调用扩展回调。
  验证 Canvas 归属、布局链、static；错误子树停止输入和提交并保留配置。
  OnUIDisabled 清捕获/焦点/临时可见性和派生位置；重新启用标全脏。

- [x] 布局：实现 UILayout、UIRect、Canvas。
  （GetOutputTexture 与 InjectPointer 分别依赖动态纹理与 UIPointerEvent，随渲染与输入阶段落地。）
  按配置表生成字段和访问器；UILayout 新增 GetResolvedRect():UIRect。
  Canvas 新增 GetOutputTexture():Texture2D?、InjectPointer(in UIPointerEvent):void。
  解析尺寸先验证有限性，零目标不投影；屏幕与世界根分别按公式处理。
  Setter 只标受影响类别，输出 texture 由 context 创建与持有。

- [x] 布局：实现通用派生 Transform 位置。
  Transform 增加 ownerToken、derivedPosition、hasDerivedPosition 非持久化字段。
  新增 SetDerivedLocalPosition(uint64,const vector3&):bool、ClearDerivedLocalPosition(uint64):void。
  新增 GetResolvedLocalPosition() const:const vector3&，矩阵计算改读此值。
  ownerToken 非零且匹配才允许更新/清除；作者 Getter/序列化保持读取作者值。
  批量 ApplyDerivedPositions 在 DirtySuppressionScope 中执行，并标识通知来源。

- [x] 布局：实现 UILayoutRegistry 与三种布局类。
  新增 MarkDirty(UILayout):void、Rebuild():void、Clear():void。
  提升脏根并去重，四阶段遍历，最后批量提交位置；重建中新增请求放下一队列。
  实现 UILayoutGroup 有效子缓存、LayoutBox 主/交叉轴公式和 GridBox 三约束。
  N=0、零可用尺寸、负 sizeDelta、父驱动与 fit 冲突按正文规则处理。

- [x] 图形：实现 UIVisual、UIMeshBuilder、Image、UIGraphicRegistry。
  UIVisual 继承两个测量方法；缓存片段列表，Geometry 与 Material 分开脏集合。
  内置网格顶点 tint 为白色，提交 tint 乘 UIVisual.tint 与控件状态色。
  AddTriangle 验范围；SetTexture 切片段；PopulateMesh 失败清当前图形并记录错误。
  Image 按 Simple/NineSlice 规则生成，验证小于边框总尺寸时无反向几何。

- [x] 图形：实现 UIFrameBuilder 与 UIGeometryCache。
  BeginFrame(ulong):void 重用容量；BuildCanvas(Canvas,in UIView):void 生成命令。
  Submit():void 先传网格变更，再传全部画布，最后 EndFrame 原子发布。
  缓存记录 meshId/revision 与页面 generation；移除图形提交 RemoveMeshes。
  命中列表与绘制遍历同源，呈现完成才切换其可用帧号。

- [x] 图形：实现 RetainedGuiTypes 与 RetainedGuiBridge/RetainedGuiNative。
  （18 槽已就位并接入：CreateContext/DestroyContext/UpdateMeshes/RemoveMeshes/SubmitCanvas/EndFrame/
  ReadViews/ApplyDerivedPositions/ReadChanges 全部可用；ReadInput/ConsumeInput/ReadDepth/QueryGlyphs/
  RasterizeGlyphs/GetKerning/SetTextInput/ReadClipboard/WriteClipboard 按各自子系统的空集语义返回，
  随平台输入、字体、渲染阶段接入实现。）
  按 ABI 表实现 18 槽，Span 展开与结构布局严格一致。
  验证 count 非负、加乘溢出、索引范围、矩阵索引、clip 配对、资源身份与代次。
  句柄为高 32 位 generation/低 32 位 slot，0 无效，释放推进 generation。
  两套根 API 尾追加 GetRetainedGuiApi，ABI=3，大小断言同步。
  同步 OrbedenCoreRuntime 初始化和 Editor 入口校验，版本不符拒绝初始化。

- [x] 字体：接入 FreeType/msdfgen 与 Font 资产。
  （依赖由 Tools/OrbedenThirdParty 生成器产出，OrbedenCore 以库依赖链接。
  已实现：Font 资产（字节 + faceIndex + 元数据 + revision）、FontRasterizer
  （OpenFont/GetGlyphMetrics/GetKerning/RasterizeGlyph/ValidateFontBytes/ReleaseFont/Shutdown，
  三种模式的 advance 一致、度量走未 hint 字体单位）、AssetPipeline 的 .ttf/.otf/.ttc 导入与 faceIndex
  设置、CookedAssetSerializer 的字体载荷（版本 1，先校验再替换资源）、RetainedGuiApi 的
  QueryGlyphs/RasterizeGlyphs/GetKerning 三槽。字节数组按引擎既有做法走 cooked 产物
  （与 Texture2D::pixels 一致），不重复写进场景文件。）
  新增 Font.h/.cpp、Runtime/Fonts/FontRasterizer.h/.cpp。
  FontRasterizer 实现 OpenFont(Font&):bool、GetGlyphMetrics(Font&,uint32,FontGlyphMetrics&):bool。
  实现 GetKerning(Font&,uint32,uint32):float32、RasterizeGlyph(Font&,uint32,FontRasterMode,uint32,FontGlyphBitmap&):bool。
  实现 ReleaseFont(int32):void、Shutdown():void；revision 变化先释放旧字体面。
  修改 AssetPipeline、CookedAssetSerializer、ResourceManager 的导入/打包/依赖分支。

- [x] 字体：实现动态纹理与 Atlas。
  （两侧都已完成：TextureAlphaMode、CreateDynamic/CreateRenderTarget、UpdateRegion（局部写同时更新
  CPU 与 GPU 脏矩形）、ResizeRenderTarget、IsRenderTarget、GetAlphaMode、GetMaximumSize 并已生成托管绑定；
  FontAtlasCache 已实现 BeginFrame、RequestGlyph、ResolveRequests、TryGetGlyph、EndFrame、Dispose。
  键为 fontObjectId/fontRevision/glyphIndex/rasterMode/bitmapPixelSize，距离场模式 bitmapPixelSize 记 0。
  普通页 1024×1024 逐行装箱（UIFontAtlasCursor，纯数学可单测），超大字形取能容纳它的二次幂独占页；
  64 MiB 软预算在 EndFrame 按 LRU 回收未固定页，回收页多留一帧再释放纹理。
  缓存按进程共享（FontAtlasCache.Shared），换世界只换原生上下文，不重新光栅化；
  页纹理即托管侧资源根，程序集卸载时 Dispose。
  Atlas 页的 UV 采用 v 轴朝上的约定，与 UIMeshBuilder 的 uvMin/uvMax 一致：
  字形按内存行序（行 0 为顶部）自然装箱，UV 记 1 - row/height，因此 UI shader 采样时需按 t = 1 - v。）
  Texture2D 新增 CreateDynamic(int32,int32,int32)、CreateRenderTarget(int32,int32)，返回 Texture2D*。
  新增 UpdateRegion(int32 x,int32 y,int32 w,int32 h,const uint8* pixels,int32 rowStride):bool。
  新增 ResizeRenderTarget(int32,int32):bool、IsRenderTarget() const:bool、GetAlphaMode() const:TextureAlphaMode。
  新增 static GetMaximumSize():int32；指针数据用 ORBEDEN_BIND_BUFFER 映射 Span。
  FontAtlasCache 实现 BeginFrame、RequestGlyph、ResolveRequests、TryGetGlyph、EndFrame、Dispose。
  请求去重→固定缓存页→生成冷字形→装箱/局部上传→回收非固定 LRU 页。

- [x] 字体：实现 UITextLayout 与 Text。
  （UITextLayout 实现归一化、度量、断行、对齐与脱字符查询，度量通过 IUITextMetrics 注入，
  因此换行与禁则规则可脱离原生宿主单测；FontAtlasCache 实现该接口，充当运行期的度量来源。
  归一化：非法代理项替换 U+FFFD、CRLF/CR 归一 LF、制表符前进到四空格制表位；
  断行：拉丁优先空白断点、超长词与汉字按标量断、行首禁“，。！？、；：）》】”、
  行尾禁“（《【”，每行至少消费一个标量；行尾空白不计视觉宽度；对齐只平移不改变测量宽度。
  字距落在后一个字形的原点上，缺字与控制字符清空前字形；缺字用统一空心方框，步进 0.6em。
  Text 是 UIVisual：缓存布局（文本/字体/字号/模式/换行/行距/对齐/可用宽度任一变化才重排），
  提交时按 Atlas 页分组输出四边形，记录用到的页代次，页回收后只重建依赖几何。
  默认 raycastTarget 为 false。）

- [x] 裁剪：实现 Mask、UIClipStack、UITextureAlphaCache。
  （Mask 组件：mode/texture/uvMin/uvMax/hitTestThreshold（限于 [0,1]，默认 0.1），
  ImageAlpha 拒绝渲染目标；GetDiagnostic 给出“缺纹理/无 CPU 像素”的诊断，检视面板直接读它，
  不写进 UINode.ConfigurationError——那条路径会让整棵子树跳过布局。
  UIClipStack 保存局部矩形、逆矩阵、UV 与阈值，Push(Mask)/PushRectangle(UILayout)/Pop/TestPoint 齐备；
  累计覆盖率为各层乘积、每层累计值达到该层阈值才允许命中，奇异矩阵与空 Alpha 纹理按不可见处理。
  层快照（UIClipSnapshot）被命中记录持有，输入阶段据此过滤命中点。
  UITextureAlphaCache 按对象身份加内容版本失效（原生新增 Texture2D.revision/GetRevision），
  双线性、边界外取零、按 t = 1 - v 取行，与 shader 的 UV 约定一致；R8 读 R、RGBA 读 A、RGB 视为全 1。
  FrameBuilder 按入栈/出栈生成 PushRectangle/PushImageAlpha/Pop 命令，形状与 UV 由一条四边形网格携带，
  形状内容比较后才推进版本，暖帧不重传顶点。
  待渲染阶段：原生覆盖率池（Push 取目标清零→投影裁剪形状→写 parentCoverage*ownCoverage）、
  分配失败跳过整段裁剪子树；ScrollBox 的 content 裁剪随控件阶段接入。）

- [x] 渲染：实现 UIRenderer、UIShaderSources 与后端扩展。
  （新增 Native/Runtime/Gui/UIRenderer.h/.cpp 与 UIShaderSources.h。
  后端扩展：GpuVertexLayout::UI（位置/UV/顶点色，stride 36）、GpuRenderTargetFormat::R8、
  BlendMode::PremultipliedAlpha（RGB 与 A 都走 ONE、ONE_MINUS_SRC_ALPHA）、
  UploadTextureRegion（行距按通道数换算，保存并恢复 unpack 行距/对齐/跳过与纹理绑定）、
  GpuTextureDesc.clampToEdge（UI 纹理必须夹边，越界不能绕回另一侧）。
  着色器：图元两种 Alpha 语义与四种材质分支，距离场按 UV 导数与图集尺寸求 screenRange（下限 1）；
  覆盖率 Pass 写 父覆盖率*本层覆盖率。
  渲染器：网格按 revision 上传、连续两帧无人引用才释放；1×1 白纹理兜底；
  覆盖率池按视口尺寸分类、每层一块 R8 目标、出栈恢复父覆盖率；分配失败跳过整段裁剪子树。
  画布矩阵由托管侧算好放进提交：屏幕与离屏是逻辑像素到裁剪空间的正交矩阵，
  世界空间提交本地到世界的变换，原生侧再乘相机投影。
  RenderOffscreen 已在下一项补全。）

- [x] 渲染：接入正常/无相机/编辑预览三条调用路径。
  （三条路径都接进 RenderSystem：离屏先于相机、世界空间在相机输出 Pass 之前、
  覆盖层在 ImGui 之前；无相机场景同样走离屏与覆盖层。
  离屏输出做实：Texture2D 标记为渲染目标时由 GpuResourceManager 解析出颜色附件目标
  （GpuRenderTargetID 存在 Texture2D 上，尺寸/版本变化先失效再延后释放附件），
  RenderOffscreen 清透明黑后逐块画布绘制；没有命令的画布也提交一次空绘制用于清屏。
  Canvas.GetOutputTexture 首次取用时按输出尺寸创建，resize 只换 GPU 附件、对象身份不变。
  托管侧按输出纹理建依赖图做拓扑排序，卡住即整环判定：环内画布清透明且不画内容，
  路径写进日志，非循环下游照常执行并采样到透明；自环同样按环处理。
  屏幕画布的显示尺寸不再由托管侧猜窗口：渲染器每帧发布视图快照（ReadViews 已有槽位），
  托管侧下一帧读取并写进画布视口，宿主用 SetCanvasViewport 显式设置过的画布不被覆盖。
  深度回读：后端 ReadDepthPixel（只动读绑定），UIRenderer 记录每视图的深度来源与帧号，
  经 RetainedGuiFrame::SetDepthReader 装进 ABI 层——ABI 不依赖渲染模块，帧号对不上就拒绝，
  命中快照据此拿到“已呈现帧 + 对应深度”。渲染器还会作为函数指针的提供方被销毁前摘除。
  编辑预览：RenderSystem::SetUIEditorPreviewTarget 让屏幕画布改画场景面板的离屏目标，
  EditorScene::RefreshSceneViewTarget 按面板可见性设置或清除，Play 时自动回到主帧缓冲。
  新增 Templates/Builtin/Shaders/ui_surface.orbshader：UIRenderer 优先按这个 Key 加载资产着色器
  （Shader::passes[0] 的源码直接编译），读不到才退回内置源码，用户改这个文件即可改 UI 着色。
  Templates 目录整体 xcopy 发布，因此它自动进入打包；内容版本迁移条目留给发布阶段。）
  世界空间画布接通：托管侧按根节点 sizeDelta 解析尺寸（不参与分辨率缩放），
  提交本地到世界的变换（画布节点的 Transform 缩放给出一单位折合多少世界单位），
  原生侧再乘相机视图投影；发布的视图带 WorldSpaceCamera 标记与相机矩阵，
  托管侧据此反投影命中射线。网格与字形不依赖相机，画布每帧只提交一份内容。
  多相机：原生按 drawLayer 与相机 drawLayerMask 的位与结果筛选，并用各自矩阵绘制同一份网格；
  每台相机发布一条带观察者标识的视图，仅供命中与场景深度读取。
  屏幕画布的首帧视口由渲染器每帧报出的主显示目标尺寸引导：
  没有它就会"没视口不提交、不提交没视图"地锁死（编辑器由预览显式设置视口，不走这条路）。）

- [x] 输入：实现有序平台事件与消费状态。
  （新增 Native/InputManager/InputEvent.h：一条有序事件，字段与 UIInputRecord 一一对应，
  桥接层只做搬运；InputManager 新增 PushEvent/GetFrameEvents/MarkEventHandled、
  SetRawKeyState/RawKey/RawKeyDown/RawKeyUp，KeyEnum 尾部追加 HOME/END/DELETE/ESCAPE。
  sequence 由输入系统分配、单调递增且保留到达顺序，Down 与 Up 各自成条不折叠；
  未消费的事件跨帧保留（上限 4096，超出丢最旧并报一次），BeginFrame 只清瞬时态。
  原始键按平台键码单独记账，不受 UI 消费影响，供编辑器与输入模块查询。
  消费语义：被消费的按下占有该键，消费抬起或取消放手；UI 放手时若物理键还按着，
  转为“屏蔽至抬起”，游戏不会看到没有按下来源的持续态；失焦事件让全部占有放手。
  占有只作用于 KeyEnum 按键（键盘与鼠标），手柄的 key 是另一套编号、不参与占有。
  GlfwWindow 的回调补齐事件：按键（带原始键码与修饰位）、鼠标键、指针移动、滚轮、
  字符提交（回调直接给码点，本地转 UTF-8，不从键码推字符）、失焦。
  桥接层 ReadInput 从这条队列取记录并拼同帧 UTF-8 文本池，容量不足不部分写也不消费；
  ConsumeInput 逐条 MarkEventHandled，占用状态由此生效。
  这一项按设计属于“必须用 C++”的一类：回调发生在平台线程入口、状态要被 C++ 玩法代码
  的 Input.Key 直接读取，策略与路由全部留给下一项的 C# 输入模块。
  托管侧同步了 UIInputKind/UIInputDevice/UIPointerButton/UIGamepadKey/UIInputModifiers。）
  GlfwWindow 接字符、滚轮、焦点；Windows 触摸过滤兼容鼠标；手柄仅上报状态。

- [x] 输入：实现 WindowsTextInput、WindowsPointerInput、GamepadInput。
  （新增 Platform/WindowsInputHook：统一窗口子类入口。一个窗口只能有一个替换过程，
  文本与指针都登记在它上面，按登记顺序询问，没人认领才转给原窗口过程。
  WindowsTextInput：Attach/Detach/SetActive/CancelComposition/SetCaretRect/SetTextSession/
  ReadClipboard/WriteClipboard 齐备；WM_IME_* 翻译成组合开始/更新/提交/取消事件，
  GCS_RESULTSTR 的内容记作“待回显”，随后同内容的 WM_IME_CHAR/WM_CHAR 被吞掉，
  已提交内容不会重复上报；组合终止未提交即取消，失焦与 Detach 也发取消；
  候选窗与组合窗按客户区逻辑坐标定位；剪贴板走 Unicode，UTF-8 转换遇到非法序列直接失败，
  不截断标量，读失败不改调用方的文本。
  WindowsPointerInput：WM_POINTER 的触摸与笔映射成 pointerId 从 1 开始的指针事件，
  并吞掉系统为触摸合成的兼容鼠标消息（触摸期间与抬手后 120ms），避免一次触摸算两次；
  摘除时给仍按着的指针补发取消。
  GamepadInput：每帧轮询 GLFW 手柄，只上报状态——轴与键各一条 GamepadState，
  死区 0.2、变化阈值过滤噪声，D-pad 与左摇杆合用两个轴；断连生成全部释放。
  桥接层：SetTextInput 落到文本输入的会话/光标/活跃状态，ReadClipboard/WriteClipboard
  直接调用平台实现；KeyEnum 追加 HOME/END/DEL/ESCAPE（Delete 与 windows.h 的宏同名，
  退一格命名），GLFW 键表补齐对应映射。
  注意：这些路径只做了编译与静态检查，输入法与触摸需要在真实窗口里手工验收。
  这一项按设计属于“必须用 C++”的一类：窗口消息、输入法上下文与手柄轮询都是平台事实，
  策略、捕获与命中全部留给下一项的 C# 输入模块。）

- [x] 输入：实现 UIInputModule、StandardUIInputModule、UIInputRouter、UIRaycaster。
  （全部为 C#。新增 UIControl 基类：interactable、targetVisual、四方向 navigation 引用、
  四态颜色（乘进提交 tint，不改目标图形的持久化颜色）、十一组 public virtual 回调、
  Focus()/HasFocus()、附加网格 PopulateOverlay + SetOverlayDirty（同节点图形之后、子节点之前绘制）。
  UIRaycaster：只用最近一次成功呈现的快照；屏幕与离屏画布把窗口逻辑点按显示缩放换到画布像素、
  再按画布缩放换到逻辑坐标并翻转 Y，裁剪层用同一份快照判定，随后在节点局部空间做 Raycast；
  世界空间画布反投影相机射线（新增通用 4×4 求逆，投影矩阵不是仿射矩阵）、与图形平面求交，
  交点转局部后判 Raycast 与裁剪，再与同呈现序号的场景深度比较（NDC z 取 (z+1)/2，
  容差 1e-5，读不到深度就不允许命中）；深度同像素一次输入阶段只回读一次。
  命中顺序：屏幕画布优先，视图内按绘制顺序逆序，取最上面的那个。
  UIInputRouter：指针按下/移动/抬起/取消、捕获移交（先取消原目标）、拖动阈值 6 逻辑像素、
  离开目标释放不算点击、滚轮从命中节点向祖先传播、同一控件只接受一个拖动指针、
  失焦与销毁取消捕获且 Up 不恢复已取消的点击、回调后重查控件身份。
  导航：显式引用优先，否则取方向半平面内 score=前向距离+2×垂直距离 的最小者，
  距离相同按绘制顺序（按层与深度遍历收集，距离相同保留先出现的）。
  UIPointerEvent/UIHitResult/UIPointerState/UINavigation/UIRawInputEvent 与 UIPointerPhase 按文档给出。
  模块：UIInputModule 抽象类 + StandardUIInputModule 默认模块（手柄死区 0.25、
  首次重复 0.4 秒、后续 0.1 秒、Submit/Cancel 取按下边沿），UIWorldContext.SetInputModule
  先 Reset 旧模块与路由器再替换；ProcessInput 排空注入的指针事件后处理平台事件，
  结束阶段把消费序列交给原生侧，UI 占有的键由此生效。
  Canvas.InjectPointer 只对离屏画布生效，事件在下一输入阶段排空。
  原生侧补齐视图快照：显示区域逻辑尺寸、视图像素原点与世界空间画布的相机矩阵
  （KeyEnum 同时加入 MetaGen 的导出枚举，托管侧据此判定方向键）。
  待下一项：控件事件（Clicked 等）与 UIEventDispatcher；ScrollBox 的“最近可处理该轴的容器”
  要等控件阶段。）

- [x] 事件：实现 UIEventDispatcher、UIEventBinding 与精确方法代理。
  （全部为 C#。UIEventDispatcher：FIFO 队列，派发一条时先让源更新状态与视觉并走持久化行顺序，
  再按开始派发时的订阅快照调用代码订阅；回调里产生的新事件入队尾，绝不递归；
  回调增删订阅从下一条事件起生效，期间注销的订阅这一条仍然执行；
  每次回调前重查源是否存活与代次是否相符，源失效即终止该源余下调用，单个监听器异常不阻断其它监听器；
  单帧上限 4096，超出清余项并报错，被丢弃的事件会通知源（OnEventDiscarded）以便撤销一次性状态。
  UIEventBinding：保存目标 Ens、目标组件类型名与方法名；解析用精确签名匹配
  （无参或 bool/float/int/vector2/string 之一），不做隐式转换，解析失败只停用这一条并报错。
  UIControl 增加持久化绑定列表与 RaiseEvent；事件源抽象成 IUIEventSource，
  因此这套顺序语义可以脱离原生宿主用假源验证。
  事件标识按文档给到 UIEventIds：Clicked=0、CheckedChanged=1、ValueChanged=2、
  ScrollChanged=3、SelectionChanged=4、TextChanged=5、Submitted=6，载荷用同一个定长结构承载。
  UIWorldContext 持有派发器，输入阶段结束时排空队列。
  新增用例 EventDispatcherTests：FIFO、回调内入队不递归、订阅快照、异常隔离、
  源失效终止余下调用、代次变化作废、单帧上限、丢弃通知。）
  UIControl 新增 GetEventBindingCount():int、GetEventBinding(int):UIEventBinding。
  InsertEventBinding(int,UIEventBinding)、SetEventBinding(int,UIEventBinding)、RemoveEventBinding(int) 返回 void。
  ComponentProxy 新增 FromComponent(Component):ComponentProxy? 与 DescribeMethods(Component):IReadOnlyList<BindableMethod> 静态入口。
  BindableMethod 保存 name:string、returnKind:InteropValueKind、parameterKinds:InteropValueKind[]。
  复用缓存与实际实例句柄；AOT 不用 Reflection.Emit/Expression.Compile，保留需要反射的成员。
  （已按此落地：UIControl 的五个按下标接口与 GetBindings/AddBinding/RemoveBinding 并存，
  内部仍是同一个持久化列表。ComponentProxy.FromComponent 从脚本实例取运行期句柄
  （原生组件包装没有句柄，返回空，那种情况用 Ens.GetNativeComponent）；
  DescribeMethods 读 ManagedTypeMetadataCache 的类型元数据并按名字与参数个数排序。
  UIEventBinding 的解析与调用改走这套元数据：签名匹配在编译期判定，
  调用走 ComponentMethod 句柄而不是 MethodInfo.Invoke，运行期不做反射扫描。
  控件检视面板的"方法"列据此换成下拉，目标解析不出来时才退回文本框。）

- [x] 控件：实现 UIControl 与 Button/CheckBox/RadioButton。
  （全部为 C#。UIControl 补齐事件路径：RaiseEvent 入派发器，派发时先更新状态与视觉、
  再走持久化绑定、最后触发代码事件；离焦清按下状态。控件附加网格接进帧构建：
  同节点图形之后、子节点之前绘制，独立片段号段与内容版本。状态色只改变提交时的状态乘子。
  Button：同目标 Up（仍处于按下状态）或 Submit 发 Clicked；取消、禁用、离焦都清按下状态。
  CheckBox：SetChecked 先更新值与标记可见性再排事件，同值不通知；
  新增运行时可见性覆盖（只影响绘制与命中，不改组件启用状态），覆盖带来源、来源销毁即撤销。
  RadioButton：groupRoot 为空取直接父、无父取画布根；选中时先更新全组标记，
  再按层级顺序给其它项发 false、最后给自己发 true；点击已选项保持选中；代码可设为 false 使组为空；
  配置里多项选中时保留配置、运行状态只让层级最前一项生效，不标脏场景。
  分组不建持久索引：每次按当前树现算，父级变化（新增 OnUIReparented 钩子，由节点重新挂接时触发）
  就把冲突再解一次，因此 reparent 不会留下过期登记；禁用与删除都会撤销视觉覆盖与捕获。
  待下一项：Slider/ScrollBar/ScrollBox。）

- [x] 控件：实现 Slider/ScrollBar/ScrollBox。
  （全部为 C#。新增 UIRangeMath：Normalize/Denormalize/Clamp/RoundToWhole，
  零区间归一为 0、还原取下界，整数模式中点远离零舍入。
  UILayout 新增两种运行期覆盖：SetDrivenRect（收父级空间，替换解析结果）与
  SetDrivenOffset（叠加在解析结果之上）；都由控件持有来源，摘除时撤销。
  滑条与滚动条的拇指/填充用矩形覆盖驱动，滚动容器的内容用平移覆盖驱动，
  因此反复驱动不会累积，也不写回子节点的锚点与尺寸配置。
  Slider：缺 track/thumb 时保留值但禁用指针拖动；点击轨道直接跳转、拖动按指针位移取值；
  导航步长为整数模式 1、否则区间长度的十分之一；同一时刻只接受一个拖动指针。
  ScrollBar：value/pageSize 限于 [0,1]，pageSize=1 时值固定为 0；
  点击拇指外侧按一页移动，拖动按 trackLength-thumbLength 换算。
  ScrollBox：SetScrollOffset/AdvanceInertia/SynchronizeBars 齐备；
  惯性每帧 offset += velocity*dt、velocity *= exp(-deceleration*dt)，到边界该轴速度归零、
  绝对值小于 0.1 归零，dt≤0 不积分；pageSize = clamp(viewport/content,0,1)，内容为空取 1；
  bar 与 offset 双向换算、内部同步带抑制标志，一次最终变化只发一次 ScrollChanged；
  修改 bar 引用先退订旧引用再订阅新引用。
  帧构建对滚动内容加一层矩形裁剪，只包围 content 子树，独立滚动条不受影响。
  驱动矩形的空间约定：SetDrivenRect 收父级空间，解析时按 pivot 换算到节点局部空间。）

- [x] 控件：实现 ComboBox 与 UIComboPopup。
  （全部为 C#。公开 API 齐备：GetOptionCount/GetOption/InsertOption/SetOption/RemoveOption/
  ClearOptions/Open/Close，越界抛 ArgumentOutOfRangeException，null 文本归一为空串。
  选择语义：SetSelectedIndex 允许 -1；删除当前项后顶上来的接位、越界取最后一项、空表为 -1；
  删除前面的项保持原条目（只前移下标）；选项一变立即关闭弹层。
  弹层是同画布下的 DontSave 临时子树（根 + 滚动容器 + 内容 + 每项一个按钮），不创建嵌套画布；
  默认向下展开，下方不足且上方更大就向上，最后夹紧画布根矩形。
  Close 先按序号重建委托退订按钮事件，再销毁临时对象，最后把焦点还给仍可交互的源控件；
  Open/Close 重复调用无副作用。每个画布同时只允许一个弹层，换世界与源控件停用/销毁时统一收起。
  控件事件经派发器：RaiseEvent 入队、派发时先更新状态与视觉、再走持久化绑定、最后触发代码事件。）

- [x] 控件：实现 TextField 与 UITextEditor。
  （全部为 C#。UITextEditor 是纯逻辑编辑模型：对外一律用 Unicode 标量下标，
  内部改动走 UTF-16 映射，任何操作都不切断代理对；归一化把 CRLF/CR/LF/Tab 变成空格、
  非法代理项替换成 U+FFFD；最大长度按标量截断（超出时整块丢弃该标量，不切高位代理）；
  SetSelection/MoveCaret/ReplaceSelection/DeleteBackward/DeleteForward/BeginComposition/
  UpdateComposition/CommitComposition/CancelComposition 齐备，令牌不符的更新与提交忽略，
  Commit 成功后令牌作废、重复提交忽略；只读允许选区与复制，拒绝改值、剪切与粘贴。
  TextField：SetText 共用输入归一流程，同值不通知；占位文本仅在实际文本为空且没有组合时显示，
  不参与选择与提交；绘制顺序为背景→选区→文本/组合→光标，全部被矩形内裁剪约束；
  光标周期 1 秒、前半个周期可见，输入与移动都会重置；取得焦点时把会话令牌与光标矩形交给平台输入法，
  离焦、停用、销毁时释放会话。
  StandardUIInputModule 接管编辑按键（方向/Home/End/Shift 选区/Backspace/Delete/Ctrl+A/C/X/V/Enter）
  与输入法事件（组合开始/更新/提交/取消按令牌驱动），被吃掉的序列确认消费；
  组合期间回车交给输入法、不发 Submitted，非组合回车发一次。
  新增用例 ControlLogicTests：归一化数学与文本编辑模型（标量映射、截断不切代理对、
  组合令牌语义、只读规则）——其中"截断不切断代理对"当场抓到一个真 bug 并已修。
  待编辑器阶段：创建菜单、属性编辑器、事件表。）

- [x] 编辑器：实现创建菜单、属性编辑器、事件表与撤销公共入口。
  UICreationMenu.Register():void；Create(EnsContext,UIWidgetKind):void。
  UIWidgetKind 按创建菜单表的展开顺序从 0 赋值，CheckBox 与 RadioButton 各占一项。
  创建与引用配置合为一条事务；失败全撤销；属性表不绕过 PropertyDocument。
  新增菜单注销能力，程序集卸载移除其注册项，避免重复菜单与强引用泄漏。
  （全部为 C#。创建菜单 13 项按展开顺序编号，CheckBox/RadioButton 各占一项：先建节点与
  唯一名、再挂全部组件并配引用，失败按逆序删掉整批，成功才由 RecordAction 记一条撤销。
  注销走 EnsContextMenuRegistry.Unregister/UnregisterAssembly/Clear，
  UIEditorRegistration 在模块初始化时登记、在 ManagedAssemblySession 卸载处理器里摘除，
  连续重载不会留下重复菜单项。属性表全部经 PropertyDocument 的 DrawProperty/CustomEditor，
  自定义行之外的字段仍由默认检视面板绘制。事件表在 UIControlEditor 里：四列一行的
  事件下拉/目标/目标组件/方法加删除，每行改动各记一条撤销，无效引用与无匹配签名只标出来、
  不自动删除；添加绑定同样可撤销。撤销公共入口是 EditorPropertyHistory.RecordAction。）

- [x] 编辑器：实现 UILayoutGizmos 与像素预览。
  UILayoutGizmos.OnSceneGui() 以起始快照反算字段，驱动轴禁止编辑。
  UIPreviewController 实现公共合同，UIPreviewPanel 通过注册表调用。
  EditorGUI.DrawTexture 解析 Texture2D 对象身份，处理线性预乘纹理显示。
  预览隐藏释放输入占有；调整分辨率使旧命中快照失效。
  （UILayoutEditor.OnSceneGui → UILayoutGizmos.OnSceneGui：画矩形轮廓与四个角点手柄、
  一个枢轴手柄。命中用 ImGui 条目而不是自算距离，拖住时活动条目会挡住场景拾取与相机操作；
  拖动量先按按下时锁定的投影比例从屏幕位移换算回布局空间，再由起始快照反算
  （ApplyCornerDelta 让被拖角跟着走、对角不动，尺寸与偏移一起改），中途绝不用中间值累加。
  被驱动轴只提示 owner、不写字段；节点销毁或投影不可用时 Cancel 回基线且不记历史。
  为这条路径新增了公共原语 EditorSceneHandles（ProjectPoint/Draw）与两个原生只读槽位
  EditorGuiSetCursorScreenPos、EditorGizmoProjectPoint，编辑器 ABI 表因此升到 85 与 6 槽，
  EditorManagedApi 随之到 168 槽。预览侧：UIPreviewController 实现 IUIPreviewProvider，
  面板只经注册表调用；离屏画布返回它的输出纹理并在此出图，屏幕画布没有独立纹理时面板
  说明像素在场景面板里。DrawTexture 由原生按运行时 ID 取 Texture2D 再问
  RenderSystem::GetTextureId，线性预乘纹理在 ImGui 直通混合下偏暗，已在实现与面板上写明。
  隐藏、关注入、失焦都调 CancelInput 收回指针；改分辨率 MarkAllInputDirty 使旧命中失效。
  交互注入：面板在纹理上盖一层等大的不可见拖动区，按活动/悬停状态转发 Down/Move/Up/Scroll
  （滚轮换号后交给 UI 约定的方向），开关经合同新增的 SetInputEnabled 同步给提供者，
  显示尺寸取提供者的当前分辨率而不是输入框里还没应用的文本。）

- [ ] 发布：更新工程、SDK、版本与文档。
  所有新增原生文件加入 vcxproj/filters，链接字体库与 imm32/comctl32。
  发布 GUI 包、shader、授权、API；生成器刷新绑定与元数据。
  Version.h 设 37；更新 BuildAndPackaging、ProjectConventions、ScriptSystem。
  验证覆盖包不被 SDK 刷新覆盖，纯 C# 修改不重跑原生工具链。

- [ ] 验收：新增 Build/Tests/RetainedGUI.ManagedTests 的布局/生命周期用例。
  （用例工程已建立，覆盖中心锚点、拉伸、pivot、负 sizeDelta、画布两种缩放、容器主轴与交叉轴、
  三种网格约束、空网格、四边形缠绕、片段切分、九宫格三级缩放。
  仍需真实世界的项——作者位移、驱动轴、布局不标脏场景、循环组件引用、禁用与整树删除、
  无 Canvas 与非法嵌套、世界与会话代次切换——随各阶段补入。）
  覆盖中心锚点、拉伸、pivot、作者位移、驱动轴、空网格、三网格约束、文字 fitHeight。
  验证布局不改持久化字段、不标脏场景；循环组件引用在加载/复制后正确。
  验证禁用、整树删除、无 Canvas、非法嵌套、世界与会话代次切换。

- [ ] 验收：使用真实控件测试输入与事件。
  覆盖同帧 Down/Up、触摸去重、两指针、ScrollBox 取消子 Button、失焦、手柄重复。
  覆盖 IME 重复提交、代理项边界、选区替换限长、剪贴板归一与只读。
  覆盖同类型两组件的精确事件目标、删除源/目标、换世界、异常、4096 上限。
  运行 CLR 与 NativeAOT 同一组持久化事件断言。

- [ ] 验收：新增字体与 GPU 集成用例。
  字体验证三模式 advance、TTC 越界、cooked 往返、UV 稳定、页代次、无回退、热缓存。
  GPU 实际出图并回读：无相机 Overlay、WorldSpace 遮挡、旋转裁剪、两层 0.5 Alpha 得到 0.25。
  验证九宫格小尺寸、线性混合、预乘 Offscreen、依赖环、resize、多相机共享 WorldSpace 网格。
  反复创建/销毁后 nativeContext、framebuffer、纹理引用计数回到基线。

- [ ] 验收：完成 C# 扩展、重载与最终操作检查。
  示例 RingGraphic:UIVisual、WrapBox:UILayoutGroup、RepeatButton:Button 只增加 C#。
  三者完成挂载、编辑、保存、复制、Undo/Redo、热重载和 Player 构建。
  连续重载 20 次确认会话可回收、菜单/回调无重复；暖缓存连续 120 帧满足统计判据。
  最终在编辑器创建全部控件，使用鼠标、键盘、触摸、手柄、中文输入逐项验证。
