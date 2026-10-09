using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 一个世界实例的 UI 运行上下文。进程内同一时刻只有一个活动上下文，
/// 世界附着时按托管宿主批量建立节点索引，分离时整体释放。
/// 结构变化来自原生世界通知；索引不每帧扫描全部原生组件。
/// </summary>
public sealed class UIWorldContext : IManagedFrameSystem
{
    /// <summary>等待处理的场景结构记录上限；超出后改为整表重建。</summary>
    public const int MaxPendingChanges = 65536;

    /// <summary>内置默认字体的资源键。</summary>
    public const string DefaultFontResourceKey = "Builtin/Fonts/Default.ttf//Font/Main";
    private Font? defaultFont;
    private bool defaultFontLoadAttempted;

    /// <summary>加载并缓存当前世界使用的默认字体。</summary>
    public Font? GetDefaultFont()
    {
        if (defaultFont is { IsAlive: true }) return defaultFont;
        if (!ReferenceEquals(defaultFont, null)) defaultFontLoadAttempted = false;
        if (defaultFontLoadAttempted) return null;
        defaultFontLoadAttempted = true;
        defaultFont = Resources.Load<Font>(DefaultFontResourceKey);
        return defaultFont;
    }

    private readonly Dictionary<EnsId, UINode> nodes = [];
    private readonly List<Canvas> canvases = [];
    private readonly HashSet<UILayout> layoutDirty = [];
    private readonly HashSet<UIElement> geometryDirty = [];
    private readonly List<UIElement> rebuildingGeometry = [];
    private readonly HashSet<UIElement> inputDirty = [];
    private readonly List<UISceneChange> pendingChanges = [];

    //本帧的画布顺序：离屏画布按输出纹理依赖排在前，其余按 sortOrder 跟随。
    private readonly List<Canvas> frameOrder = [];
    private readonly List<Canvas> offscreenCanvases = [];
    //循环依赖里的画布：清透明但不画内容，避免同一次绘制读写同一张纹理。
    private readonly List<Canvas> suppressedCanvases = [];
    //排序与找环的中间量，逐帧复用：纹理对象 ID 到离屏画布下标，以及依赖图本身。
    private readonly Dictionary<int, int> canvasProducers = [];
    private readonly List<HashSet<int>> canvasEdges = [];
    private readonly List<int> canvasInDegree = [];
    private readonly List<int> canvasOrder = [];
    private readonly List<int> canvasDependencies = [];
    private readonly List<int> cyclePath = [];
    private readonly HashSet<int> cycleOnPath = [];
    //宿主显式设置的画布视口：这些画布不再被视图快照覆盖。
    private readonly HashSet<Canvas> explicitCanvasViewports = [];
    //读取视图快照的复用缓冲。
    private UIView[] viewBuffer = new UIView[8];

    //视图快照缓冲；命中检测与画布视口刷新共用同一份。
    internal Span<UIView> ViewBuffer => viewBuffer;

    //事件派发器与输入侧；模块可以替换，派发器与路由器随上下文长存。
    private readonly UIEventDispatcher eventDispatcher = new();
    //输入侧：射线检测、路由器与输入模块。模块可以替换，路由器随上下文长存。
    private readonly UIRaycaster raycaster;
    private readonly UIInputRouter inputRouter;
    private UIInputModule inputModule = new StandardUIInputModule();
    //本阶段读到的原始事件与文本池，逐帧复用。
    private UIInputRecord[] inputRecords = new UIInputRecord[16];
    private byte[] inputText = new byte[256];
    private UIRawInputEvent[] rawEvents = new UIRawInputEvent[16];
    //离屏画布注入的指针事件；下一次输入阶段排空。
    private readonly List<UIPointerEvent> injectedPointers = [];
    //本阶段读完的原始事件条数。
    private int rawEventCount;

    private readonly UIFrameBuilder frameBuilder;
    private RetainedGuiBridge? bridge;
    private ulong worldRevision;
    private ulong managedGeneration;
    private ulong frameCounter;
    private bool editorMode;
    private bool fullResync;
    private bool worldAttached;

    /// <summary>创建上下文；帧构建器只产出数据，世界附着后才有内容。</summary>
    public UIWorldContext()
    {
        frameBuilder = new UIFrameBuilder(this);
        raycaster = new UIRaycaster(this, frameBuilder);
        inputRouter = new UIInputRouter(this, raycaster);
    }

    //每个画布至多一个打开的下拉弹层；换世界或分离时统一关闭。
    private readonly Dictionary<Canvas, UIComboPopup> comboPopups = [];

    /// <summary>登记一个弹层；先关掉同画布上的旧弹层。</summary>
    internal void RegisterComboPopup(Canvas? canvas, UIComboPopup popup)
    {
        if (canvas == null) return;
        CloseComboPopups(canvas);
        comboPopups[canvas] = popup;
    }

    /// <summary>注销一个弹层。</summary>
    internal void UnregisterComboPopup(UIComboPopup popup)
    {
        foreach (KeyValuePair<Canvas, UIComboPopup> entry in comboPopups)
        {
            if (!ReferenceEquals(entry.Value, popup)) continue;
            comboPopups.Remove(entry.Key);
            return;
        }
    }

    /// <summary>关掉一个画布上的弹层。</summary>
    internal void CloseComboPopups(Canvas? canvas)
    {
        if (canvas != null && comboPopups.TryGetValue(canvas, out UIComboPopup? popup))
        {
            comboPopups.Remove(canvas);
            popup.Close();
            return;
        }
        if (canvas != null) return;

        //画布为空：全部关掉。
        foreach (KeyValuePair<Canvas, UIComboPopup> entry in comboPopups) entry.Value.Close();
        comboPopups.Clear();
    }

    //当前持有文本输入的控件与它的会话令牌；离焦时令牌作废。
    private TextField? textFocus;
    private ulong textSession;
    private ulong nextTextSession = 1;

    /// <summary>把文本输入焦点交给控件：会话令牌递增，旧令牌的提交会被丢弃。</summary>
    internal void SetTextInputFocus(TextField field, ulong token, int x, int y, int width, int height)
    {
        if (ReferenceEquals(textFocus, field)) return;
        textFocus = field;
        textSession = nextTextSession++;
        if (nextTextSession == 0) nextTextSession = 1;
        bridge?.SetTextInput(textSession, true, x, y, width, height);
    }

    /// <summary>释放文本输入焦点；不是持有者调用时无副作用。</summary>
    internal void ClearTextInputFocus(TextField field)
    {
        if (!ReferenceEquals(textFocus, field)) return;
        textFocus = null;
        textSession = 0;
        bridge?.SetTextInput(0, false, 0, 0, 0, 0);
    }

    /// <summary>当前文本输入会话令牌；没有焦点时为 0。</summary>
    internal ulong TextSession => textSession;

    /// <summary>持有文本输入的控件；没有时为空。</summary>
    internal TextField? TextFocus => textFocus;

    /// <summary>事件派发器；控件事件统一从这里进 FIFO。</summary>
    public UIEventDispatcher EventDispatcher { get; } = new();

    /// <summary>输入路由器；控件与模块都通过它交互。</summary>
    public UIInputRouter InputRouter => inputRouter;

    /// <summary>命中检测；需要自行判定命中时使用。</summary>
    public UIRaycaster Raycaster => raycaster;

    /// <summary>当前的输入模块。</summary>
    public UIInputModule GetInputModule() => inputModule;

    /// <summary>替换输入模块；先复位旧模块并取消捕获，再换新的。</summary>
    public void SetInputModule(UIInputModule module)
    {
        ArgumentNullException.ThrowIfNull(module);
        if (ReferenceEquals(inputModule, module)) return;

        inputModule.Reset();
        inputRouter.Reset();
        inputModule = module;
    }

    /// <summary>注入一次指针事件；离屏画布用它接收外部指针，下一次输入阶段排空。</summary>
    internal void InjectPointer(in UIPointerEvent input) => injectedPointers.Add(input);

    /// <summary>本上下文的帧构建器；世界附着后它的提交目标就是原生上下文。</summary>
    public UIFrameBuilder FrameBuilder => frameBuilder;

    /// <summary>原生上下文薄层；原生侧未接入或版本不符时为空。</summary>
    public RetainedGuiBridge? NativeBridge => bridge;

    /// <summary>当前活动的 UI 上下文；没有世界附着时为空。</summary>
    public static UIWorldContext? Current { get; private set; }

    /// <summary>本上下文对应的世界代次。</summary>
    public ulong WorldRevision => worldRevision;

    /// <summary>本上下文对应的托管会话代次。</summary>
    public ulong ManagedGeneration => managedGeneration;

    /// <summary>是否运行在编辑模式：只做结构同步、布局与渲染，不路由交互。</summary>
    public bool IsEditorMode => editorMode;

    /// <summary>是否由原生侧要求整表重建。</summary>
    public bool NeedsFullResync => fullResync;

    /// <summary>当前索引中的全部节点。</summary>
    public IReadOnlyCollection<UINode> Nodes => nodes.Values;

    /// <summary>当前索引中的全部画布，按排序权重升序。</summary>
    public IReadOnlyList<Canvas> Canvases => canvases;

    /// <summary>派生位置写入目标；原生 UI 上下文接入后由它承担实际的 Transform 写入。</summary>
    public IUIDerivedPositionSink? DerivedPositionSink { get; set; }

    //屏幕画布的显示区域像素尺寸，由渲染阶段读取视图后写入。
    private readonly Dictionary<Canvas, vector2> canvasViewports = [];

    /// <summary>读取字形光栅缩放；世界空间固定按配置字号生成。</summary>
    internal float GetRasterScale(Canvas? canvas)
    {
        if (canvas == null || canvas.GetRenderMode() == CanvasRenderMode.WorldSpace) return 1.0f;
        if (canvas.GetRenderMode() == CanvasRenderMode.Offscreen) return canvas.ComputeScale(canvas.GetOutputSize());
        return canvasViewports.TryGetValue(canvas, out vector2 pixels) ? canvas.ComputeScale(pixels) : 1.0f;
    }

    /// <summary>节点输入处理器缓存；输入路由与命中过滤共用，换世界时清空。</summary>
    internal UIHandlerCache HandlerCache { get; } = new();

    /// <summary>记录画布的显示区域像素尺寸；屏幕画布据此换算逻辑尺寸。</summary>
    public void SetCanvasViewport(Canvas canvas, vector2 pixelSize)
    {
        //宿主显式设置的尺寸优先，之后不再被视图快照覆盖。
        explicitCanvasViewports.Add(canvas);
        ApplyCanvasViewport(canvas, pixelSize);
    }

    //写入解析用的显示尺寸并让整棵子树重排。
    private void ApplyCanvasViewport(Canvas canvas, vector2 pixelSize)
    {
        canvasViewports[canvas] = pixelSize;
        if (canvas.GetLayout() is UILayout layout) layoutDirty.Add(layout);
    }

    //离屏画布直接用输出尺寸；世界空间画布按根节点的 sizeDelta 解析；屏幕画布用已记录的显示区域。
    internal vector2 ResolveCanvasLogicalSize(Canvas canvas)
    {
        if (canvas.GetRenderMode() == CanvasRenderMode.Offscreen)
            return canvas.ComputeLogicalSize(canvas.GetOutputSize());
        if (canvas.GetRenderMode() == CanvasRenderMode.WorldSpace)
            return ResolveWorldSpaceSize(canvas);
        return canvasViewports.TryGetValue(canvas, out vector2 pixelSize)
            ? canvas.ComputeLogicalSize(pixelSize)
            : new vector2(0.0f, 0.0f);
    }

    //世界空间画布不参与分辨率缩放：根矩形就是根节点自己的 sizeDelta。
    private static vector2 ResolveWorldSpaceSize(Canvas canvas)
    {
        vector2 size = canvas.GetLayout() is UILayout layout
            ? layout.GetSizeDelta()
            : new vector2(Canvas.DefaultWorldSpaceWidth, Canvas.DefaultWorldSpaceHeight);
        if (!float.IsFinite(size.x) || !float.IsFinite(size.y)) return default;
        return new vector2(MathF.Max(size.x, 0.0f), MathF.Max(size.y, 0.0f));
    }

    //批量提交派生位置；没有写入目标时解析结果仍然保留在组件上。
    internal void CommitDerivedPositions(List<UIDerivedPosition> positions)
    {
        if (positions.Count == 0) return;
        DerivedPositionSink?.ApplyDerivedPositions(System.Runtime.InteropServices.CollectionsMarshal.AsSpan(positions));
    }

    /// <summary>附着世界：建立节点索引并接入帧阶段。</summary>
    public void AttachWorld(ulong revision, bool editorModeValue)
    {
        //模块初始化早于引擎 API 绑定是正常次序：世界附着时再对一次原生函数表。
        RetainedGuiNative.EnsureInitialized();
        worldRevision = revision;
        managedGeneration = ManagedAssemblySession.GetGeneration();
        editorMode = editorModeValue;
        worldAttached = true;
        Current = this;

        //原生上下文与世界代次绑定；接入失败时 UI 只做数据与布局，不提交也不绘制。
        bridge = RetainedGuiBridge.Create(revision, managedGeneration);
        //切换字形上下文并清理旧世界图集
        FontAtlasCache.SetSharedContext(bridge?.Context ?? 0);
        frameBuilder.Host = bridge;
        frameBuilder.Reset(managedGeneration);
        DerivedPositionSink = bridge;
        RebuildIndex();
    }

    /// <summary>分离世界：先撤销全部派生位置覆盖，再清空索引与解析结果。</summary>
    public void DetachWorld()
    {
        //域卸载先恢复作者位置，否则下次附着时残留的覆盖会盖住新算出的结果。
        List<UIDerivedPosition> clears = [];
        foreach (UINode node in nodes.Values)
        {
            if (node.Canvas == null) clears.Add(new UIDerivedPosition { ens = node.Ens, clear = 1 });
        }
        CommitDerivedPositions(clears);

        UILayoutRegistry.Clear(nodes.Values);
        nodes.Clear();
        canvasViewports.Clear();
        HandlerCache.Clear();
        defaultFont = null;
        defaultFontLoadAttempted = false;
        canvases.Clear();
        layoutDirty.Clear();
        geometryDirty.Clear();
        inputDirty.Clear();
        pendingChanges.Clear();
        fullResync = false;
        worldAttached = false;
        frameBuilder.Host = null;
        frameBuilder.Reset(managedGeneration);
        //世界要换了：临时弹层先关掉，别把 DontSave 子树留到下一个世界。
        CloseComboPopups(null);
        DerivedPositionSink = null;
        FontAtlasCache.SetSharedContext(0);
        bridge?.Destroy();
        bridge = null;
        if (ReferenceEquals(Current, this)) Current = null;
    }

    /// <summary>
    /// 屏幕画布的默认视口取窗口帧缓冲尺寸。宿主（编辑器预览）可以显式改写，
    /// 只有仍然等于上一次默认值的画布才跟着窗口一起变，宿主写进去的尺寸不会被覆盖。
    /// </summary>
    private void RefreshDefaultCanvasViewports()
    {
        //视图快照来自上一帧的原生渲染：托管侧不猜窗口尺寸，只读实际画出来的目标尺寸。
        if (bridge == null) return;

        int count = bridge.CountViews();
        if (count > 0)
        {
            if (count > viewBuffer.Length) viewBuffer = new UIView[count];
            foreach (UIView view in bridge.ReadViews(viewBuffer))
            {
                vector2 size = new(view.width, view.height);
                if (size.x <= 0.0f || size.y <= 0.0f) continue;
                //快照按画布对象 ID 索引；宿主显式设置过的画布不再被覆盖。
                foreach (Canvas canvas in canvases)
                {
                    if (canvas.GetRenderMode() != CanvasRenderMode.Overlay) continue;
                    if (unchecked((ulong)(uint)canvas.InstanceId) != view.viewId) continue;
                    if (explicitCanvasViewports.Contains(canvas)) break;
                    ApplyCanvasViewport(canvas, size);
                    break;
                }
            }
        }

        //首帧引导：屏幕画布在渲染过一次之前没有视图，而没视口就不会被提交。
        //渲染器每帧报出主显示目标的尺寸，这里用它补齐还没拿到视图的屏幕画布。
        if (bridge.TryReadDisplaySize(out int displayWidth, out int displayHeight))
        {
            vector2 display = new(displayWidth, displayHeight);
            foreach (Canvas canvas in canvases)
            {
                if (canvas.GetRenderMode() != CanvasRenderMode.Overlay) continue;
                if (canvasViewports.ContainsKey(canvas)) continue;
                ApplyCanvasViewport(canvas, display);
            }
        }

    }

    /// <summary>输入阶段：编辑模式不路由交互。</summary>
    public void ProcessInput(float deltaTime)
    {
        if (!worldAttached || editorMode) return;
        ApplyPendingChanges();

        inputRouter.BeginPhase();
        //先处理注入的指针：离屏画布由外部喂事件，不经过平台队列。
        for (int index = 0; index < injectedPointers.Count; ++index)
            inputRouter.ProcessPointer(injectedPointers[index]);
        injectedPointers.Clear();

        ReadRawEvents();
        inputModule.Process(rawEvents.AsSpan(0, rawEventCount), deltaTime, inputRouter);
        inputRouter.EndPhase();

        //事件在同一阶段内排空：回调里产生的新事件排到队尾，逐条处理完。
        eventDispatcher.BeginFrame();
        eventDispatcher.Dispatch();
    }

    //从原生侧取本阶段新增的原始事件；容量不足时按需求扩容后重新读取。
    private void ReadRawEvents()
    {
        rawEventCount = 0;
        if (bridge is not RetainedGuiBridge host) return;

        int required = host.CountInput();
        if (required <= 0) return;
        if (required > inputRecords.Length) inputRecords = new UIInputRecord[required];

        int count = 0;
        int textBytes = 0;
        bool complete = false;
        //扩容后重新读取完整的输入批次
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            count = host.ReadInput(inputRecords, inputText, out textBytes);
            if (count <= 0) return;
            bool resized = false;
            if (count > inputRecords.Length) { inputRecords = new UIInputRecord[count]; resized = true; }
            if (textBytes > inputText.Length) { inputText = new byte[textBytes]; resized = true; }
            if (!resized) { complete = true; break; }
        }
        if (!complete) return;

        if (count > rawEvents.Length) rawEvents = new UIRawInputEvent[count];
        for (int index = 0; index < count; ++index)
        {
            UIInputRecord record = inputRecords[index];
            string text = string.Empty;
            if (record.textLength > 0)
            {
                int offset = (int)record.textOffset;
                int length = (int)record.textLength;
                if (offset >= 0 && length >= 0 && offset + length <= textBytes)
                    text = InteropText.DecodeUtf8(inputText, offset, length);
            }
            rawEvents[index] = new UIRawInputEvent(record, text);
        }
        rawEventCount = count;
    }

    /// <summary>渲染准备阶段：先应用结构变化，再执行布局与几何重建。</summary>
    public void PrepareRender(float deltaTime)
    {
        if (!worldAttached) return;
        ApplyPendingChanges();
        RefreshDefaultCanvasViewports();
        //字形页在本帧重建期间被命中的一律固定，回收统一推迟到帧尾。
        FontAtlasCache.Shared.BeginFrame();
        //顺序固定：先结构、再布局，最后图形——几何依赖这一帧的解析矩形。
        foreach (UINode node in nodes.Values)
            UIGraphicRegistry.MarkInvalidatedGeometry(node, geometryDirty);
        UILayoutRegistry.Rebuild(this, layoutDirty);
        foreach (UINode node in nodes.Values)
        {
            (node.Visual as UIVisual)?.RefreshMeshModifiers();
            UIGraphicRegistry.MarkResizedGeometry(node, geometryDirty);
        }
        UIGraphicRegistry.Rebuild(geometryDirty, rebuildingGeometry);
        //命中快照在图形重建后失效，由输入阶段读取。
        inputDirty.Clear();

        //滚动容器的惯性按真实帧时间推进；速度为零的容器会立刻返回。
        foreach (UINode node in nodes.Values)
        {
            if (node.GetElement<ScrollBox>() is ScrollBox box) box.Tick(deltaTime);
        }

        BuildFrame();
        FontAtlasCache.Shared.EndFrame();
    }

    //构建并提交本帧。零尺寸视图直接跳过：既不出布局也不进命中快照。
    private void BuildFrame()
    {
        ++frameCounter;
        if (frameCounter == 0) frameCounter = 1;
        frameBuilder.BeginFrame(frameCounter);

        //刷新脏绘制状态供依赖收集与提交共用
        foreach (UINode node in nodes.Values) (node.Visual as UIVisual)?.PrepareDrawStates();
        OrderCanvases();
        foreach (Canvas canvas in frameOrder)
        {
            if (suppressedCanvases.Contains(canvas)) continue;

            if (!TryResolveCanvasView(canvas, out UIView view)) continue;
            frameBuilder.BuildCanvas(canvas, view);
        }
        //被抑制的离屏画布仍然提交一次空命令：输出必须每帧从透明黑开始。
        foreach (Canvas canvas in suppressedCanvases)
        {
            if (canvas.GetRenderMode() != CanvasRenderMode.Offscreen) continue;
            if (!TryResolveCanvasView(canvas, out UIView view)) continue;
            frameBuilder.BuildEmptyCanvas(canvas, view);
        }
        frameBuilder.Submit();
    }

    //每帧的画布顺序：离屏按输出纹理依赖拓扑排序（上游先画），其余按 sortOrder 跟在后面。
    private void OrderCanvases()
    {
        frameOrder.Clear();
        suppressedCanvases.Clear();
        offscreenCanvases.Clear();
        foreach (Canvas canvas in canvases)
        {
            if (canvas.GetRenderMode() == CanvasRenderMode.Offscreen) offscreenCanvases.Add(canvas);
        }
        if (offscreenCanvases.Count == 0)
        {
            frameOrder.AddRange(canvases);
            return;
        }

        int count = offscreenCanvases.Count;
        //输出纹理到生产它的画布下标。
        canvasProducers.Clear();
        for (int index = 0; index < count; ++index)
        {
            if (offscreenCanvases[index].GetOutputTexture() is Texture2D output)
                canvasProducers[output.InstanceId] = index;
        }

        //建图：读别人输出纹理的画布是下游，必须排在被读的画布之后。
        canvasEdges.Clear();
        canvasInDegree.Clear();
        for (int index = 0; index < count; ++index)
        {
            canvasEdges.Add([]);
            canvasInDegree.Add(0);
        }
        for (int consumer = 0; consumer < count; ++consumer)
        {
            foreach (int producer in CollectCanvasDependencies(offscreenCanvases[consumer]))
            {
                //自环与重复边只记一次；自环会让拓扑排序卡住，随后整环清透明。
                if (canvasEdges[producer].Add(consumer)) canvasInDegree[consumer]++;
            }
        }

        //Kahn 拓扑排序；卡住说明剩下的都在环里。
        canvasOrder.Clear();
        while (canvasOrder.Count < count)
        {
            int current = -1;
            for (int index = 0; index < count; ++index)
            {
                if (canvasInDegree[index] == 0 && !canvasOrder.Contains(index))
                {
                    current = index;
                    break;
                }
            }
            if (current < 0)
            {
                //环内画布清透明并报告一条路径；非循环下游仍然执行，采样到的就是透明。
                List<int> cycle = FindCycle(canvasEdges, canvasOrder);
                foreach (int member in cycle)
                {
                    suppressedCanvases.Add(offscreenCanvases[member]);
                    canvasOrder.Add(member);
                }
                Console.Error.WriteLine(
                    $"Canvas 输出纹理存在循环依赖，以下画布清透明：{DescribeCycle(cycle)}");
                foreach (int member in cycle)
                {
                    foreach (int next in canvasEdges[member])
                    {
                        if (canvasInDegree[next] > 0) canvasInDegree[next]--;
                    }
                }
                continue;
            }

            canvasOrder.Add(current);
            foreach (int next in canvasEdges[current])
            {
                if (canvasInDegree[next] > 0) canvasInDegree[next]--;
            }
        }

        foreach (int index in canvasOrder) frameOrder.Add(offscreenCanvases[index]);
        foreach (Canvas canvas in canvases)
        {
            if (canvas.GetRenderMode() != CanvasRenderMode.Offscreen) frameOrder.Add(canvas);
        }
    }

    //在还没排进顺序的节点里找一条环路径：沿边一直走，踩回路径上的点即成环。
    private List<int> FindCycle(List<HashSet<int>> edges, List<int> ordered)
    {
        int start = -1;
        for (int index = 0; index < edges.Count; ++index)
        {
            if (!ordered.Contains(index))
            {
                start = index;
                break;
            }
        }
        cyclePath.Clear();
        cycleOnPath.Clear();
        if (start < 0) return cyclePath;

        int current = start;
        while (true)
        {
            if (!cycleOnPath.Add(current)) break;
            cyclePath.Add(current);
            int next = -1;
            foreach (int candidate in edges[current])
            {
                if (cycleOnPath.Contains(candidate) || !ordered.Contains(candidate))
                {
                    next = candidate;
                    break;
                }
            }
            if (next < 0) return cyclePath;
            current = next;
        }

        //current 是回边指向的节点，去掉它之前的前缀就是环本身。
        int first = cyclePath.IndexOf(current);
        cyclePath.RemoveRange(0, first);
        return cyclePath;
    }

    private string DescribeCycle(List<int> cycle)
    {
        System.Text.StringBuilder text = new();
        foreach (int index in cycle)
        {
            if (text.Length != 0) text.Append(" -> ");
            text.Append(offscreenCanvases[index].ResourceKey);
        }
        if (cycle.Count > 0) text.Append(" -> ").Append(offscreenCanvases[cycle[0]].ResourceKey);
        return text.ToString();
    }

    //收集一块画布子树里引用的输出纹理，返回生产它们的画布下标。
    private List<int> CollectCanvasDependencies(Canvas canvas)
    {
        canvasDependencies.Clear();
        UINode? root = FindNode(canvas.EnsId);
        if (root != null) CollectCanvasDependencies(root);
        return canvasDependencies;
    }

    //子树里本帧真正会用到的纹理：最终片段状态的纹理、材质的全部纹理、控件附加网格与遮罩形状。
    //依赖信息每帧重建，纹素或材质参数改了下一帧自然生效，不需要额外的失效通知。
    private void CollectCanvasDependencies(UINode node)
    {
        if (node.Visual is UIVisual visual) CollectVisualDependencies(visual);
        if (node.GetElement<UIControl>() is UIControl control) CollectOverlayDependencies(control);
        if (node.GetElement<Mask>() is Mask mask) AddCanvasDependency(mask.GetTexture());
        foreach (UINode child in node.Children) CollectCanvasDependencies(child);
    }

    //图形的最终片段状态：纹理与显式材质引用的全部纹理。
    private void CollectVisualDependencies(UIVisual visual)
    {
        UIMeshBuilder mesh = visual.Mesh;
        for (int index = 0; index < mesh.Fragments.Count; ++index)
        {
            if (!visual.TryGetDrawState(index, out UIDrawState state)) continue;
            AddCanvasDependency(state.texture);
            CollectMaterialDependencies(state.material);
        }
    }

    //控件附加网格的纹理：与图形同源，读取共享网格。
    private void CollectOverlayDependencies(UIControl control)
    {
        if (control.GetOverlay() is not UIMeshBuilder overlay) return;
        foreach (UIMeshFragment fragment in overlay.Fragments) AddCanvasDependency(fragment.state.texture);
    }

    //材质引用的全部纹理；材质换了纹理也走这条路进依赖图。
    private void CollectMaterialDependencies(Material? material)
    {
        if (material == null) return;
        foreach (MaterialTextureSlot slot in material.textureSlots) AddCanvasDependency(slot.texture);
    }

    //一条纹理依赖：只有生产它的是本帧的离屏画布才算边。
    private void AddCanvasDependency(Texture2D? texture)
    {
        if (texture == null) return;
        if (!canvasProducers.TryGetValue(texture.InstanceId, out int index)) return;
        canvasDependencies.Add(index);
    }

    //屏幕画布用已记录的显示区域与单位正交投影；离屏画布用输出尺寸。
    private bool TryResolveCanvasView(Canvas canvas, out UIView view)
    {
        view = default;
        int width;
        int height;
        if (canvas.GetRenderMode() == CanvasRenderMode.Offscreen)
        {
            vector2 size = canvas.GetOutputSize();
            width = (int)size.x;
            height = (int)size.y;
            view.logicalSize = canvas.ComputeLogicalSize(size);
        }
        else if (canvas.GetRenderMode() == CanvasRenderMode.WorldSpace)
        {
            //世界空间画布没有屏幕面积：尺寸来自根节点，落点由相机决定。
            //这里的像素尺寸只作占位，原生渲染后发布的视图会带上真实相机矩阵。
            vector2 size = ResolveWorldSpaceSize(canvas);
            width = (int)size.x;
            height = (int)size.y;
            view.logicalSize = size;
        }
        else if (canvasViewports.TryGetValue(canvas, out vector2 pixels))
        {
            width = (int)pixels.x;
            height = (int)pixels.y;
            view.logicalSize = canvas.ComputeLogicalSize(pixels);
        }
        else
        {
            return false;
        }

        if (width <= 0 || height <= 0) return false;
        view.viewId = unchecked((ulong)(uint)canvas.InstanceId);
        view.width = width;
        view.height = height;
        view.view = matrix4x4.Identity;
        view.projection = matrix4x4.Identity;
        view.flags = UIViewFlags.Primary | UIViewFlags.Presented;
        return true;
    }

    /// <summary>把一条场景结构记录排入队列；溢出时改为整表重建。</summary>
    public void EnqueueChange(in UISceneChange change)
    {
        if (change.worldRevision != 0 && change.worldRevision != worldRevision)
        {
            //代次不匹配说明记录属于已经换掉的世界，直接丢弃。
            return;
        }
        if (fullResync) return;
        if (pendingChanges.Count >= MaxPendingChanges)
        {
            pendingChanges.Clear();
            fullResync = true;
            return;
        }
        pendingChanges.Add(change);
    }

    /// <summary>要求下一边界整表重建索引。</summary>
    public void RequestFullResync()
    {
        pendingChanges.Clear();
        fullResync = true;
    }

    /// <summary>按 Ens 句柄取节点。</summary>
    public UINode? FindNode(EnsId ens) => nodes.TryGetValue(ens, out UINode? node) ? node : null;

    /// <summary>清除一个 Ens 的派生位置覆盖，恢复作者位置；退出 UI、禁用与销毁时调用。</summary>
    public static void ClearDerivedPosition(EnsId ens)
    {
        if (ens.IsNull || Current?.DerivedPositionSink is not IUIDerivedPositionSink sink) return;
        Span<UIDerivedPosition> entry = stackalloc UIDerivedPosition[1];
        entry[0] = new UIDerivedPosition { ens = ens, clear = 1 };
        sink.ApplyDerivedPositions(entry);
    }

    /// <summary>标记布局脏；同一帧内的重复标记合并成一次重建。</summary>
    public static void MarkLayoutDirty(UILayout? layout)
    {
        UIWorldContext? context = Current;
        if (context == null || layout == null) return;
        context.layoutDirty.Add(layout);
    }

    /// <summary>标记几何脏。</summary>
    public static void MarkGeometryDirty(UIElement element)
    {
        UIWorldContext? context = Current;
        if (context == null) return;
        context.geometryDirty.Add(element);
        context.inputDirty.Add(element);
    }

    /// <summary>标记命中快照失效；几何或层级变化后调用。</summary>
    public static void MarkInputDirty(UIElement element)
    {
        Current?.inputDirty.Add(element);
    }

    /// <summary>
    /// 清空命中快照：分辨率或视图尺寸变化后，按旧尺寸判出来的命中不再可信。
    /// </summary>
    public static void MarkAllInputDirty()
    {
        UIWorldContext? context = Current;
        if (context == null) return;
        foreach (UINode node in context.nodes.Values)
        {
            if (node.Layout is UILayout layout && node.Elements.Count != 0)
                foreach (UIElement element in node.Elements) context.inputDirty.Add(element);
        }
    }

    /// <summary>画布配置变化：根矩形与目标都要重新解析。</summary>
    public static void MarkCanvasDirty(Canvas canvas)
    {
        UIWorldContext? context = Current;
        if (context == null) return;
        if (canvas.GetLayout() is UILayout layout) context.layoutDirty.Add(layout);
    }

    /// <summary>节点的层级或 ignoreLayout 变化：父组要重排。</summary>
    public static void NotifyLayoutStructureChanged(UILayout layout)
    {
        UIWorldContext? context = Current;
        if (context == null) return;
        context.layoutDirty.Add(layout);
        if (context.FindNode(layout.EnsId) is UINode node && node.Parent?.Layout is UILayout parentLayout)
        {
            context.layoutDirty.Add(parentLayout);
        }
    }

    /// <summary>取得与元素同节点的 UILayout；没有时为空。</summary>
    internal static UILayout? GetLayoutOf(UIElement element) => element.GetLayout();

    /// <summary>沿 Ens 父级向上找最近的 UI 祖先节点上的第一个元素。</summary>
    internal static UIElement? GetParentElement(UIElement element)
    {
        UINode? parent = Current?.FindNode(element.EnsId)?.Parent;
        return parent != null && parent.Elements.Count != 0 ? parent.Elements[0] : null;
    }

    /// <summary>取直接子 UI 节点的第一个元素。</summary>
    internal static IReadOnlyList<UIElement> GetChildElements(UIElement element)
    {
        UIWorldContext? context = Current;
        if (context == null) return [];

        UINode? node = context.FindNode(element.EnsId);
        if (node == null) return [];

        List<UIElement> result = [];
        foreach (UINode child in node.Children)
        {
            if (child.Elements.Count != 0) result.Add(child.Elements[0]);
        }
        return result;
    }

    /// <summary>取元素的直接子节点；组用它缓存有效子节点，避免重建期调用 GetComponents。</summary>
    internal static IReadOnlyList<UINode> GetChildNodes(UIElement element)
    {
        UINode? node = Current?.FindNode(element.EnsId);
        return node?.Children ?? (IReadOnlyList<UINode>)[];
    }

    /// <summary>登记一个 UI 组件；上下文尚未附着时跳过，索引会在附着时整体建立。</summary>
    internal static void RegisterElement(UIElement element)
    {
        UIWorldContext? context = Current;
        if (context == null) return;
        context.AddElement(element);
        context.RequestFullResync();
    }

    /// <summary>撤销一个 UI 组件的登记。</summary>
    internal static void UnregisterElement(UIElement element)
    {
        UIWorldContext? context = Current;
        if (context == null) return;
        context.RemoveElement(element);
        context.RequestFullResync();
    }

    //把一个 UIElement 收进索引；同节点第二个元素只追加不进树。
    private void AddElement(UIElement element)
    {
        EnsId ens = element.EnsId;
        if (ens.IsNull) return;

        if (!nodes.TryGetValue(ens, out UINode? node))
        {
            node = new UINode(ens);
            nodes.Add(ens, node);
            node.Layout = element.GetLayout();
            node.Canvas = element as Canvas;
            RelinkNode(node);
        }
        if (!node.Elements.Contains(element)) node.Elements.Add(element);
        node.Layout = element.GetLayout();
        node.Canvas = node.GetElement<Canvas>();
        if (element is Canvas canvas && !canvases.Contains(canvas)) canvases.Add(canvas);
        if (node.Layout != null) layoutDirty.Add(node.Layout);
        geometryDirty.Add(element);
        inputDirty.Add(element);
        SortCanvases();
        ValidateNode(node);
    }

    //撤销登记；节点上还有别的元素时保留节点。
    private void RemoveElement(UIElement element)
    {
        if (!nodes.TryGetValue(element.EnsId, out UINode? node)) return;
        node.Elements.Remove(element);
        if (element is Canvas canvas)
        {
            canvases.Remove(canvas);
            canvasViewports.Remove(canvas);
            explicitCanvasViewports.Remove(canvas);
        }
        node.Canvas = node.GetElement<Canvas>();
        if (node.Elements.Count != 0 || node.Layout != null)
        {
            if (node.Layout != null) layoutDirty.Add(node.Layout);
            ValidateNode(node);
            return;
        }

        //节点离开 UI 树时交还作者位置，避免残留的布局覆盖继续驱动世界矩阵。
        ClearDerivedPosition(node.Ens);
        foreach (UINode child in node.Children.ToArray()) child.Parent = null;
        nodes.Remove(node.Ens);
        HandlerCache.Remove(node.Ens);
        if (node.Parent != null) node.Parent.Children.Remove(node);
    }

    //整表重建：按当前世界的托管宿主重新建立索引与父子关系。
    private void RebuildIndex()
    {
        HandlerCache.Clear();
        nodes.Clear();
        canvases.Clear();
        layoutDirty.Clear();
        geometryDirty.Clear();
        inputDirty.Clear();
        fullResync = false;
        pendingChanges.Clear();

        //先登记纯布局节点再接入图形与控件
        foreach (Script script in ScriptRuntimeRegistry.GetAllScripts())
        {
            if (script is UILayout layout && !layout.EnsId.IsNull)
                nodes[layout.EnsId] = new UINode(layout.EnsId) { Layout = layout };
        }
        foreach (Script script in ScriptRuntimeRegistry.GetAllScripts())
        {
            if (script is UIElement element) AddElement(element);
        }
        foreach (UINode node in nodes.Values) RelinkNode(node);
        foreach (UINode node in nodes.Values) ValidateNode(node);
    }

    //按 Ens 父级重新计算父子关系；最近的一个 UI 祖先才是 UI 父节点。
    //重新挂接之后的额外登记：分组一类依赖父级的东西在这里重建。
    private static void NotifyReparented(UINode node)
    {
        foreach (UIElement element in node.Elements) element.NotifyReparented();
    }

    private void RelinkNode(UINode node)
    {
        node.Parent?.Children.Remove(node);
        node.Parent = null;

        EnsId parent = Ens.FromId(node.Ens).Transform.GetParent();
        while (!parent.IsNull)
        {
            if (nodes.TryGetValue(parent, out UINode? parentNode))
            {
                node.Parent = parentNode;
                if (!parentNode.Children.Contains(node)) parentNode.Children.Add(node);
                break;
            }
            Ens owner = Ens.FromId(parent);
            if (!owner.IsValid) break;
            parent = owner.Transform.GetParent();
        }

        //父级定下来之后通知元素重新登记：分组一类依赖父级的东西在这里重建。
        NotifyReparented(node);
    }

    //按设计约束给出配置错误：UI 节点禁止 static，画布不得嵌套，UI 树不得跨越缺少 UILayout 的节点。
    private void ValidateNode(UINode node)
    {
        node.ConfigurationError = string.Empty;
        Ens owner = Ens.FromId(node.Ens);
        if (!owner.IsValid)
        {
            node.ConfigurationError = "UI Ens is no longer valid.";
            return;
        }
        if (owner.Static)
        {
            node.ConfigurationError = "UI Ens cannot be static.";
            return;
        }
        if (node.Layout == null)
        {
            node.ConfigurationError = "UI node is missing UILayout.";
            return;
        }
        //画布是 UI 子树的根：根节点必须自己就是画布，非根节点必须落在某个画布之下。
        Canvas? ancestorCanvas = null;
        for (UINode? ancestor = node.Parent; ancestor != null; ancestor = ancestor.Parent)
        {
            if (ancestor.Layout == null)
            {
                node.ConfigurationError = "UI ancestor is missing UILayout.";
                return;
            }
            if (ancestor.Canvas == null) continue;
            ancestorCanvas = ancestor.Canvas;
            break;
        }
        if (node.Canvas != null && ancestorCanvas != null)
            node.ConfigurationError = "Nested Canvas is not supported.";
        else if (node.Canvas == null && ancestorCanvas == null)
            node.ConfigurationError = "UI node has no owning Canvas.";
        //遮罩的诊断不走 ConfigurationError：那条路径会让整棵子树跳过布局，
        //而遮罩配置不完整只应让覆盖率为零，帧构建器与检视面板直接问 Mask。
    }

    /// <summary>遮罩配置变化：命中快照失效，下一帧重新生成裁剪命令。</summary>
    internal static void NotifyMaskChanged(Mask mask)
    {
        UIWorldContext? context = Current;
        if (context == null) return;
        context.inputDirty.Add(mask);
        //裁剪形状按帧重建，网格内容版本靠形状比较推进，这里不必额外标脏。
    }

    //画布按 sortOrder 升序；同值按稳定组件路径 ordinal 排序。
    private void SortCanvases()
    {
        canvases.Sort((left, right) =>
        {
            int order = left.GetSortOrder().CompareTo(right.GetSortOrder());
            return order != 0 ? order : string.CompareOrdinal(left.ResourceKey, right.ResourceKey);
        });
    }

    //应用排队的结构记录；整表重建请求优先。
    private void ApplyPendingChanges()
    {
        if (fullResync)
        {
            RebuildIndex();
            return;
        }
        if (pendingChanges.Count == 0) return;

        //结构变化后按完整组件集合重建层级与首帧脏队列
        foreach (UISceneChange change in pendingChanges)
        {
            if (change.worldRevision != 0 && change.worldRevision != worldRevision) continue;
            if ((UISceneChangeKind)change.kind is not (UISceneChangeKind.Added or UISceneChangeKind.Removed or UISceneChangeKind.Reparented)) continue;
            RebuildIndex();
            return;
        }

        foreach (UISceneChange change in pendingChanges)
        {
            if (change.worldRevision != 0 && change.worldRevision != worldRevision) continue;
            switch ((UISceneChangeKind)change.kind)
            {
            case UISceneChangeKind.Added:
            case UISceneChangeKind.ActiveChanged:
                if (nodes.TryGetValue(change.ens, out UINode? node)) { ValidateNode(node); break; }
                break;
            case UISceneChangeKind.Removed:
                nodes.Remove(change.ens);
                HandlerCache.Remove(change.ens);
                break;
            case UISceneChangeKind.Reparented:
                if (nodes.TryGetValue(change.ens, out UINode? moved)) RelinkNode(moved);
                break;
            case UISceneChangeKind.TransformChanged:
                if (nodes.TryGetValue(change.ens, out UINode? touched) && touched.Layout != null)
                {
                    touched.Layout.SynchronizePosition();
                    EnqueueLayout(touched.Layout);
                }
                break;
            case UISceneChangeKind.FieldsChanged:
                if (nodes.TryGetValue(change.ens, out UINode? changed) && changed.Layout != null)
                {
                    EnqueueLayout(changed.Layout);
                    //刷新检视面板直接写入的图形与交互配置
                    foreach (UIElement element in changed.Elements)
                    {
                        MarkGeometryDirty(element);
                        MarkInputDirty(element);
                        if (element is UIVisual visual)
                        {
                            visual.SetVerticesDirty();
                            visual.SetMaterialDirty();
                        }
                        if (element is UIControl control) control.RefreshVisualState();
                    }
                    if (changed.Canvas != null) MarkCanvasDirty(changed.Canvas);
                }
                break;
            }
        }
        pendingChanges.Clear();

        //父子关系只在结构类记录出现时重算，字段变化不触发。
        foreach (UINode node in nodes.Values) ValidateNode(node);
    }

    private void EnqueueLayout(UILayout layout) => layoutDirty.Add(layout);
}
