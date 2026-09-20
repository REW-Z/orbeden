using Orbeden;

namespace OrbedenEditor;

/// <summary>显示原生与托管两侧日志的控制台面板。</summary>
internal sealed class ConsolePanel : EditorPanel
{
    //详情区高度
    private const float DetailsHeight = 88.0f;


    //本地镜像上限，与原生保留窗口容量一致
    private const int MirrorCapacity = 512;

    private readonly List<ConsoleEntry> entries = new();
    private readonly List<ConsoleRow> rows = new();
    private readonly EditorRectPrimitive[] rects = new EditorRectPrimitive[1024];
    private readonly bool[] levelVisible = { true, true, true };
    private readonly int[] levelCounts = new int[3];
    private string search = string.Empty;
    private bool collapseDuplicates = true;
    private bool followTail = true;
    private long cursor;
    private long selectedRevision = -1;
    private float rowPitch;

    //Clear 按钮宽度：首帧按估值摆，量到实际宽度后贴右就准了
    private float clearButtonWidth = 52.0f;

    public override EditorPanelInfo Info => new("console", "Console", true,
        new vector2(760, 220), PanelDockPlacement.Bottom, 0.25f, 130);

    /// <summary>绘制日志过滤栏、列表与选中条目详情。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        //不主动请求重绘：原生日志写入时会唤醒消息循环，日志自然会来
        SyncEntries();

        //焦点离开面板就取消选中，详情区随之收起
        if (selectedRevision >= 0 && !NativeEditorGUI.IsWindowFocused())
        {
            selectedRevision = -1;

            //面板平时不请求重绘，选中变化要自己催一帧，否则布局要等到下次鼠标动才修正
            EditorApplication.RequestRepaint();
        }

        DrawToolbar();
        EditorGUI.Separator();
        DrawEntryList();
        DrawEntryDetails();
    }

    //把原生保留窗口里的新日志同步到本地镜像，返回是否读到了新条目
    private bool SyncEntries()
    {
        if (!NativeEditorLog.IsAvailable) return false;

        bool ingested = false;
        int retained = NativeEditorLog.GetRange(out long oldest, out long newest);
        if (retained <= 0)
        {
            if (entries.Count > 0) ingested = true;
            entries.Clear();
            cursor = newest;
            return ingested;
        }

        //游标落在窗口之外说明已被挤出或被清空，整体重载
        if (cursor < oldest || cursor > newest)
        {
            if (entries.Count > 0) ingested = true;
            entries.Clear();
            selectedRevision = -1;
            cursor = oldest;
        }

        while (cursor < newest)
        {
            string? message = NativeEditorLog.CopyEntry(cursor, out int level, out long timestamp);
            if (message == null) break;

            entries.Add(new ConsoleEntry(cursor, level, timestamp, message));
            cursor++;
            ingested = true;
        }

        //被挤出窗口的旧条目在镜像里也要丢掉，否则列表会无限增长
        if (entries.Count > MirrorCapacity) entries.RemoveRange(0, entries.Count - MirrorCapacity);
        return ingested;
    }

    //绘制工具条：搜索框在最左，开关按钮居中，Clear 贴右
    private void DrawToolbar()
    {
        NativeEditorLog.GetCounts(levelCounts);
        vector2 available = NativeEditorGUI.GetContentRegionAvail();

        //搜索框不带标签，文本一变就实时过滤，不需要触发动作
        EditorGUI.InputText("##console_search", ref search);
        if (NativeEditorGUI.IsItemHovered()) EditorGUI.SetTooltip("Filter by substring (live)");

        EditorGUI.SameLine();
        if (EditorGUI.ToggleButton($"Info {levelCounts[EditorLogLevel.Info]}##console_info", levelVisible[EditorLogLevel.Info]))
            levelVisible[EditorLogLevel.Info] = !levelVisible[EditorLogLevel.Info];

        EditorGUI.SameLine();
        if (EditorGUI.ToggleButton($"Warn {levelCounts[EditorLogLevel.Warning]}##console_warning", levelVisible[EditorLogLevel.Warning]))
            levelVisible[EditorLogLevel.Warning] = !levelVisible[EditorLogLevel.Warning];

        EditorGUI.SameLine();
        if (EditorGUI.ToggleButton($"Err {levelCounts[EditorLogLevel.Error]}##console_error", levelVisible[EditorLogLevel.Error]))
            levelVisible[EditorLogLevel.Error] = !levelVisible[EditorLogLevel.Error];

        EditorGUI.SameLine();
        if (EditorGUI.ToggleButton("Collapse##console_collapse", collapseDuplicates)) collapseDuplicates = !collapseDuplicates;

        EditorGUI.SameLine();
        if (EditorGUI.ToggleButton("Follow##console_follow", followTail)) followTail = !followTail;

        //Clear 贴右；按钮宽度首帧量一次，量到后催一帧让对齐生效
        float clearOffset = available.x - clearButtonWidth;
        if (clearOffset > 0.0f) EditorGUI.SameLine(clearOffset);
        else EditorGUI.SameLine();

        vector2 beforeClear = NativeEditorGUI.GetCursorScreenPos();
        if (EditorGUI.Button("Clear##console_clear")) ClearEntries();
        vector2 afterClear = NativeEditorGUI.GetCursorScreenPos();

        float measured = afterClear.x - beforeClear.x - EditorTheme.Current.SpacingX;
        if (measured > 0.0f && Math.Abs(measured - clearButtonWidth) > 0.5f)
        {
            clearButtonWidth = measured;
            EditorApplication.RequestRepaint();
        }
    }

    //清空原生保留窗口与本地镜像
    private void ClearEntries()
    {
        NativeEditorLog.Clear();
        entries.Clear();
        cursor = 0;
        selectedRevision = -1;
    }

    //绘制日志列表
    private void DrawEntryList()
    {
        BuildRows();

        //详情区只在选中时占位置
        vector2 available = NativeEditorGUI.GetContentRegionAvail();
        float listHeight = Math.Max(available.y - (selectedRevision >= 0 ? DetailsHeight : 0.0f), 60.0f);

        float width = 0.0f;
        bool childVisible = NativeEditorGUI.BeginChild("##console_entries", ref width, listHeight);
        try
        {
            if (!childVisible) return;

            vector2 listTop = NativeEditorGUI.GetCursorScreenPos();

            //隔行底色：行高用上一帧量到的，首帧还没有就跳过
            if (rowPitch > 0.0f)
            {
                int stripeCount = 0;
                for (int index = 1; index < rows.Count; index += 2)
                {
                    float stripeTop = listTop.y + index * rowPitch;
                    stripeCount = EditorRects.Append(rects, stripeCount,
                        listTop.x, stripeTop, listTop.x + available.x, stripeTop + rowPitch,
                        EditorTheme.Current.ConsoleRowStripe);
                }
                NativeEditorGUI.DrawRects(rects, stripeCount);
            }

            for (int index = 0; index < rows.Count; index++) DrawRow(rows[index]);

            //量一次行距给下一帧的底色用
            vector2 listBottom = NativeEditorGUI.GetCursorScreenPos();
            if (rows.Count > 1) rowPitch = Math.Max((listBottom.y - listTop.y) / rows.Count, 0.0f);

            //跟随尾部时把视图拉到最新一条
            if (followTail) EditorGUI.SetScrollHereY(1.0f);
        }
        finally
        {
            NativeEditorGUI.EndChild();
        }
    }

    //按级别与搜索词过滤，必要时折叠连续重复
    private void BuildRows()
    {
        rows.Clear();

        for (int index = 0; index < entries.Count; index++)
        {
            ConsoleEntry entry = entries[index];
            if (!IsVisible(entry)) continue;

            //折叠只合并相邻重复，被过滤掉的条目会打断连续性
            if (collapseDuplicates && rows.Count > 0)
            {
                ConsoleRow last = rows[^1];
                ConsoleEntry previous = entries[last.Index];
                if (last.Index == index - 1
                    && previous.Level == entry.Level
                    && string.Equals(previous.Message, entry.Message, StringComparison.Ordinal))
                {
                    last.RepeatCount++;
                    rows[^1] = last;
                    continue;
                }
            }

            rows.Add(new ConsoleRow(index, 1));
        }
    }

    //判断条目是否通过级别与搜索过滤
    private bool IsVisible(ConsoleEntry entry)
    {
        if (entry.Level < 0 || entry.Level >= levelVisible.Length || !levelVisible[entry.Level]) return false;
        if (search.Length == 0) return true;

        return entry.Message.Contains(search, StringComparison.OrdinalIgnoreCase);
    }

    //绘制一行日志，用级别配色标出文本
    private void DrawRow(ConsoleRow row)
    {
        ConsoleEntry entry = entries[row.Index];
        bool selected = entry.Revision == selectedRevision;

        string text = row.RepeatCount > 1 ? $"{entry.Message} ({row.RepeatCount})" : entry.Message;
        string label = $"{FormatTimestamp(entry.TimestampMilliseconds)}  {text}";

        //选择项只负责整行命中与高亮，文本另画一层才能上色；再点一次取消选中
        if (EditorGUI.Selectable($"##console_row_{row.Index}", selected))
        {
            selectedRevision = selected ? -1 : entry.Revision;

            //选中当帧列表还是按"无详情"算的高度，催一帧让布局立刻修正
            EditorApplication.RequestRepaint();
        }
        EditorGUI.SameLine();
        EditorGUI.TextColored(label, GetLevelColor(entry.Level));
    }

    //绘制选中条目的完整文本；没有选中就不占位置
    private void DrawEntryDetails()
    {
        ConsoleEntry? selected = FindSelectedEntry();
        if (selected == null) return;

        ConsoleEntry entry = selected.Value;
        EditorGUI.Separator();
        EditorGUI.TextColored(FormatTimestamp(entry.TimestampMilliseconds), GetLevelColor(entry.Level));

        //用只读多行输入框，才能框选和复制
        NativeEditorGUI.InputTextMultiline("##console_details", entry.Message, DetailsHeight);
    }

    //查找当前选中的日志条目
    private ConsoleEntry? FindSelectedEntry()
    {
        if (selectedRevision < 0) return null;

        for (int index = entries.Count - 1; index >= 0; index--)
        {
            if (entries[index].Revision == selectedRevision) return entries[index];
        }

        return null;
    }

    //读取级别配色
    private static color GetLevelColor(int level)
    {
        EditorTheme theme = EditorTheme.Current;
        return level switch
        {
            EditorLogLevel.Warning => theme.LogWarning,
            EditorLogLevel.Error => theme.LogError,
            _ => theme.LogInfo,
        };
    }

    //把墙钟毫秒格式化成时分秒毫秒
    private static string FormatTimestamp(long timestampMilliseconds)
    {
        //越界时间戳退回占位文本，避免格式化抛异常
        if (timestampMilliseconds <= 0 || timestampMilliseconds > 253402300799999) return "--:--:--.---";

        return DateTimeOffset.FromUnixTimeMilliseconds(timestampMilliseconds).LocalDateTime.ToString("HH:mm:ss.fff");
    }

    //一条日志的本地镜像
    private readonly struct ConsoleEntry
    {
        public ConsoleEntry(long revision, int level, long timestampMilliseconds, string message)
        {
            Revision = revision;
            Level = level;
            TimestampMilliseconds = timestampMilliseconds;
            Message = message;
        }

        public long Revision { get; }
        public int Level { get; }
        public long TimestampMilliseconds { get; }
        public string Message { get; }
    }

    //列表行，Index 指向镜像条目，RepeatCount 是折叠后的重复条数
    private struct ConsoleRow
    {
        public ConsoleRow(int index, int repeatCount)
        {
            Index = index;
            RepeatCount = repeatCount;
        }

        public int Index;
        public int RepeatCount;
    }
}
