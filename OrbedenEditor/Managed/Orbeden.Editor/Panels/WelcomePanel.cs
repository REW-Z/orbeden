using Orbeden;

namespace OrbedenEditor;

/// <summary>未打开项目时的欢迎工作区，显示最近项目和内容最后编辑时间。</summary>
internal sealed class WelcomePanel : EditorPanel
{
    private readonly EditorRectPrimitive[] rects = new EditorRectPrimitive[8];
    private RecentProjectView[] projects = [];
    private Task<RecentProjectView[]>? scan;
    private CancellationTokenSource? cancellation;
    private string scanError = string.Empty;

    public override EditorPanelInfo Info => new("welcome", "Welcome", false,
        new vector2(960, 640), PanelDockPlacement.Center, 1.0f, 0, fixedWorkspace: true, showBorder: false);

    /// <summary>显示缓存记录，并在后台刷新项目可用性和内容修改时间。</summary>
    public override void OnShown()
    {
        cancellation?.Cancel();
        cancellation?.Dispose();
        cancellation = new();
        scanError = string.Empty;
        projects = EditorRecentProjects.GetProjects().Select(project => new RecentProjectView(project, true)).ToArray();
        scan = EditorRecentProjects.ScanAsync(cancellation.Token);
    }

    /// <summary>离开欢迎页后停止目录扫描，不占用编辑工作区。</summary>
    public override void OnHidden()
    {
        cancellation?.Cancel();
        cancellation?.Dispose();
        cancellation = null;
        scan = null;
    }

    /// <summary>绘制响应式欢迎页、操作入口与最近项目列表。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        if (scan is { IsCompleted: true })
        {
            if (scan.IsCompletedSuccessfully)
            {
                projects = scan.Result;
                EditorRecentProjects.UpdateTimes(projects);
            }
            else if (scan.Exception != null) scanError = "Could not refresh project activity.";
            scan = null;
        }
        if (scan != null) EditorApplication.RequestRepaint();

        EditorTheme theme = EditorTheme.Current;
        float available = NativeEditorGUI.GetContentRegionAvail().x;
        float margin = available < 480 ? 16 : Math.Max(32, (available - 920) * 0.5f);
        float width = Math.Max(1, available - margin * 2);
        vector2 origin = NativeEditorGUI.GetCursorScreenPos();
        NativeEditorGUI.InvisibleButton("##welcome_hero", new vector2(Math.Max(1, available), 174));
        float left = origin.x + margin;
        float right = left + width;
        Fill(left, origin.y + 35, 7, 62, theme.Active, 3);
        Text(left + 23, origin.y + 34, right, 16, "WELCOME TO", Muted(theme), 12);
        Text(left + 21, origin.y + 56, right, 46, "ORBEDEN", theme.Text, 36);
        Text(left, origin.y + 118, right, 28, "Your next world starts here.", Muted(theme), 16);

        DrawActions(available, margin, width, theme);
        string status = EditorApplication.GetProjectText(EditorProjectField.Status);
        string error = status.Length > 0 ? status : scanError.Length > 0 ? scanError : EditorRecentProjects.Error;
        if (error.Length > 0)
        {
            origin = NativeEditorGUI.GetCursorScreenPos();
            NativeEditorGUI.InvisibleButton("##welcome_error", new vector2(Math.Max(1, available), 42));
            Fill(left, origin.y + 4, width, 32, theme.Header, 6);
            Text(left + 12, origin.y + 11, right - 12, 20, error, theme.LogError, 12);
            if (NativeEditorGUI.IsItemHovered()) EditorGUI.SetTooltip(error);
        }

        origin = NativeEditorGUI.GetCursorScreenPos();
        bool refresh = NativeEditorGUI.InvisibleButton("##recent_heading", new vector2(Math.Max(1, available), 60));
        vector2 mouse = NativeEditorGUI.GetMousePos();
        Text(left, origin.y + 26, right - 75, 22, $"RECENT PROJECTS  /  {projects.Length:00}", Muted(theme), 12);
        bool overRefresh = mouse.x >= right - 72 && mouse.x <= right && NativeEditorGUI.IsItemHovered();
        Text(right - 66, origin.y + 26, right, 22, scan != null ? "Loading" : "Refresh", overRefresh ? theme.Active : Muted(theme), 12);
        if (refresh && overRefresh && scan == null) OnShown();

        if (projects.Length == 0)
        {
            origin = NativeEditorGUI.GetCursorScreenPos();
            NativeEditorGUI.InvisibleButton("##no_projects", new vector2(Math.Max(1, available), 130));
            Fill(left, origin.y, width, 114, theme.Header, 10);
            Text(left + 24, origin.y + 27, right - 24, 28, "A fresh start", theme.Text, 19);
            Text(left + 24, origin.y + 66, right - 24, 28, "Open or create a project. It will appear here next time.", Muted(theme), 13);
        }
        string? remove = null;
        foreach (RecentProjectView project in projects)
            if (DrawProject(project, available, margin, width, theme)) remove = project.Project.ProjectFile;
        if (remove != null)
        {
            cancellation?.Cancel();
            scan = null;
            EditorRecentProjects.Remove(remove);
            projects = projects.Where(item => item.Project.ProjectFile != remove).ToArray();
        }
    }

    /// <summary>绘制标题下方并排的项目操作卡片。</summary>
    private void DrawActions(float available, float margin, float width, EditorTheme theme)
    {
        vector2 origin = NativeEditorGUI.GetCursorScreenPos();
        bool clicked = NativeEditorGUI.InvisibleButton("##welcome_actions", new vector2(Math.Max(1, available), 82));
        bool hovered = NativeEditorGUI.IsItemHovered();
        vector2 mouse = NativeEditorGUI.GetMousePos();
        float half = (width - 12) * 0.5f;
        for (int index = 0; index < 2; ++index)
        {
            float left = origin.x + margin + index * (half + 12);
            bool hot = hovered && mouse.x >= left && mouse.x <= left + half && mouse.y <= origin.y + 66;
            Fill(left, origin.y, half, 66, hot ? theme.Hovered : theme.Header, 9);
            Fill(left, origin.y, 3, 66, index == 0 ? theme.Active : theme.Border, 2);
            float inset = width < 480 ? 14 : 20;
            Text(left + inset, origin.y + 13, left + half - 12, 25, index == 0 ? "Open project" : "+ New project", theme.Text, 16);
            if (width >= 480)
                Text(left + inset, origin.y + 40, left + half - 12, 18, index == 0 ? "Continue an existing project" : "Create a new workspace", Muted(theme), 11);
            if (clicked && hot) EditorApplication.RequestProjectAction(index == 0 ? 1 : 2);
        }
    }

    /// <summary>绘制单个最近项目；卡片单击打开，右键可移除失效记录。</summary>
    private bool DrawProject(RecentProjectView view, float available, float margin, float width, EditorTheme theme)
    {
        RecentProject project = view.Project;
        bool narrow = width < 660;
        float height = narrow ? 106 : 86;
        vector2 origin = NativeEditorGUI.GetCursorScreenPos();
        bool clicked = NativeEditorGUI.InvisibleButton("##recent_" + project.ProjectFile, new vector2(Math.Max(1, available), height + 8));
        vector2 mouse = NativeEditorGUI.GetMousePos();
        float left = origin.x + margin, right = left + width;
        bool hot = NativeEditorGUI.IsItemHovered() && mouse.x >= left && mouse.x <= right && mouse.y <= origin.y + height;
        Fill(left, origin.y, width, height, hot ? theme.Active : theme.Border, 9);
        Fill(left + 1, origin.y + 1, width - 2, height - 2, hot && view.Exists ? theme.Hovered : theme.Header, 8);
        if (width > 360)
        {
            Fill(left + 17, origin.y + 22, 36, 36, theme.Control, 7);
            Text(left + 27, origin.y + 30, left + 49, 22, System.Globalization.StringInfo.GetNextTextElement(project.Name).ToUpperInvariant(), view.Exists ? theme.Active : Muted(theme), 18);
        }
        float nameLeft = left + (width > 360 ? 70 : 16);
        float textRight = narrow ? right - 20 : right - 220;
        Text(nameLeft, origin.y + 18, textRight, 26, project.Name, view.Exists ? theme.Text : Muted(theme), 17);
        Text(nameLeft, origin.y + 48, textRight, 21, Path.GetDirectoryName(project.ProjectFile) ?? project.ProjectFile, Muted(theme), 12);
        string time = !view.Exists ? "Project not found" : project.LastEditedUtc == DateTime.MinValue
            ? (scan != null ? "Reading activity..." : "Time unavailable") : project.LastEditedUtc.ToLocalTime().ToString("yyyy-MM-dd  HH:mm");
        if (narrow) Text(nameLeft, origin.y + 78, right - 20, 20, view.Exists ? "Last edited  " + time : time, Muted(theme), 11);
        else
        {
            Text(right - 194, origin.y + 20, right - 18, 16, "LAST EDITED", Muted(theme), 10);
            Text(right - 194, origin.y + 46, right - 18, 24, time, view.Exists ? theme.Text : theme.LogWarning, 12);
        }
        if (hot) EditorGUI.SetTooltip(view.Exists ? project.ProjectFile + "\nClick to open" : "This project was moved or deleted. Right-click to remove it from this list.");
        bool remove = false;
        if (EditorGUI.BeginPopupContextItem("recent_menu_" + project.ProjectFile))
        {
            try { remove = EditorGUI.MenuItem("Remove from recent projects"); }
            finally { EditorGUI.EndPopup(); }
        }
        if (clicked && hot && view.Exists) EditorApplication.RequestProjectAction(0, project.ProjectFile);
        return remove;
    }

    /// <summary>绘制适应主题的圆角实心块。</summary>
    private void Fill(float x, float y, float width, float height, color fill, float radius)
    {
        if (width <= 0 || height <= 0) return;
        int count = EditorRects.Append(rects, 0, x, y, x + width, y + height, fill);
        rects[0].Rounding = radius;
        NativeEditorGUI.DrawRects(rects, count);
    }

    /// <summary>在指定区域绘制可缩放的单行文本，长路径自动裁剪。</summary>
    private static void Text(float x, float y, float right, float height, string text, color tint, float size)
    {
        if (right <= x) return;
        NativeEditorGUI.DrawTextClipped(new(x, y), new(right, y + height), new(x, y), tint, text, size);
    }

    /// <summary>从当前主题生成次要文本颜色。</summary>
    private static color Muted(EditorTheme theme) => new(theme.Text.r, theme.Text.g, theme.Text.b, 0.58f);
}
