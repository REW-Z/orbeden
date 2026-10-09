# 需求/目标

本文定义 Orbeden RetainedGUI 的实现边界、公共接口、算法、编辑器接入、程序集归属与验收。
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
| 字体 | TTF、OTF、TTC；Bitmap、SDF、MSDF；预烘焙 Atlas＋动态补字 |
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
- UI 由引擎程序集提供，游戏工程不编译 UI 源码，只派生与扩展控件。
- 修改控件、排版、布局与路由不运行 C++ 编译，不改变原生函数表。
- 自定义组件能挂载、保存、复制、Undo、热重载并运行于 NativeAOT。
- 暖缓存且画面未变时，布局/几何重建、字形生成与顶点上传计数均为零。
- 无 Camera 时 Overlay/Offscreen 正常；WorldSpace 使用场景深度遮挡。
- 事件回调删除源、目标或请求换世界时不访问失效对象。

UGUI 的借鉴点是托管图形生成、分阶段重建与原生渲染桥接，不复制 Unity 的底层类型。
参考： https://github.com/Unity-Technologies/uGUI/blob/main/com.unity.ugui/Runtime/UGUI/UI/Core/Graphic.cs
参考： https://github.com/Unity-Technologies/uGUI/blob/main/com.unity.ugui/Runtime/UGUI/UI/Core/CanvasUpdateRegistry.cs
参考： https://github.com/Unity-Technologies/uGUI/blob/main/com.unity.ugui/Runtime/UGUI/UI/Core/Layout/LayoutRebuilder.cs

路径缩写只用于本文；每个主类型与文件同名，紧密关联的 POD/枚举放在对应主类型文件。

| 缩写 | 目录 |
|---|---|
| CoreCS | OrbedenCore/Managed/OrbedenCore.CSharp/ |
| UI | CoreCS/UI/ |
| Native | OrbedenCore/Src/ |
| UIEditor | OrbedenEditor/Managed/Orbeden.Editor/UI/ |
| EditorCS | OrbedenEditor/Managed/Orbeden.Editor/ |

# 关于托管组件基础系统

所有 UI 组件继承 Orbeden.Script，每实例对应独立原生 Script 宿主。
原生宿主只保存身份、managedTypeName、enabled 与字段快照，不保存控件状态机。
示例：同一 Ens 上为 Transform、Script→UILayout、Script→Image、Script→Button。
场景使用 Component type="Script" 与完整 managedTypeName，例如 Orbeden.Button。
稳定组件路径用于持久化引用；ObjectId、包装指针与数组下标只用于当前进程。

**两阶段实例构造**
1. 构造全部可解析宿主并登记包装；构造函数仅初始化字段，不调用扩展回调。
2. 应用字段并解析引用，随后统一附着和发送初始活动通知。
循环组件引用在步骤二通过已登记包装解决，不递归构造对端。
找不到托管类型时保留宿主和原字段，显示 Missing Script；不删除数据。
单个构造失败断开该实例；引用它的字段保留未解析路径，不阻塞其他实例。

CoreCS/IManagedComponentLifecycle.cs：
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
保存、复制、Prefab、进入 Play、程序集重载前调用 FlushHostFields，序列化边界另调用 IManagedComponentLifecycle.OnComponentBeforeSerialize()。
普通 setter 不逐次调用原生字段写入；派生矩形、选区、捕获、网格、委托不持久化。
Inspector 事务先转换全部值，成功后一起应用，最后调用一次 FieldsChanged。
转换失败整批拒绝；验证失败回滚该事务，不保存部分值。
加载配置非法时保留原数据，标记该组件不可运行并显示错误；用户修复后重新验证。
未解析引用单独记录字段/元素对应的稳定路径；未被明确编辑为 null 前不能刷新成空路径。

托管组件约束使用 ComponentConstraintAttribute(Type exclusiveBaseType)，Inherited=true、AllowMultiple=true。
同一 Ens 最多一个可赋值给 exclusiveBaseType 的组件；检查范围包括 C# 派生类型。
UniqueComponent 用于 UILayout、Canvas、Mask；家族互斥用于 UIVisual、UIControl、UILayoutGroup。
用户添加时补齐依赖，失败逆序撤销新增组件；场景加载只验证、不插入依赖。
单删被依赖组件被拒绝；整 Ens 销毁放行；Undo/复制恢复在整批对象建完后验证。
字段协议含 vector2，标量序列化名称为 vector2，值为 invariant 的 x y。
一维集合采用 UTF-8 字节长度前缀协议；不增加任意托管对象图序列化。

# 关于程序集与会话系统

UI 运行时代码位于 `OrbedenCore.CSharp` 的 `UI/` 下，命名空间为 `Orbeden`；编辑器部分位于 `Orbeden.Editor` 的 `UI/` 下，命名空间为 `OrbedenEditor`。
两者随 SDK 以程序集发布：游戏运行时、游戏脚本与 `<Game>.Editor` 引用它们，不编译任何 UI 源码。
没有源码包、包版本合同与项目覆盖位置；游戏开发者派生与扩展控件，不改 UI 实现。
UI 运行时不引用 `Orbeden.Editor`；编辑器源码不进入 Player/AOT。
Core 的 MetaGen 只处理底层原生能力；UI 的 C# 类型不参与原生反射生成。

CoreCS/ManagedAssemblySession.cs：
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

类型解析顺序为游戏程序集→`<Game>.Editor`→`OrbedenCore.CSharp`；UI 类型由第三级命中。
原生侧与序列化只认 `managedTypeName` 字符串，不含程序集限定名，因此 UI 类型所在程序集与场景数据无关。

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

CoreCS/IManagedFrameSystem.cs：
```csharp
void AttachWorld(ulong worldRevision, bool editorMode);
void ProcessInput(float deltaTime);
void PrepareRender(float deltaTime);
void DetachWorld();
```
工厂使用 ManagedFrameSystems.Register(id, factory)（会话层，随程序集卸载清空）或 RegisterBuiltin(id, factory)（常驻层）。
UI 用常驻登记：`UIRuntimeBootstrap` 在引擎绑定初始化时登记 id="Orbeden.RetainedGUI"，模块初始化只执行一次，登记不能被会话清理带走。
Unregister(string id) 仅在世界分离后执行；重复 id 报错，构造顺序为 id 的 ordinal 升序。
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
offset 与 Transform 局部 X/Y 使用同一份平移，Z 使用 Transform 局部 Z；GetAnchoredPosition/SetAnchoredPosition 提供三维入口。

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

**场景编辑规则**
SceneView 的 Rect 工具（T）修改 UILayout 的 offset 与 sizeDelta；Move/Rotate/Scale（W/E/R）修改 Transform。
普通 UI 的最终位置是“父矩形中的锚点落点 + UILayout.offset + Transform.localPosition”。
未拉伸时 Width/Height 等于 sizeDelta；拉伸时矩形尺寸等于锚点跨度加 sizeDelta，Inspector 显示相应边距。
localScale 在布局完成后缩放网格、文字及子树，不修改 Width/Height，也不会让文字按缩放后的宽度重新换行。
常规排版保持 localPosition=(0,0,0)、localScale=(1,1,1)，用 Rect 调位置与尺寸；额外位移或缩放动画使用 Transform。
父布局组驱动的子矩形应修改布局组或设置 ignoreLayout。
Overlay 根 Canvas 的 Transform 只参与 SceneView 空间预览，PIE 的大小由 Canvas 缩放配置与视口决定；
WorldSpace 根 Canvas 的 Transform 则决定真实世界位置和尺寸。
活动 Canvas 边框常显，子 UI 边框只在选中时显示；共享手柄入口限制 UILayout 只能在 Rect 模式交互。
点击场景时 UI 场景扩展按 Image/Text 的实际预览变换求射线和平面交点，再检查解析矩形与祖先遮罩；运行时 RaycastTarget 不参与编辑器选择。
先过滤被更近 MeshRenderer 遮挡的 UI，再按 Ens 树深度优先选取更“叶子”的节点，同层级比较相机距离。
离屏画布显示其空间边框，实际离屏图元不参与 SceneView 拾取。

**预览空间**
Overlay 画布在场景中的大小由画布根 Transform 的缩放给出，新建画布写 0.01，与 WorldSpace 同刻度；预览矩阵只再按枢轴居中一次，不再另乘缩放。
逻辑单位到场景世界单位的换算因此只有一个来源，UI 子节点的世界矩阵就等于它在场景里的绘制位置——W/E/R 手柄、双击聚焦（F）、选择包围盒与绘制三者对齐。
居中偏移同时写进派生位置（见上），两边的偏移同源。
旧项目的 Overlay 画布根缩放是 1，需在画布根上手工设为 0.01。
根逻辑矩形仍为 min={0,0}、size=logicalSize，屏幕正交投影与字形光栅缩放仍不读根 Transform。

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

实现约定：`SetVerticesDirty()` 只刷新几何与命中，自定义图形改变期望尺寸时另调 `SetLayoutDirty()`；内置 Text 内容与字号变化已同时标记布局。
`SetMaterialDirty()` 只刷新最终绘制状态，修改器参数和动画变化后主动调用；设置图形颜色、材质或 Image 纹理会自动标记。
最终状态始终从基础片段重算，静态帧复用；Material 纹理槽依赖仍每帧读取。
组件扩展缓存使用集合版本，覆盖同数量替换、重排与启停；图形重建期间产生的新脏请求保留到下一轮。
派生图形可重写 Raycast 与网格生成；生命周期注册仍由 UIElement 实现。
扩展入口为 IUIMeshModifier（`ModifyMesh(UIMeshBuilder)`）、IUIMaterialModifier 与 UIVisual 的 PopulateMesh 重写。

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

RetainedGuiApi 表头为 uint version=3、uint structSize；随后指针槽顺序固定：
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
| 19 | int ReadPrebakedAtlas(ulong context, int fontObjectId, ulong fontRevision, Span<byte> output) |

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
GetRetainedGuiApi 返回进程期稳定只读表；原生 UI context 生命周期独立。
托管侧由 OrbedenCoreRuntime.RetainedGuiApi 保存该指针，RetainedGuiNative 保存类型化镜像并校验表版本与 structSize。
SubmitCanvas 暂存本帧，EndFrame 原子发布；长度、索引、矩阵、裁剪栈全部校验成功才交换。
提交失败停止该上下文当前帧绘制并清命中快照，不显示半帧。
原生复制上传数据后返回；mesh 更新才上传顶点；无变化网格不重复上传。
原生帧持有资源 Ref；收集器把 context 的 Ref 清单作为资源根。
字体批量请求期间同样固定 Font；页面纹理由托管缓存登记资源根，回收时解除，不能依赖 C# 包装被 GC 来保活。
DestroyContext 释放帧、网格、派生位置、视图快照和资源根，重复销毁无副作用。
首版在主线程提交与渲染；GPU 删除依托后端资源释放，不从终结器执行 GL 调用。

# 关于字体与文字系统

版本 62：预烘焙字符文本改为 `.txt` 原始文件引用框，支持拖放、文件选择、清空与缺失显示；Import Settings 保存 Content 相对路径 `prebakeTextFile`，不保存字符文本。导入读取 UTF-8（可带 BOM）文件并记入源文件依赖，跳过换行等控制字符，按 Unicode 标量去重和排序；同一字形的多个字符映射共用图集区域。字符文件变化参与编辑器缓存和运行态重导指纹。字符文件只参与导入，Font 运行时读取已烘焙载荷，文件内容不会被修改。

版本 61：字体导入预烘焙常用字符，运行时读取烘焙页并在剩余空间动态补字。源文件 Import Settings 配置 Raster Mode、Atlas Resolution、Bake Pixel Size（px/em）、Prebake Character Set 和自定义字符文本。Bitmap 的字形宽高按烘焙像素字号及字体比例生成，不固定为等宽单元；不同 Bitmap 投影字号分别缓存，距离场字形共用指定采样密度。图集页及字形表作为 Font 的不可变载荷持久化，运行时复制像素后恢复装箱游标；补字只修改世界所属纹理。图集回收或 PIE 切换后重新加载载荷，沿用版本 60 的失效和重建边界。

版本 60：FontAtlasCache 的共享实例跨世界保留，图集页及字形状态绑定当前 UI 上下文；进入和退出 PIE 都清理旧页并推进缓存版本。Text/TextField 检查缓存版本、图集纹理存活与临时失败状态后重排，字体图集失效时不绘制实心替代矩形。输入计数查询须提供 textBytes 输出指针，无文本输入不要求分配文本缓冲。PIE 世界重建只使运行时缓存失效，不改写世界文件。

分工：Font/FontRasterizer/FontAtlasBaker 用 C++，UITextLayout/FontAtlasCache/Text 用 C#。
原生运行时负责字体字节、字体面、glyphIndex、度量、kerning、轮廓光栅化；导入阶段由 FontAtlasBaker 烘焙和装箱，动态装箱由 C# 负责，换行在 UITextLayout。
Font 位于 OrbedenCore/Src/Runtime/Object/Font.h/.cpp，其余原生字体服务位于 OrbedenCore/Src/Runtime/Fonts/。
依赖固定 FreeType VER-2-14-3、msdfgen v1.13；保留 FTL 与 MIT 授权文件。
https://github.com/freetype/freetype/releases/tag/VER-2-14-3
https://github.com/Chlumsky/msdfgen/releases/tag/v1.13
FreeType 关闭外部 ZLIB/BZIP2/PNG/HarfBuzz/Brotli；msdfgen 使用 core-only。
msdfgen 不构建 standalone、OpenMP、Skia、安装目标；通过 FT_Outline_Decompose 转换轮廓。

Font 持久化 sourceBytes:List<uint8>、faceIndex:uint=0；字体元数据从字节解析。
元数据为 familyName、styleName、unitsPerEm、ascender、descender、lineHeight。
revision 为运行时 ulong，重新导入成功后递增；Font 保存预烘焙 Atlas 的像素与字形表，不保存 FT_Face 或 GPU 句柄。
导入 .ttf/.otf/.ttc；越界 faceIndex 导入失败；Player 使用打包字节，不读系统字体。
cooked 字体载荷版本 3 保存 faceIndex、rasterMode、atlasSize、distanceFieldSize、distanceFieldRange、字体字节及预烘焙载荷；先校验和解析，再替换资源。版本 1/2 没有预烘焙载荷，继续动态生成；版本 1 使用默认导入设置。字体字节保留供动态补字使用。
原始字体在 ProjectPanel 下展开为 Font 对象，与 PNG 展开为 Texture2D 一致；源文件 Inspector 显示 Objects 和 Import Settings。
导入设置写入源文件旁的 .resinfo，Apply 重新导入并保持 Font 对象身份，推进 revision、刷新字体面及文字几何。
字体仍属于外部原始格式，导入结果是 Font 对象；编辑器不新增 .orbfont 文件格式，Cooked 产物为 .orbo。
FontRasterMode={Bitmap=0,SDF=1,MSDF=2}；Atlas 通道分别为 R8、R8、RGB8，均为线性。

字形度量使用未 hint 字体单位；按 fontSize/unitsPerEm 缩放，三模式 advance 相同。
Bitmap 光栅字号取四舍五入并限于 [1,512]，留白 1 texel，处理正/负 pitch。
位图字号 = 逻辑字号 × 光栅缩放；Overlay/Offscreen 使用 Canvas scale，WorldSpace 光栅缩放固定为 1。
WorldSpace 的布局、Image/Text 网格、控件附加网格与文字排版均与摄像机无关。
相机移动、增删与分辨率变化不重建 WorldSpace 几何；需要跨距离保持清晰时使用 SDF/MSDF，shader 按 UV 导数处理边缘。
每台相机分别发布呈现快照，命中检测使用对应相机矩阵、视口和深度，不生成相机网格变体。
Font 导入设置默认 rasterMode=Bitmap、atlasSize=1024、prebakePixelSize=16、prebakeCharacterSet=BasicLatin、distanceFieldSize=64、distanceFieldRange=4。
预烘焙字符集支持 BasicLatin（U+0020–U+007E）、Latin1（再加 U+00A0–U+00FF）、Custom（Text File Characters）与 None；非 None 模式追加 `prebakeTextFile` 引用的 UTF-8 `.txt` 文件内容，去重后按码点排序，忽略控制字符，字体未包含的字符报告 Warning。Import Settings 的 Prebake Text File 仅接收 Content 内的 `.txt` 文件，支持 UTF-8 BOM，缺失显示 Missing。`.resinfo` 只保存文件引用路径，字符文件作为导入依赖参与缓存验证；旧 `prebakeCharacters` 文本设置不再使用。
预烘焙载荷内部版本为 1，小端存储：头部为 version、rasterMode、pixelSize、atlasSize、unitsPerEm、pageCount、glyphCount（各 uint32）；每页为 width、height、channels、cursorX、cursorY、rowHeight、byteCount 后跟像素；每字形为 scalar、glyphIndex、pageIndex（空轮廓为 -1）、x、y、width、height、originX、originY，随后五个 float32（advance、bearingX、bearingY、width、height）。字号单位为 px/em，Bitmap 支持 1–512，距离场支持 16–256；字体比例和留白共同决定字形实际像素宽高。烘焙图集像素限制为每字体 64 MiB，超限导入失败。
atlasSize 支持 256/512/1024/2048/4096；distanceFieldSize 支持 [16,256] pixels/em；distanceFieldRange 支持 [1,32] texel。
SDF/MSDF 从 Font 读取像素密度和 range，padding=ceil(range)+2、edge angle=3 radians、seed=0；Bitmap 在投影字号与烘焙字号一致时使用烘焙字形，其他字号动态生成。
空格只缓存度量，不分配 Atlas；空轮廓同样不生成像素。

Atlas 键：fontObjectId、fontRevision、glyphIndex、rasterMode、bitmapPixelSize。
SDF/MSDF 的 bitmapPixelSize=0；页面保存 pageId、generation、texture、row cursor、lastUsedFrame。
普通页边长由 Font.atlasSize 指定并限制到设备上限；字体对象及 revision 各自分配页面。逐行装箱，宽不足换行，高不足换页，不旋转、不移动已有字形。
烘焙页按实际使用的字形加载，上传后恢复游标，剩余空间与动态字形共用。LRU 回收时解除载荷索引对运行时页的引用，下次使用重新上传原始烘焙像素；动态图集内容不写回字体载荷。设备不支持烘焙页尺寸时按动态路径生成字形。
超大字形分配能容纳它的二次幂独立页；超设备上限使用缺字图形并记录错误。
64 MiB 为软预算：先收集本帧全部视图字形并固定命中页，再生成冷字形。
只驱逐未固定的 LRU 页；当前工作集超过预算允许超出，禁止本帧反复驱逐。
页面回收推进 generation，依赖几何失效；GPU 资源待原生帧释放引用后销毁。
新增字形只上传占用矩形；首版在主线程同步生成，不承诺冷缓存无耗时峰值。
缓存为世界无关的常驻实例：换世界只换原生上下文，不重新光栅化；页纹理属核心侧资源，与程序集同生命周期。

缺字：glyphIndex=0 时不查备用字体、不用 .notdef，生成统一空心方框。
方框 advance=0.6em、宽=0.5em、高=0.8em、线宽=0.05em；Font=null 同样处理。
方框由 C# 四条矩形组成，避免缺字仍依赖字体光栅化；行高使用 1em。

默认字体：Text 与 TextField 的 Font 字段为空时，实际排版使用 `Builtin/Fonts/Default.ttf//Font/Main`（Cubic 11／俐方體11號 1.500），默认以 Bitmap 导入。
源文件路径为 `Builtin/Fonts/Default.ttf`，引用保存导入后的 Font 对象 Key。
组件字段仍保留空值，默认资源按世界延迟加载并缓存；显式字体优先，输入框正文、占位文字、组合文字共用该规则。
字体按依赖锁文件的固定提交与哈希还原，字节未修改；字体及完整 OFL 许可证随 Builtin 内容发布。
旧项目需更新 Builtin/Fonts 并重新导入，Player cook 会收录字体资源。

Text 配置：font:Font?=null、text:string=""、fontSize:float=16；rasterMode 与图集参数读取有效 Font，无组件级导入设置。
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

TextField：text=""、font=null、fontSize=16、maxLength=0、readOnly=false；字形模式及图集参数读取有效 Font。
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
UI 运行时不引用 Editor 的历史与面板类。

接入方式：`UIEditorRegistration.Register()` 在引擎编辑器初始化时登记一次，创建菜单、字体资源检视、场景扩展与预览提供者都是常驻项，不随游戏程序集卸载。
控件编辑器由 CustomEditorRegistry 扫描 `Orbeden.Editor` 时按 `[CustomEditor]` 自动接入；`RegisterBuiltins()` 同时登记核心程序集，UI 组件才能在按类型名解析时查到。
Inspector 的可添加组件列表同时收集核心程序集与游戏程序集里可添加的托管脚本类型。
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
SDK 随程序集发布 UI 能力：`OrbedenCore.CSharp.dll` 供游戏运行时与脚本引用，`Orbeden.Editor.dll` 供编辑器与 `<Game>.Editor` 引用；不再发布 UI 源码。
UI 的公开 API 由 Core 的程序集与 XML 文档经 OrbedenDocGen 投影到 Docs/Manual/Api，按命名空间与完整类型名去重。
改 UI 代码不触发 C++ 或原生 Binding 生成。

统计项：Layout、TextLayout、GlyphRaster、AtlasUpload、Geometry、Clip、Input、EventDispatch、NativeSubmit、Draw。
计数项：布局节点、几何重建、冷字形、上传字节、Atlas 字节、批次、裁剪目标峰值、深度回读。
性能验收使用暖缓存、固定分辨率、无属性修改场景，连续采集 120 帧。
布局重建/几何重建/冷字形/顶点上传须为零；帧描述和 draw 允许继续提交。
连续重载 20 次确认会话可回收、菜单与回调无重复，UI 每次重载后仍工作。
不以语言文件数量比例验收，以“新增控件不改原生”作为架构判据。
