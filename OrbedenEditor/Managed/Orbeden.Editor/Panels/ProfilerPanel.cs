using Orbeden;

namespace OrbedenEditor;

/// <summary>按帧展示性能采样的剖析面板：上方针状走势图，下方单帧层级表与甘特图。</summary>
internal sealed class ProfilerPanel : EditorPanel
{
    //走势图高度
    private const float ChartHeight = 120.0f;

    //图例色块尺寸
    private const float LegendSwatchWidth = 12.0f;
    private const float LegendRowHeight = 15.0f;

    //甘特图行高与时间轴高度
    private const float TimelineRowHeight = 14.0f;
    private const float TimelineAxisHeight = 16.0f;

    //甘特图缩放：每次滚轮的倍率与最大放大倍数
    private const float TimelineZoomStep = 0.15f;
    private const float TimelineMaxZoom = 50.0f;

    //单帧分类数量，与原生 ProfileCategory 一致
    private const int CategoryCount = 6;

    //走势图保留的帧数，与原生帧摘要窗口一致
    private const int MaxTrendFrames = 1024;

    //刷新间隔：采集数据按秒重读，绘制与命中测试仍跟随帧
    private const double RefreshIntervalSeconds = 1.0;

    private static readonly string[] CategoryNames = { "Other", "Script", "Render", "Physics", "FileIO", "Editor" };
    private static readonly string[] SortNames = { "Tree", "Total", "Self", "Calls" };

    //每帧事件上限，与原生 FrameEventCapacity 一致
    private const int MaxFrameEvents = 128;

    //每帧最多画 6 个分类分段加空闲与选中高亮，一次批量提交要放得下整个走势图
    private const int MaxChartRects = MaxTrendFrames * (CategoryCount + 2) + 64;

    private readonly ProfileFrameSummary[] frames = new ProfileFrameSummary[MaxTrendFrames];
    private readonly ProfileEvent[] events = new ProfileEvent[MaxFrameEvents];

    //甘特图里每次调用的横向区间，画条与写名字共用
    private readonly float[] eventLeft = new float[MaxFrameEvents];
    private readonly float[] eventRight = new float[MaxFrameEvents];
    private int maxEventDepth;

    //走势图定标用的临时缓冲：按分位定标，单根偶发尖峰不该把整幅图压平
    private readonly long[] deltaScratch = new long[MaxTrendFrames];
    private long chartScaleMicroseconds = 1000;
    private long chartPeakMicroseconds;
    private readonly EditorRectPrimitive[] rects = new EditorRectPrimitive[MaxChartRects];
    private readonly Dictionary<int, string> nameCache = new();
    private readonly List<NodeRow> nodeRows = new();
    private readonly Dictionary<int, int> nodeIndexById = new();
    private readonly List<int> sortedRows = new();

    private int frameCount;
    private int selectedFrameEventCount;
    private long selectedFrame = -1;
    private long selectedFrameMicroseconds;
    private bool followLatest = true;
    private bool timelineView;
    private float timelineZoom = 1.0f;
    private long timelinePan;
    private int sortMode;
    private bool panelVisible;
    private bool manualRecording;
    private bool forceRefresh;
    private DateTime lastRepaintRequest = DateTime.MinValue;
    private DateTime lastDataRefresh = DateTime.MinValue;

    public override EditorPanelInfo Info => new("profiler", "Profiler", false,
        new vector2(900, 420), PanelDockPlacement.Bottom, 0.3f, 140);

    /// <summary>面板显示时开始采集。</summary>
    public override void OnShown()
    {
        panelVisible = true;
        UpdateCapturing();
    }

    /// <summary>面板隐藏时停止采集。</summary>
    public override void OnHidden()
    {
        panelVisible = false;
        UpdateCapturing();
    }

    /// <summary>Play 开始会隐藏全部面板，这里跟着打开采集。</summary>
    public override void OnGameAssemblyLoaded(string assemblyPath) => UpdateCapturing();

    /// <summary>Play 结束按当前状态重新判定采集开关。</summary>
    public override void OnGameAssemblyUnloaded() => UpdateCapturing();

    //手动录制开关与面板可见性、Play 状态共同决定是否采集
    private void UpdateCapturing()
    {
        NativeEditorProfiler.SetCapturing(manualRecording && (panelVisible || EditorApplication.IsPlaying));
    }

    //交互后立刻重绘并重读数据，不走按秒节流
    private void RequestImmediateRefresh()
    {
        forceRefresh = true;
        lastRepaintRequest = DateTime.MinValue;
    }

    //按秒重读采集数据，避免每帧都去搬上千帧摘要与事件
    private void RefreshData()
    {
        DateTime now = DateTime.UtcNow;
        if (!forceRefresh && (now - lastDataRefresh).TotalSeconds < RefreshIntervalSeconds) return;

        lastDataRefresh = now;
        forceRefresh = false;
        SyncFrames();
    }

    /// <summary>绘制走势图与单帧分析区。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        //能画到就说明面板可见：可见性没变化时收不到 OnShown，这里自愈一次
        panelVisible = true;
        UpdateCapturing();
        RequestPeriodicRepaint(ref lastRepaintRequest, RefreshIntervalSeconds);

        //数据按秒重读，走势图与表格的绘制仍跟随帧
        RefreshData();
        DrawToolbar();
        EditorGUI.Separator();
        DrawFrameChart();
        DrawLegend();
        EditorGUI.Separator();
        DrawDetail();
    }

    //同步帧摘要并维护选中帧
    private void SyncFrames()
    {
        frameCount = NativeEditorProfiler.CopyFrameSummaries(frames);

        //最新一帧还在采集，不参与展示
        int displayCount = Math.Max(frameCount - 1, 0);
        if (displayCount <= 0)
        {
            selectedFrame = -1;
            selectedFrameEventCount = 0;
            nodeRows.Clear();
            return;
        }

        //选中帧被挤出窗口或采集重启过，回到最新一帧
        long oldest = frames[0].FrameIndex;
        long newest = frames[displayCount - 1].FrameIndex;
        if (followLatest || selectedFrame < oldest || selectedFrame > newest) selectedFrame = newest;

        selectedFrameMicroseconds = 0;
        for (int index = 0; index < displayCount; index++)
        {
            if (frames[index].FrameIndex != selectedFrame) continue;

            selectedFrameMicroseconds = frames[index].DeltaTimeMicroseconds;
            break;
        }

        selectedFrameEventCount = NativeEditorProfiler.CopyFrameEvents(selectedFrame, events);
        BuildNodeRows();
        UpdateChartScale(displayCount);
    }

    //按 95 分位定标，峰值为 0 或样本太少时退回峰值
    private void UpdateChartScale(int displayCount)
    {
        chartPeakMicroseconds = 0;
        for (int index = 0; index < displayCount; index++)
        {
            long delta = frames[index].DeltaTimeMicroseconds;
            deltaScratch[index] = delta;
            chartPeakMicroseconds = Math.Max(chartPeakMicroseconds, delta);
        }

        //样本太少时分位没意义，直接用峰值
        if (displayCount < 20)
        {
            chartScaleMicroseconds = Math.Max(chartPeakMicroseconds, 1000);
            return;
        }

        Array.Sort(deltaScratch, 0, displayCount);
        int index95 = Math.Min((int)(displayCount * 0.95), displayCount - 1);
        chartScaleMicroseconds = Math.Max(deltaScratch[index95], 1000);
    }

    //绘制录制开关、采集状态与视图控件
    private void DrawToolbar()
    {
        if (EditorGUI.Button(manualRecording ? "Stop##profiler_record" : "Record##profiler_record"))
        {
            manualRecording = !manualRecording;
            RequestImmediateRefresh();
        }

        EditorGUI.SameLine();
        bool capturing = NativeEditorProfiler.IsCapturing();
        EditorGUI.TextColored(capturing ? "Recording" : manualRecording ? "Paused" : "Stopped",
            capturing ? EditorTheme.Current.LogError : EditorTheme.Current.ProfileOther);

        EditorGUI.SameLine();
        if (EditorGUI.Button("Clear##profiler_clear"))
        {
            NativeEditorProfiler.ClearFrames();
            followLatest = true;
            selectedFrame = -1;
            RequestImmediateRefresh();
        }

        EditorGUI.SameLine();
        EditorGUI.Checkbox("Follow##profiler_follow", ref followLatest);

        EditorGUI.SameLine();
        if (EditorGUI.Button(timelineView ? "Hierarchy##profiler_view" : "Timeline##profiler_view")) timelineView = !timelineView;

        EditorGUI.SameLine();
        if (selectedFrame >= 0)
        {
            EditorGUI.Label($"Frame {selectedFrame}  |  {ToMilliseconds(selectedFrameMicroseconds):F2} ms  |  {CountEvents()} samples");
        }
        else
        {
            EditorGUI.Label("No frames captured yet.");
        }

        //甘特图放大时给出倍数提示
        if (timelineView && timelineZoom > 1.0f)
        {
            EditorGUI.SameLine();
            EditorGUI.TextColored($"Zoom {timelineZoom:F1}x", EditorTheme.Current.Active);
        }

        int dropped = CountDroppedEvents();
        if (dropped > 0)
        {
            EditorGUI.TextColored($"{dropped} samples dropped from the event pool.", EditorTheme.Current.LogWarning);
        }
    }

    //统计当前保留窗口里丢掉的采样数
    private int CountDroppedEvents()
    {
        int dropped = 0;
        for (int index = 0; index < frameCount; index++) dropped += frames[index].DroppedEventCount;
        return dropped;
    }

    //读取选中帧的采样数
    private int CountEvents()
    {
        for (int index = 0; index < frameCount; index++)
        {
            if (frames[index].FrameIndex == selectedFrame) return frames[index].EventCount;
        }

        return selectedFrameEventCount;
    }

    //绘制帧耗时堆叠柱状图
    private void DrawFrameChart()
    {
        vector2 available = NativeEditorGUI.GetContentRegionAvail();
        if (available.x < 48.0f) return;

        vector2 origin = NativeEditorGUI.GetCursorScreenPos();
        vector2 size = new(available.x, ChartHeight);
        NativeEditorGUI.InvisibleButton("##profiler_chart", size);

        bool hovered = NativeEditorGUI.IsItemHovered();
        bool clicked = NativeEditorGUI.IsItemClicked();

        int displayCount = Math.Max(frameCount - 1, 0);
        if (displayCount <= 0)
        {
            NativeEditorGUI.DrawTextClipped(origin, new vector2(origin.x + available.x, origin.y + ChartHeight),
                new vector2(origin.x + 6.0f, origin.y + 6.0f), EditorTheme.Current.Text, "Waiting for frames...");
            return;
        }

        //一帧一个像素，宽度放不下时只画最近的若干帧；不够宽时靠右对齐，新帧始终贴着右边
        int visibleCount = Math.Min(displayCount, Math.Max((int)available.x, 1));
        int firstVisible = displayCount - visibleCount;
        float barsLeft = origin.x + available.x - visibleCount;

        //按分位定标；超出刻度的柱子会截平在顶端，真实峰值另在刻度文字里给出
        float scale = ChartHeight / Math.Max(chartScaleMicroseconds, 1);
        float bottom = origin.y + ChartHeight;

        int rectCount = 0;
        for (int index = firstVisible; index < displayCount; index++)
        {
            float left = barsLeft + (index - firstVisible);
            float right = left + 1.0f;
            float top = bottom;
            float frameTop = Math.Max(bottom - (float)frames[index].DeltaTimeMicroseconds * scale, origin.y);

            //分类分段自下而上堆叠
            for (int category = 0; category < CategoryCount; category++)
            {
                long value = frames[index].GetCategoryMicroseconds(category);
                if (value <= 0) continue;

                float segment = (float)value * scale;
                rectCount = EditorRects.Append(rects, rectCount, left, Math.Max(top - segment, origin.y), right, top, GetCategoryColor(category));
                top -= segment;
            }

            //限帧等待（深灰）：帧率节流主动睡掉的时间，不是空闲
            long wait = Math.Max(frames[index].WaitMicroseconds, 0);
            if (wait > 0)
            {
                float segment = (float)wait * scale;
                rectCount = EditorRects.Append(rects, rectCount, left, Math.Max(top - segment, origin.y), right, top, EditorTheme.Current.ProfileWait);
                top -= segment;
            }

            //其它未统计事项（浅灰）：既不在埋点里、也不是限帧等待的部分。
            //不足一个像素就不画：亚像素的矩形会渲染成断断续续的虚线，看着像噪点
            long other = frames[index].DeltaTimeMicroseconds - frames[index].RootMicroseconds - wait;
            if (other * scale >= 1.0f) rectCount = EditorRects.Append(rects, rectCount, left, frameTop, right, top, EditorTheme.Current.ProfileOther);

            //选中帧整条描亮
            if (frames[index].FrameIndex == selectedFrame)
            {
                rectCount = EditorRects.Append(rects, rectCount, left, frameTop, right, Math.Max(frameTop + 1.0f, bottom), EditorTheme.Current.Active);
            }

            if (rectCount >= rects.Length - 8) break;
        }

        //16.7 ms 参考线
        long reference = 16667;
        if (reference < chartScaleMicroseconds)
        {
            float referenceY = bottom - reference * scale;
            rectCount = EditorRects.Append(rects, rectCount, origin.x, referenceY, origin.x + available.x, referenceY + 1.0f, EditorTheme.Current.Border);
        }
        NativeEditorGUI.DrawRects(rects, rectCount);

        //刻度与真实峰值
        string scaleText = $"{ToMilliseconds(chartScaleMicroseconds):F2} ms";
        if (chartPeakMicroseconds > chartScaleMicroseconds)
        {
            scaleText += $"  |  max {ToMilliseconds(chartPeakMicroseconds):F2} ms";
        }
        NativeEditorGUI.DrawTextClipped(origin, new vector2(origin.x + available.x, origin.y + ChartHeight),
            new vector2(origin.x + 4.0f, origin.y + 2.0f), EditorTheme.Current.Text, scaleText);

        if (!hovered) return;

        //一帧一像素，横坐标直接就是帧下标
        vector2 mouse = NativeEditorGUI.GetMousePos();
        int hoveredIndex = firstVisible + (int)(mouse.x - barsLeft);
        if (hoveredIndex < firstVisible || hoveredIndex >= displayCount) return;

        if (clicked)
        {
            selectedFrame = frames[hoveredIndex].FrameIndex;
            followLatest = false;
            RequestImmediateRefresh();
        }

        ProfileFrameSummary hoveredFrame = frames[hoveredIndex];
        EditorGUI.SetTooltip($"Frame {hoveredFrame.FrameIndex}\n"
            + $"Frame time {ToMilliseconds(hoveredFrame.DeltaTimeMicroseconds):F2} ms\n"
            + $"Measured work {ToMilliseconds(hoveredFrame.RootMicroseconds):F2} ms\n"
            + $"Dominant: {GetDominantCategory(hoveredFrame)}\n"
            + "Click to inspect this frame.");
    }

    //读取该帧占比最大的分类名
    private static string GetDominantCategory(ProfileFrameSummary frame)
    {
        int dominant = -1;
        long best = 0;
        for (int category = 0; category < CategoryCount; category++)
        {
            long value = frame.GetCategoryMicroseconds(category);
            if (value <= best) continue;
            best = value;
            dominant = category;
        }

        return dominant < 0 ? "None" : CategoryNames[dominant];
    }

    //绘制分类图例：色块负责表色，文字用正常文字色——深色的分块色当文字根本读不出来
    private void DrawLegend()
    {
        int swatchCount = 0;
        for (int index = 0; index <= CategoryCount; index++)
        {
            if (index > 0) EditorGUI.SameLine();

            bool waitEntry = index == CategoryCount;
            color swatchColor = waitEntry ? EditorTheme.Current.ProfileWait : GetCategoryColor(index);
            string label = waitEntry ? "Wait" : CategoryNames[index];

            //用不可见按钮占位，拿到这一格的屏幕位置再往里画色块
            vector2 slot = NativeEditorGUI.GetCursorScreenPos();
            NativeEditorGUI.InvisibleButton($"##profiler_legend_{index}", new vector2(LegendSwatchWidth, LegendRowHeight));
            swatchCount = EditorRects.Append(rects, swatchCount, slot.x, slot.y + 3.0f, slot.x + LegendSwatchWidth, slot.y + LegendRowHeight - 3.0f, swatchColor);

            EditorGUI.SameLine();
            EditorGUI.TextColored(label, EditorTheme.Current.Text);
        }

        NativeEditorGUI.DrawRects(rects, swatchCount);
    }

    //绘制单帧分析区
    private void DrawDetail()
    {
        if (selectedFrame < 0)
        {
            EditorGUI.Label("No frames captured yet.");
            return;
        }

        if (nodeRows.Count == 0)
        {
            EditorGUI.Label("No samples in the selected frame.");
            return;
        }

        if (timelineView) DrawTimeline();
        else DrawHierarchy();
    }

    //绘制层级表
    private void DrawHierarchy()
    {
        if (EditorGUI.BeginCombo("Sort##profiler_sort", SortNames[sortMode]))
        {
            try
            {
                for (int index = 0; index < SortNames.Length; index++)
                {
                    if (EditorGUI.Selectable(SortNames[index], sortMode == index)) sortMode = index;
                }
            }
            finally
            {
                EditorGUI.EndCombo();
            }
        }

        BuildSortedRows();

        float width = 0.0f;
        bool childVisible = NativeEditorGUI.BeginChild("##profiler_hierarchy", ref width);
        try
        {
            if (!childVisible) return;

            bool tableVisible = EditorGUI.BeginTable("##profiler_nodes", 5);
            try
            {
                if (!tableVisible) return;

                EditorGUI.TableSetupColumn("Name");
                EditorGUI.TableSetupColumn("Total ms", 84.0f, true);
                EditorGUI.TableSetupColumn("Self ms", 84.0f, true);
                EditorGUI.TableSetupColumn("Calls", 56.0f, true);
                EditorGUI.TableSetupColumn("%", 56.0f, true);
                EditorGUI.TableHeadersRow();

                for (int index = 0; index < sortedRows.Count; index++)
                {
                    NodeRow row = nodeRows[sortedRows[index]];
                    EditorGUI.TableNextRow();

                    EditorGUI.TableSetColumnIndex(0);
                    //甘特图之外用缩进表达层级
                    string indent = new string(' ', Math.Max(row.Depth, 0) * 3);
                    EditorGUI.TextColored(indent + GetName(row.NameId), GetCategoryColor(row.Category));

                    EditorGUI.TableSetColumnIndex(1);
                    EditorGUI.Label($"{ToMilliseconds(row.Total):F3}");
                    EditorGUI.TableSetColumnIndex(2);
                    EditorGUI.Label($"{ToMilliseconds(Math.Max(row.Self, 0)):F3}");
                    EditorGUI.TableSetColumnIndex(3);
                    EditorGUI.Label(row.Calls.ToString());
                    EditorGUI.TableSetColumnIndex(4);
                    EditorGUI.Label(selectedFrameMicroseconds > 0
                        ? $"{100.0 * row.Total / selectedFrameMicroseconds:F1}"
                        : "0.0");
                }
            }
            finally
            {
                if (tableVisible) EditorGUI.EndTable();
            }
        }
        finally
        {
            NativeEditorGUI.EndChild();
        }
    }

    //按当前排序方式整理行顺序
    private void BuildSortedRows()
    {
        sortedRows.Clear();
        for (int index = 0; index < nodeRows.Count; index++) sortedRows.Add(index);
        if (sortMode == 0) return;

        //Tree 之外的排序都按降序，让最耗时的排在前面
        sortedRows.Sort((left, right) => sortMode switch
        {
            1 => nodeRows[right].Total.CompareTo(nodeRows[left].Total),
            2 => nodeRows[right].Self.CompareTo(nodeRows[left].Self),
            _ => nodeRows[right].Calls.CompareTo(nodeRows[left].Calls),
        });
    }

    //绘制单帧甘特图：一个深度一行，同一深度的节点并排在那一行上
    private void DrawTimeline()
    {
        float width = 0.0f;
        bool childVisible = NativeEditorGUI.BeginChild("##profiler_timeline", ref width);
        try
        {
            if (!childVisible) return;

            vector2 available = NativeEditorGUI.GetContentRegionAvail();
            int rowCount = Math.Max(maxEventDepth, 1);
            float rowsHeight = rowCount * TimelineRowHeight;

            vector2 origin = NativeEditorGUI.GetCursorScreenPos();
            NativeEditorGUI.InvisibleButton("##profiler_timeline_canvas",
                new vector2(available.x, TimelineAxisHeight + rowsHeight + 4.0f));

            bool hovered = NativeEditorGUI.IsItemHovered();

            //时间轴按帧耗时与最晚的采样结束时刻取大
            long axis = Math.Max(selectedFrameMicroseconds, 1);
            for (int index = 0; index < selectedFrameEventCount; index++)
                axis = Math.Max(axis, events[index].StartMicroseconds + events[index].DurationMicroseconds);

            float plotLeft = origin.x;
            float plotWidth = Math.Max(available.x - 2.0f, 40.0f);
            float rowsTop = origin.y + TimelineAxisHeight;

            //滚轮以光标下的时刻为锚点缩放，双击回到整帧
            if (hovered)
            {
                float wheel = NativeEditorGUI.GetMouseWheel();
                if (wheel != 0.0f) ApplyTimelineZoom(wheel, axis, plotLeft, plotWidth);
                if (NativeEditorGUI.IsItemDoubleClicked()) ResetTimelineZoom();
            }

            long visibleSpan = Math.Max((long)(axis / timelineZoom), 1);
            timelinePan = Math.Clamp(timelinePan, 0, Math.Max(axis - visibleSpan, 0));

            //先把每次调用的横向区间算出来，画条与写名字共用同一份；窗口外的不画
            for (int index = 0; index < selectedFrameEventCount; index++)
            {
                ProfileEvent profileEvent = events[index];
                if (profileEvent.StartMicroseconds + profileEvent.DurationMicroseconds < timelinePan
                    || profileEvent.StartMicroseconds > timelinePan + visibleSpan)
                {
                    eventLeft[index] = -1.0f;
                    eventRight[index] = -1.0f;
                    continue;
                }

                eventLeft[index] = plotLeft + plotWidth * ((float)(profileEvent.StartMicroseconds - timelinePan) / visibleSpan);
                eventRight[index] = Math.Min(
                    eventLeft[index] + Math.Max(plotWidth * ((float)profileEvent.DurationMicroseconds / visibleSpan), 1.5f),
                    plotLeft + plotWidth);
            }

            int rectCount = 0;

            //时间轴刻度
            for (int step = 0; step <= 4; step++)
            {
                float ratio = step / 4.0f;
                float x = plotLeft + plotWidth * ratio;
                rectCount = EditorRects.Append(rects, rectCount, x, rowsTop - 4.0f, x + 1.0f, rowsTop + rowsHeight, EditorTheme.Current.Border);
                NativeEditorGUI.DrawTextClipped(origin, new vector2(plotLeft + plotWidth, rowsTop),
                    new vector2(x + 2.0f, origin.y), EditorTheme.Current.Text,
                    $"{ToMilliseconds(timelinePan + (long)(visibleSpan * ratio)):F2}");
            }

            //一次调用画在它所属深度的那一行
            for (int index = 0; index < selectedFrameEventCount; index++)
            {
                if (eventRight[index] < 0.0f) continue;

                float rowTop = rowsTop + Math.Max(events[index].Depth, 0) * TimelineRowHeight;
                rectCount = EditorRects.Append(rects, rectCount, eventLeft[index], rowTop + 1.0f, eventRight[index],
                    rowTop + TimelineRowHeight - 1.0f, GetCategoryColor(events[index].Category));

                if (rectCount >= rects.Length) break;
            }

            NativeEditorGUI.DrawRects(rects, rectCount);

            //名字写在各自的时间条里，条太窄就裁掉
            for (int index = 0; index < selectedFrameEventCount; index++)
            {
                if (eventRight[index] < 0.0f) continue;

                float rowTop = rowsTop + Math.Max(events[index].Depth, 0) * TimelineRowHeight;
                NativeEditorGUI.DrawTextClipped(new vector2(eventLeft[index], rowTop),
                    new vector2(eventRight[index], rowTop + TimelineRowHeight),
                    new vector2(eventLeft[index] + 3.0f, rowTop + 1.0f),
                    EditorTheme.Current.Text, GetName(events[index].NameId));
            }

            if (!hovered) return;

            //鼠标落在哪一层、哪一段时间上，直接反推
            vector2 mouse = NativeEditorGUI.GetMousePos();
            float hoverRatio = (mouse.x - plotLeft) / plotWidth;
            if (hoverRatio < 0.0f || hoverRatio > 1.0f) return;

            int hoveredDepth = (int)((mouse.y - rowsTop) / TimelineRowHeight);
            if (hoveredDepth < 0 || hoveredDepth >= rowCount) return;

            long hoveredTime = (long)(axis * hoverRatio);
            for (int index = 0; index < selectedFrameEventCount; index++)
            {
                ProfileEvent profileEvent = events[index];
                if (profileEvent.Depth != hoveredDepth) continue;
                if (hoveredTime < profileEvent.StartMicroseconds) continue;
                if (hoveredTime > profileEvent.StartMicroseconds + profileEvent.DurationMicroseconds) continue;

                EditorGUI.SetTooltip($"{GetName(profileEvent.NameId)}\n"
                    + $"This call {ToMilliseconds(profileEvent.DurationMicroseconds):F3} ms @ {ToMilliseconds(profileEvent.StartMicroseconds):F3} ms\n"
                    + GetAggregateText(profileEvent.NodeId)
                    + $"Depth {profileEvent.Depth}");
                break;
            }
        }
        finally
        {
            NativeEditorGUI.EndChild();
        }
    }

    //按光标位置为锚点缩放时间轴，缩放前后光标下的时刻保持不动
    private void ApplyTimelineZoom(float wheel, long axis, float plotLeft, float plotWidth)
    {
        vector2 mouse = NativeEditorGUI.GetMousePos();
        float ratio = Math.Clamp((mouse.x - plotLeft) / plotWidth, 0.0f, 1.0f);

        long visibleSpan = Math.Max((long)(axis / timelineZoom), 1);
        long anchor = timelinePan + (long)(visibleSpan * ratio);

        timelineZoom = Math.Clamp(timelineZoom * (1.0f + wheel * TimelineZoomStep), 1.0f, TimelineMaxZoom);

        long newSpan = Math.Max((long)(axis / timelineZoom), 1);
        timelinePan = Math.Clamp(anchor - (long)(newSpan * ratio), 0, Math.Max(axis - newSpan, 0));
    }

    //回到整帧视图
    private void ResetTimelineZoom()
    {
        timelineZoom = 1.0f;
        timelinePan = 0;
    }

    //读取某个采样节点在这一帧里的合计信息
    private string GetAggregateText(int nodeId)
    {
        if (!nodeIndexById.TryGetValue(nodeId, out int rowIndex)) return string.Empty;

        NodeRow node = nodeRows[rowIndex];
        return $"Total {ToMilliseconds(node.Total):F3} ms\n"
            + $"Self {ToMilliseconds(Math.Max(node.Self, 0)):F3} ms\n"
            + $"Calls {node.Calls}\n";
    }

    //聚合选中帧的事件，事件顺序就是树的先序
    private void BuildNodeRows()
    {
        nodeRows.Clear();
        nodeIndexById.Clear();
        maxEventDepth = 0;

        for (int index = 0; index < selectedFrameEventCount; index++)
        {
            ProfileEvent profileEvent = events[index];

            //甘特图按深度分行，先把最深层数记下来
            maxEventDepth = Math.Max(maxEventDepth, profileEvent.Depth + 1);

            if (!nodeIndexById.TryGetValue(profileEvent.NodeId, out int rowIndex))
            {
                nodeIndexById.Add(profileEvent.NodeId, nodeRows.Count);
                rowIndex = nodeRows.Count;
                nodeRows.Add(new NodeRow
                {
                    NodeId = profileEvent.NodeId,
                    ParentNodeId = profileEvent.ParentNodeId,
                    NameId = profileEvent.NameId,
                    Category = profileEvent.Category,
                    Depth = profileEvent.Depth,
                    Total = 0,
                    Self = 0,
                    Calls = 0,
                });
            }

            NodeRow row = nodeRows[rowIndex];
            row.Total += profileEvent.DurationMicroseconds;
            row.Calls++;
            nodeRows[rowIndex] = row;
        }

        //自耗时先当作总耗时，再逐个把子节点耗时减掉
        for (int index = 0; index < nodeRows.Count; index++)
        {
            NodeRow row = nodeRows[index];
            row.Self = row.Total;
            nodeRows[index] = row;
        }

        for (int index = 0; index < nodeRows.Count; index++)
        {
            NodeRow row = nodeRows[index];
            if (row.ParentNodeId < 0) continue;
            if (!nodeIndexById.TryGetValue(row.ParentNodeId, out int parentIndex)) continue;

            NodeRow parent = nodeRows[parentIndex];
            parent.Self -= row.Total;
            nodeRows[parentIndex] = parent;
        }
    }

    //读取采样名，按编号缓存
    private string GetName(int nameId)
    {
        if (nameCache.TryGetValue(nameId, out string? cached)) return cached;

        string name = NativeEditorProfiler.CopyName(nameId);
        nameCache[nameId] = name;
        return name;
    }

    //读取分类配色
    private static color GetCategoryColor(int category)
    {
        EditorTheme theme = EditorTheme.Current;
        return category switch
        {
            1 => theme.ProfileScript,
            2 => theme.ProfileRender,
            3 => theme.ProfilePhysics,
            4 => theme.ProfileFileIO,
            5 => theme.ProfileEditor,
            _ => theme.ProfileOther,
        };
    }

    //微秒换算成毫秒
    private static double ToMilliseconds(long microseconds) => microseconds / 1000.0;

    //一帧里的一个采样节点
    private struct NodeRow
    {
        public int NodeId;
        public int ParentNodeId;
        public int NameId;
        public int Category;
        public int Depth;
        public long Total;
        public long Self;
        public int Calls;
    }
}
