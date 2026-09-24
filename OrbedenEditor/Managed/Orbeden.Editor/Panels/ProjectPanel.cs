using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

/// <summary>ProjectPanel 右键扩展收到的资源上下文。</summary>
public readonly struct ProjectAssetContext
{
    public string FullPath { get; }
    public string ResourceKey { get; }
    public bool IsDirectory { get; }

    /// <summary>创建一个资源右键上下文。</summary>
    public ProjectAssetContext(string fullPath, string resourceKey, bool isDirectory)
    {
        FullPath = fullPath;
        ResourceKey = resourceKey;
        IsDirectory = isDirectory;
    }
}
/// <summary>允许 Editor 扩展向 ProjectPanel 追加右键菜单项。</summary>
public static class ProjectContextMenuRegistry
{
    private sealed record Item(string Label, Func<ProjectAssetContext, bool>? Enabled, Action<ProjectAssetContext> Execute);
    private static readonly List<Item> Items = [];

    /// <summary>注册一个 ProjectPanel 资源右键菜单项。</summary>
    public static void Register(string label, Action<ProjectAssetContext> execute, Func<ProjectAssetContext, bool>? enabled = null)
    {
        if (string.IsNullOrWhiteSpace(label)) throw new ArgumentException("Menu label is empty.", nameof(label));
        ArgumentNullException.ThrowIfNull(execute);
        Items.Add(new Item(label, enabled, execute));
    }

    //绘制所有扩展菜单项。
    internal static void Draw(ProjectAssetContext context, Action<string> setStatus)
    {
        if (Items.Count == 0) return;
        EditorGUI.Separator();
        foreach (Item item in Items)
        {
            bool enabled = item.Enabled?.Invoke(context) ?? true;
            if (!EditorGUI.MenuItem(item.Label, enabled)) continue;
            try
            {
                item.Execute(context);
            }
            catch (Exception ex)
            {
                setStatus($"{item.Label} failed: {ex.Message}");
            }
        }
    }
}

/// <summary>通过目录树和资产列表浏览及管理项目资源文件。</summary>
internal sealed class ProjectPanel : EditorPanel
{
    private enum PendingOperation
    {
        None,
        Move,
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private sealed class OpenFileName
    {
        public int StructSize = Marshal.SizeOf<OpenFileName>();
        public IntPtr Owner;
        public IntPtr Instance;
        public string Filter = "All Files\0*.*\0\0";
        public string? CustomFilter;
        public int MaxCustomFilter;
        public int FilterIndex = 1;
        public StringBuilder File = new(32768);
        public int MaxFile = 32768;
        public StringBuilder? FileTitle;
        public int MaxFileTitle;
        public string? InitialDirectory;
        public string? Title;
        public int Flags;
        public short FileOffset;
        public short FileExtension;
        public string? DefaultExtension;
        public IntPtr CustomData;
        public IntPtr Hook;
        public string? TemplateName;
        public IntPtr Reserved;
        public int ReservedValue;
        public int FlagsEx;
    }

    private const int FileMustExist = 0x00001000;
    private const int PathMustExist = 0x00000800;
    private const int ExplorerDialog = 0x00080000;
    //网格瓦片宽度由缩放滑条控制，它就是一行放几块的依据；ScrollbarAllowance 是竖向滚动条余量
    private const float TileSizeMin = 64.0f;
    private const float TileSizeMax = 192.0f;
    private const float TileSizeDefault = 96.0f;
    private const float ScrollbarAllowance = 16.0f;

    private string contentRoot = string.Empty;
    private string currentDirectory = string.Empty;
    private string? selectedEntryPath;
    private string? expandedGridSource;
    private string? selectedPath
    {
        get => selectedEntryPath;
        set
        {
            selectedEntryPath = value;
            EditorAssetInspection.Select(value);
        }
    }
    private string? renamingEntry;
    private string renameBuffer = string.Empty;
    private bool renameFocusRequested;
    private bool panelFocused;
    private string search = string.Empty;
    private string status = string.Empty;
    private string operationValue = string.Empty;
    private PendingOperation pendingOperation;
    private FileSystemWatcher? watcher;
    private int refreshRequested;
    private float directoryWidth = 200;
    //Alt 折叠的待办：目录一合上，子树这一帧就不再被提交，只能等它们各自被画到时再逐个压回折叠
    private readonly HashSet<string> collapsedDirectories = [];
    private string startupWorld = string.Empty;
    private float tileSize = TileSizeDefault;
    private static string? pingKey;

    //浏览模式由缩放值推导：滑到最小端就是列表模式，其余按网格排。
    //不额外存一个模式字段，免得滑条与模式各说各话
    private bool gridView => tileSize > TileSizeMin;

    //请求在项目列表中定位引用资源
    internal static void Ping(string key)
    {
        pingKey = key.Split("//", 2, StringSplitOptions.None)[0];
        //请求要等本面板下一次绘制才被消费，而空闲编辑器不出帧，得自己催一帧
        EditorApplication.RequestRepaint();
    }

    public override EditorPanelInfo Info => new(
        "project",
        "Project",
        true,
        new vector2(640.0f, 260.0f),
        PanelDockPlacement.Bottom,
        0.28f,
        100);

    [DllImport("comdlg32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetOpenFileName([In, Out] OpenFileName openFileName);

    /// <summary>Panel 显示时刷新资源索引。</summary>
    public override void OnShown()
    {
        EditorAssetCatalog.Instance.Refresh();
        SetupWatcher();
    }

    /// <summary>Panel 隐藏时停止目录监听，并交还选择。</summary>
    public override void OnHidden()
    {
        watcher?.Dispose();
        watcher = null;
        EditorSelection.Clear(Info.Id);
        EditorAssetInspection.Select(null);
    }

    /// <summary>绘制 ProjectPanel。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        //F2 与 Delete 由选择系统按焦点派发到这里；进入 Play 后资源被锁，重命名必须收工
        panelFocused = NativeEditorGUI.IsWindowFocused();
        EditorSelection.Report(Info.Id, panelFocused, selectedPath != null);
        EditorDialog.Draw(Info.Id);
        if (renamingEntry != null && !EditorAssetsNative.CanModifyAssets()) EndRename();

        if (!EnsureProject())
        {
            EditorGUI.Label("No project loaded.");
            return;
        }

        if (Interlocked.Exchange(ref refreshRequested, 0) != 0)
        {
            EditorAssetCatalog.Instance.Refresh();
            if (!Directory.Exists(currentDirectory)) currentDirectory = EditorAssetCatalog.Instance.ContentRoot;
        }

        if (pingKey != null)
        {
            string path = Path.GetFullPath(Path.Combine(contentRoot, pingKey));
            pingKey = null;
            if (File.Exists(path) && ProjectAssetOperations.IsSameOrChild(path, contentRoot))
            {
                currentDirectory = Path.GetDirectoryName(path)!;
                selectedPath = path;
                search = string.Empty;
            }
        }
        EditorAssetCache.Update();
        DrawToolbar();
        DrawPendingOperation();
        if (!string.IsNullOrEmpty(status)) EditorGUI.Label(status);
        if (!string.IsNullOrEmpty(EditorWorldActions.Status)) EditorGUI.Label(EditorWorldActions.Status);
        startupWorld = EditorAssetsNative.GetWorldKey(true);
        DrawAssetBrowser();
        EditorWorldActions.DrawPendingSwitch();
    }

    //检测项目切换并重置目录状态。
    private bool EnsureProject()
    {
        string currentContentRoot = PathDefines.ContentRoot;
        if (string.IsNullOrWhiteSpace(currentContentRoot)) return false;

        if (string.Equals(contentRoot, currentContentRoot, StringComparison.OrdinalIgnoreCase)
            && Directory.Exists(currentDirectory)) return true;

        EditorWorldActions.Clear();
        contentRoot = currentContentRoot;
        EditorAssetCatalog.Instance.Refresh();
        currentDirectory = EditorAssetCatalog.Instance.ContentRoot;
        Directory.CreateDirectory(currentDirectory);
        SetupWatcher();
        selectedPath = null;
        EndRename();
        pendingOperation = PendingOperation.None;
        status = string.Empty;
        return true;
    }

    //监听外部资源文件变化并在下一帧刷新索引。
    private void SetupWatcher()
    {
        watcher?.Dispose();
        watcher = null;
        if (string.IsNullOrWhiteSpace(PathDefines.ContentRoot)) return;

        //监听整个内容根：脚本、资源、场景可以放在内容根内任何目录。
        string path = EditorAssetCatalog.Instance.ContentRoot;
        if (!Directory.Exists(path)) return;
        watcher = new FileSystemWatcher(path)
        {
            IncludeSubdirectories = true,
            NotifyFilter = NotifyFilters.FileName | NotifyFilters.DirectoryName | NotifyFilters.LastWrite,
            EnableRaisingEvents = true,
        };
        watcher.Changed += RequestRefresh;
        watcher.Created += RequestRefresh;
        watcher.Deleted += RequestRefresh;
        watcher.Renamed += RequestRefresh;
    }

    //标记资源索引需要刷新。
    private void RequestRefresh(object sender, FileSystemEventArgs args)
    {
        //生成目录在构建期间高频变化，不参与刷新。
        if (EditorAssetCatalog.Instance.IsGeneratedPath(args.FullPath)) return;
        Interlocked.Exchange(ref refreshRequested, 1);
        EditorApplication.RequestRepaint();
    }

    //绘制导航和常用操作栏。
    private void DrawToolbar()
    {
        bool atRoot = string.Equals(currentDirectory, EditorAssetCatalog.Instance.ContentRoot, StringComparison.OrdinalIgnoreCase);
        EditorGUI.BeginDisabled(atRoot);
        if (EditorGUI.Button("Up") && !atRoot)
        {
            currentDirectory = Path.GetDirectoryName(currentDirectory) ?? EditorAssetCatalog.Instance.ContentRoot;
            selectedPath = null;
        }
        EditorGUI.EndDisabled();

        //缩放滑条就是浏览模式控件：拉到最小端即列表模式。它同时改格子宽度、图标大小与一行放几块
        EditorGUI.SameLine();
        EditorGUI.SliderFloat("##project_tile_size", ref tileSize, TileSizeMin, TileSizeMax, 120.0f);

        EditorGUI.SameLine();
        EditorGUI.InputText("Search##project_search", ref search);
    }

    //绘制当前正在确认的文件操作。
    private void DrawPendingOperation()
    {
        if (pendingOperation == PendingOperation.None) return;

        EditorGUI.Separator();
        switch (pendingOperation)
        {
        case PendingOperation.Move:
            EditorGUI.Label("Destination folder (project-relative):");
            EditorGUI.InputText("Folder##project_operation", ref operationValue);
            break;
        }

        if (EditorGUI.Button("Confirm##project_operation")) ConfirmOperation();
        EditorGUI.SameLine();
        if (EditorGUI.Button("Cancel##project_operation")) pendingOperation = PendingOperation.None;
        EditorGUI.Separator();
    }

    //绘制目录树与当前目录的直接子项
    private void DrawAssetBrowser()
    {
        bool visible = NativeEditorGUI.BeginChild("##project_directories", ref directoryWidth, resizable: true);
        try { if (visible) DrawDirectory(EditorAssetCatalog.Instance.ContentRoot); }
        finally { NativeEditorGUI.EndChild(); }
        EditorGUI.SameLine();
        float remainingWidth = 0;
        visible = NativeEditorGUI.BeginChild("##project_contents", ref remainingWidth);
        try
        {
            if (visible)
            {
                //内容根的项目内相对键算出来是个点，换成目录名，和目录树里的叫法一致
                string location = EditorAssetCatalog.Instance.ToResourceKey(currentDirectory);
                EditorGUI.Label(location.Length == 0 || location == "." ? GetDirectoryName(currentDirectory) : location);
                DrawAssets(remainingWidth);
            }
        }
        finally { NativeEditorGUI.EndChild(); }
        DrawDirectoryDrop(currentDirectory);
    }

    //接收目录移动或将 Ens 子树保存为预制体
    private void DrawDirectoryDrop(string destinationDirectory)
    {
        string sourceKey = NativeEditorGUI.ReadDrag(out int kind);
        if (sourceKey.Length == 0) return;
        bool valid = EditorAssetsNative.CanModifyAssets();
        string source = kind == 2 ? Path.GetFullPath(Path.Combine(contentRoot, sourceKey)) : string.Empty;
        if (kind == 2)
            valid &= !string.Equals(Path.GetDirectoryName(source), destinationDirectory, StringComparison.OrdinalIgnoreCase)
                && !ProjectAssetOperations.IsSameOrChild(destinationDirectory, source);
        if (!NativeEditorGUI.AcceptDrag(valid)) return;
        if (kind == 2)
        {
            string destination = Path.Combine(destinationDirectory, Path.GetFileName(source));
            if (ProjectAssetOperations.Move(source, destination, out status))
            {
                if (ProjectAssetOperations.IsSameOrChild(currentDirectory, source))
                    currentDirectory = Path.Combine(destination, Path.GetRelativePath(source, currentDirectory));
                selectedPath = destination;
            }
            return;
        }
        Ens ens = Ens.Find(sourceKey);
        if (!ens.IsValid) return;
        string name = string.Concat(ens.Name.Select(character => Path.GetInvalidFileNameChars().Contains(character) ? '_' : character));
        if (string.IsNullOrWhiteSpace(name)) name = "New Prefab";
        string path = Path.Combine(destinationDirectory, name + ".prefab");
        for (int index = 1; File.Exists(path) || Directory.Exists(path); ++index)
            path = Path.Combine(destinationDirectory, name + " " + index + ".prefab");
        string key = EditorAssetCatalog.Instance.ToResourceKey(path);
        if (!EditorAssetsNative.SavePrefab(sourceKey, key))
        {
            status = "Failed to save prefab: " + key;
            return;
        }
        selectedPath = path;
        status = "Saved prefab: " + key;
        EditorAssetCatalog.Instance.Refresh();
    }

    //递归绘制展开目录并排除生成目录与目录链接；forceExpand 来自上级的 Alt 展开，要求整棵子树一并展开
    private void DrawDirectory(string path, bool forceExpand = false)
    {
        if (EditorAssetCatalog.Instance.IsGeneratedPath(path)) return;
        string name = GetDirectoryName(path);
        bool renaming = string.Equals(renamingEntry, path, StringComparison.OrdinalIgnoreCase) && RenamesInTree(path);
        //Alt 折叠的待办在这里被消费一次：本次先把目录压回折叠，之后交回 ImGui 自己记状态
        bool forceCollapse = !forceExpand && collapsedDirectories.Remove(path);
        //没有下级目录的叶子不该显示展开箭头
        //重命名时把名称让给输入框；隐藏标签后节点宽度仍是「箭头+标签」，输入框正好从原名称的位置开始
        int state = NativeEditorGUI.TreeNode((renaming ? string.Empty : name) + "##directory_" + path,
            string.Equals(currentDirectory, path, StringComparison.OrdinalIgnoreCase),
            leaf: !HasSubDirectories(path),
            forceOpen: forceExpand,
            forceCollapse: forceCollapse);
        if (renaming)
        {
            EditorGUI.SameLine();
            DrawRenameInput(path);
        }
        //按住 Alt 点箭头才递归：刚展开的这一帧子树会被提交，展开能顺着递归直接传下去；
        //刚折叠的这一帧子树一个都不会提交，只能记进待折叠集合
        bool altToggle = (state & 16) != 0 && (state & 32) != 0;
        bool expandSubtree = altToggle && (state & 1) != 0;
        if (altToggle && !expandSubtree) MarkCollapsedSubtree(path);
        if ((state & 2) != 0)
        {
            currentDirectory = path;
            //当前选中项就是点中的这个目录：F2 与右键 Rename 都作用在它身上
            selectedPath = path;
        }
        DrawDirectoryDrop(path);
        NativeEditorGUI.DragSource(2, EditorAssetCatalog.Instance.ToResourceKey(path));
        if (EditorGUI.BeginPopupContextItem("##directory_menu_" + path))
        {
            try { DrawItemContextMenu(path, true); }
            finally { EditorGUI.EndPopup(); }
        }
        if ((state & 1) == 0) return;
        try
        {
            foreach (string directory in Directory.EnumerateDirectories(path).Order(StringComparer.OrdinalIgnoreCase))
            {
                if ((File.GetAttributes(directory) & FileAttributes.ReparsePoint) == 0) DrawDirectory(directory, expandSubtree);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            EditorGUI.Label("Directory read failed: " + ex.Message);
        }
        finally { NativeEditorGUI.TreePop(); }
    }

    //把整棵子目录记进待折叠集合，等被折叠挡住的那些目录各自被画到时再压回折叠。
    //范围与绘制一致：跳过分隔点与生成目录，否则会留下永远画不到的死键
    private void MarkCollapsedSubtree(string path)
    {
        try
        {
            foreach (string directory in Directory.EnumerateDirectories(path))
            {
                if ((File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0) continue;
                if (EditorAssetCatalog.Instance.IsGeneratedPath(directory)) continue;
                collapsedDirectories.Add(directory);
                MarkCollapsedSubtree(directory);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
        }
    }

    //目录的显示名：内容根统一叫 Content（与目录树、项目模板一致），其余取文件夹名
    private static string GetDirectoryName(string path)
        => string.Equals(path, EditorAssetCatalog.Instance.ContentRoot, StringComparison.OrdinalIgnoreCase)
            ? "Content"
            : Path.GetFileName(path);

    //判断目录下还有没有可展开的子目录，生成目录与目录链接不算
    private static bool HasSubDirectories(string path)
    {
        try
        {
            foreach (string directory in Directory.EnumerateDirectories(path))
            {
                if ((File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0) continue;
                if (EditorAssetCatalog.Instance.IsGeneratedPath(directory)) continue;
                return true;
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
        }
        return false;
    }

    //绘制 Create... 二级菜单：新建的资源一律落在当前目录，与 Create World 的落点保持一致
    private void DrawCreateMenu(bool canModify)
    {
        if (!EditorGUI.BeginMenu("Create...", canModify)) return;
        try
        {
            if (EditorGUI.MenuItem("Create World", canModify)) CreateWorld();
            if (EditorGUI.MenuItem("Create Material", canModify)) CreateMaterial();
            if (EditorGUI.MenuItem("Create Script", canModify)) CreateScript();
            if (EditorGUI.MenuItem("Create Shader", canModify)) CreateShader();
        }
        finally
        {
            EditorGUI.EndMenu();
        }
    }

    //按模板新建资源：取不冲突的默认名，建完选中并立刻进入就地重命名
    private void CreateAssetEntry(string defaultName, string extension, string content)
    {
        try
        {
            string path = Path.Combine(currentDirectory, defaultName + extension);
            for (int index = 1; File.Exists(path) || Directory.Exists(path); ++index)
                path = Path.Combine(currentDirectory, defaultName + " " + index + extension);
            if (!ProjectAssetOperations.CreateAsset(path, content, out status)) return;
            selectedPath = path;
            BeginRename(path);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            status = "Create asset failed: " + ex.Message;
        }
    }

    //新建材质资产：内容由 .orbmat 序列化器读取
    private void CreateMaterial() => CreateAssetEntry("New Material", ".orbmat", EditorAssetTemplates.Material());

    //新建脚本资产：模板整段注释，命名空间取项目名
    private void CreateScript() => CreateAssetEntry("New Script", ".cs", EditorAssetTemplates.Script(
        EditorAssetTemplates.NamespaceFromProject(EditorApplication.GetProjectText(EditorProjectField.Name))));

    //新建着色器资产
    private void CreateShader() => CreateAssetEntry("New Shader", ".orbshader", EditorAssetTemplates.Shader());

    //创建名称不冲突的 World 并请求打开
    private void CreateWorld()
    {
        try
        {
            string path = Path.Combine(currentDirectory, "New World.world");
            for (int index = 1; File.Exists(path) || Directory.Exists(path); ++index)
                path = Path.Combine(currentDirectory, "New World " + index + ".world");
            string key = EditorAssetCatalog.Instance.ToResourceKey(path);
            if (!EditorAssetsNative.CreateWorld(key))
            {
                status = EditorAssetsNative.GetProjectError();
                return;
            }
            EditorAssetCatalog.Instance.Refresh();
            selectedPath = path;
            EditorWorldActions.RequestOpen(key);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            status = "Create World failed: " + ex.Message;
        }
    }

    //绘制当前目录资源：列表与网格共用一份枚举结果与背景菜单
    private void DrawAssets(float width)
    {
        List<string> entries;
        try
        {
            entries = Directory.EnumerateFileSystemEntries(currentDirectory)
                .Where(path => !EditorAssetCatalog.Instance.IsGeneratedPath(path))
                .Where(MatchesSearch)
                .OrderBy(path => File.Exists(path))
                .ThenBy(path => Path.GetFileName(path), StringComparer.OrdinalIgnoreCase)
                .ToList();
        }
        catch (Exception ex)
        {
            EditorGUI.Label("Directory read failed: " + ex.Message);
            return;
        }

        if (gridView)
        {
            DrawAssetGrid(entries, width);
            if (expandedGridSource != null && entries.Contains(expandedGridSource) && File.Exists(expandedGridSource)
                && EditorAssetCatalog.CanExpandSource(expandedGridSource))
            {
                EditorGUI.Separator();
                EditorGUI.Label("Resources in " + Path.GetFileName(expandedGridSource));
                DrawSubAssets(expandedGridSource, false);
            }
        }
        else DrawAssetTable(entries);

        if (EditorGUI.BeginPopupContextWindow("##project_background_menu"))
        {
            try
            {
                bool canModify = EditorAssetsNative.CanModifyAssets();
                if (EditorGUI.MenuItem("Create Folder", canModify)) CreateFolder();
                DrawCreateMenu(canModify);
                if (EditorGUI.MenuItem("Import...", canModify)) ImportFile();
                if (EditorGUI.MenuItem("Paste", canModify && EditorAssetClipboard.HasEntry)) PasteEntry(currentDirectory);
                if (EditorGUI.MenuItem("Refresh"))
                {
                    EditorAssetCatalog.Instance.Refresh();
                    status = "Assets refreshed.";
                }
                //这里的 Reimport 以当前目录为范围，覆盖其下全部已加载资源
                if (EditorGUI.MenuItem("Reimport")) ReimportEntry(currentDirectory);
                if (EditorGUI.MenuItem("Reimport All")) ReimportAll();
                //背景菜单以当前目录为上下文，扩展项在空白处也能用
                ProjectContextMenuRegistry.Draw(new ProjectAssetContext(currentDirectory,
                    EditorAssetCatalog.Instance.ToResourceKey(currentDirectory), true), value => status = value);
            }
            finally
            {
                EditorGUI.EndPopup();
            }
        }
    }

    /// <summary>F2 触发：就地重命名当前选中的资源。</summary>
    public override void OnRenameRequested()
    {
        if (!EditorAssetsNative.CanModifyAssets()) return;
        BeginRename(selectedPath ?? string.Empty);
    }

    /// <summary>Delete 触发：删除当前选中的资源，先弹确认窗。</summary>
    public override void OnDeleteRequested() => BeginDelete(selectedPath);

    /// <summary>Reimport 触发：重新导入当前选中的资源。</summary>
    public override void OnReimportRequested() => ReimportEntry(selectedPath);

    /// <summary>Ctrl+C 触发：把当前选中的资源路径放进剪贴板。</summary>
    public override void OnCopyRequested()
    {
        if (selectedPath == null) return;
        CopyEntry(selectedPath);
    }

    /// <summary>Ctrl+V 触发：把剪贴板里的资源贴到当前目录。</summary>
    public override void OnPasteRequested()
    {
        if (!EditorAssetClipboard.HasEntry) return;
        PasteEntry(currentDirectory);
    }

    //把资源路径放进剪贴板；存绝对路径，粘贴时按内容根重新判定它还合不合法
    private void CopyEntry(string entry)
    {
        EditorAssetClipboard.Capture(Path.GetFullPath(entry));
        EditorStatusBar.Print("Copied: " + EditorAssetCatalog.Instance.ToResourceKey(entry));
    }

    //把剪贴板里的资源复制到目标目录，贴完选中副本
    private void PasteEntry(string destinationDirectory)
    {
        //没有项目、正在 Play 或者剪贴板里的路径已经不合法时，Paste 把原因写在 outReason 里
        if (!ProjectAssetOperations.Paste(EditorAssetClipboard.Entry, destinationDirectory,
            out string pasted, out string reason))
        {
            EditorStatusBar.Print(reason);
            return;
        }
        selectedPath = pasted;
    }

    //粘贴落点：文件夹贴进它自己，文件贴进它所在的目录
    private static string PasteDestination(string entry, bool directory)
        => directory ? entry : Path.GetDirectoryName(entry)!;

    //重新导入指定路径的已加载资源；目录连带子路径，两种都报出处理的源文件数
    private void ReimportEntry(string? entry)
    {
        if (string.IsNullOrEmpty(entry)) return;
        EditorAssetInspection.Invalidate(force: true);
        int count = EditorAssetsNative.ReimportAsset(
            EditorAssetCatalog.Instance.ToResourceKey(entry), Directory.Exists(entry));
        status = count == 0 ? "Nothing to reimport: no loaded asset under this path."
            : $"Reimported {count} source file(s).";
    }

    //重新导入全部已加载资源
    private void ReimportAll()
    {
        EditorAssetInspection.Invalidate(force: true);
        int count = EditorAssetsNative.ReimportAllAssets();
        status = count == 0 ? "Nothing to reimport: no loaded assets." : $"Reimported {count} source file(s).";
    }

    //请求删除：资源删除进回收站且没有撤销，所以先确认
    private void BeginDelete(string? entry)
    {
        if (string.IsNullOrEmpty(entry) || !EditorAssetsNative.CanModifyAssets()) return;
        EditorDialog.Confirm(Info.Id, "Delete",
            $"Move '{Path.GetFileName(entry)}' to Recycle Bin? Soft references will be cleared.",
            "Delete", () => DeleteEntry(entry));
    }

    //确认后真正删除
    private void DeleteEntry(string entry)
    {
        if (!ProjectAssetOperations.Delete(entry, out status)) return;
        if (string.Equals(selectedPath, entry, StringComparison.OrdinalIgnoreCase)) selectedPath = null;
    }

    //进入就地重命名，输入框落在原来名称的位置
    private void BeginRename(string entry)
    {
        if (string.IsNullOrEmpty(entry)) return;
        //内容根改名会让项目里的 ContentRoot 指向不存在的目录，直接挡掉
        if (string.Equals(entry, EditorAssetCatalog.Instance.ContentRoot, StringComparison.OrdinalIgnoreCase)) return;
        selectedPath = entry;
        renamingEntry = entry;
        renameBuffer = Path.GetFileName(entry);
        renameFocusRequested = true;
        pendingOperation = PendingOperation.None;
        EditorApplication.RequestRepaint();
    }

    //重命名输入框只画一处：当前目录的儿子在列表/网格里，更深的目录只有目录树看得到。
    //同一个目录两处都在屏幕上时画两次会共用一个输入框状态
    private bool RenamesInTree(string entry)
    {
        return !string.Equals(Path.GetDirectoryName(entry), currentDirectory, StringComparison.OrdinalIgnoreCase);
    }

    //退出就地重命名
    private void EndRename()
    {
        renamingEntry = null;
        renameBuffer = string.Empty;
        renameFocusRequested = false;
    }

    //绘制就地重命名输入框：回车或失焦应用，Esc 放弃。width 为 0 时占满本行剩余宽度。
    private void DrawRenameInput(string entry, float width = 0.0f)
        => ApplyRenameResult(entry, EditorGUI.RenameInput("##project_rename", ref renameBuffer, ref renameFocusRequested, width));

    //处理输入框结果：列表、目录树、网格瓦片三种画法共用这一段。
    //0 继续编辑、1 回车、2 失焦、3 Esc
    private void ApplyRenameResult(string entry, int result)
    {
        if (result == 0) return;
        if (result == 3) { EndRename(); return; }   //Esc，文本已由框架还原

        //回车留在原地接着改，失焦时人的注意力已经走了，只能放弃
        bool keepEditing = result == 1;
        string name = renameBuffer.Trim();
        if (!IsValidName(name))
        {
            status = "Name contains invalid path characters.";
        }
        else
        {
            string target = Path.Combine(Path.GetDirectoryName(entry)!, name);
            if (ProjectAssetOperations.Move(entry, target, out status))
            {
                //改的是目录时，当前目录可能就在它下面，跟着一起搬
                if (Directory.Exists(target) && ProjectAssetOperations.IsSameOrChild(currentDirectory, entry))
                    currentDirectory = Path.Combine(target, Path.GetRelativePath(entry, currentDirectory));
                if (string.Equals(selectedPath, entry, StringComparison.OrdinalIgnoreCase)) selectedPath = target;
                EndRename();
                return;
            }
        }

        if (keepEditing) renameFocusRequested = true;
        else EndRename();
    }

    //直接建目录再立刻进入重命名：和主流编辑器一样，不用先填名字再确认
    private void CreateFolder()
    {
        string path = Path.Combine(currentDirectory, "New Folder");
        for (int index = 1; Directory.Exists(path) || File.Exists(path); ++index)
            path = Path.Combine(currentDirectory, "New Folder " + index);
        if (!ProjectAssetOperations.CreateFolder(path, out status)) return;

        selectedPath = path;
        BeginRename(path);
    }

    //绘制资源列表视图
    private void DrawAssetTable(List<string> entries)
    {
        if (!EditorGUI.BeginTable("##project_assets", 2)) return;
        try
        {
            EditorGUI.TableSetupColumn("Name");
            EditorGUI.TableSetupColumn("Type", 150.0f, fixedWidth: true);
            EditorGUI.TableHeadersRow();
            foreach (string entry in entries) DrawAssetRow(entry);
        }
        finally
        {
            EditorGUI.EndTable();
        }
    }

    //绘制资源网格视图：按可用宽度决定列数，剩余宽度均分给每块瓦片
    private void DrawAssetGrid(List<string> entries, float width)
    {
        //子窗口宽度含边框与内边距，再留出竖向滚动条余量，避免铺满一行后挤出横向滚动条
        float spacing = EditorTheme.Current.SpacingX;
        float available = width - (EditorTheme.Current.PaddingX + 1.0f) * 2.0f - ScrollbarAllowance;
        int columns = Math.Max(1, (int)((available + spacing) / (tileSize + spacing)));
        float tileWidth = Math.Max(tileSize, (available - (columns - 1) * spacing) / columns);
        for (int index = 0; index < entries.Count; index++)
        {
            if (index % columns != 0) EditorGUI.SameLine();
            DrawAssetTile(entries[index], tileWidth);
        }
    }

    //绘制一个资源瓦片，交互与列表行保持一致
    private void DrawAssetTile(string entry, float width)
    {
        bool directory = Directory.Exists(entry);
        bool selected = string.Equals(selectedPath, entry, StringComparison.OrdinalIgnoreCase);
        //重命名的瓦片由原生一次画出来：图标照旧，名称那一行就地变成输入框。
        //网格一行一个 SameLine 单元格、一个单元格只能放一个控件，分两次调用会把输入框挤到下一行
        if (string.Equals(renamingEntry, entry, StringComparison.OrdinalIgnoreCase) && !RenamesInTree(entry))
        {
            ApplyRenameResult(entry, EditorGUI.AssetRenameTile(EditorIconCatalog.ForResource(entry, directory), entry,
                ref renameBuffer, ref renameFocusRequested, width, selected));
            return;
        }
        int state = NativeEditorGUI.AssetTile(EditorIconCatalog.ForResource(entry, directory), GetDisplayName(entry), entry,
            width, selected, expandable: !directory && EditorAssetCatalog.CanExpandSource(entry), expanded: expandedGridSource == entry);
        if ((state & 1) != 0) selectedPath = entry;
        if ((state & 2) != 0)
        {
            selectedPath = entry;
            expandedGridSource = expandedGridSource == entry ? null : entry;
        }
        if ((state & 4) == 0 && EditorGUI.IsItemDoubleClicked()) OpenEntry(entry);
        if (directory) DrawDirectoryDrop(entry);
        NativeEditorGUI.DragSource(2, EditorAssetCatalog.Instance.ToResourceKey(entry));
        if (EditorGUI.BeginPopupContextItem("##project_tile_menu_" + entry))
        {
            selectedPath = entry;
            try { DrawItemContextMenu(entry, directory); }
            finally { EditorGUI.EndPopup(); }
        }
    }

    //取资源显示名，启动 World 追加标记
    private string GetDisplayName(string entry)
    {
        string name = Path.GetFileName(entry);
        return string.Equals(EditorAssetCatalog.Instance.ToResourceKey(entry), startupWorld, StringComparison.OrdinalIgnoreCase)
            ? name + " [Startup]"
            : name;
    }

    //绘制一行资源。
    private void DrawAssetRow(string entry)
    {
        bool directory = Directory.Exists(entry);
        bool expanded = false;
        string name = GetDisplayName(entry);
        EditorGUI.TableNextRow();
        EditorGUI.TableSetColumnIndex(0);
        if (string.Equals(renamingEntry, entry, StringComparison.OrdinalIgnoreCase) && !RenamesInTree(entry))
        {
            //重命名时名称位置让给输入框，它占满这一列，与原来的标签同宽同位
            DrawRenameInput(entry);
        }
        else
        {
            bool clicked;
            if (!directory)
            {
                bool leaf = !EditorAssetCatalog.CanExpandSource(entry);
                int state = NativeEditorGUI.TreeNode(name + "##" + entry,
                    string.Equals(selectedPath, entry, StringComparison.OrdinalIgnoreCase), leaf: leaf,
                    icon: EditorSourceIconCatalog.ForFile(entry));
                expanded = !leaf && (state & 1) != 0;
                if (leaf && (state & 1) != 0) NativeEditorGUI.TreePop();
                clicked = (state & 2) != 0;
            }
            else clicked = EditorGUI.TableSelectable((directory ? "[Folder] " : string.Empty) + name + "##" + entry,
                string.Equals(selectedPath, entry, StringComparison.OrdinalIgnoreCase));
            bool doubleClicked = EditorGUI.IsItemDoubleClicked();
            if (clicked) selectedPath = entry;
            if (doubleClicked) OpenEntry(entry);
            if (directory) DrawDirectoryDrop(entry);
            NativeEditorGUI.DragSource(2, EditorAssetCatalog.Instance.ToResourceKey(entry));
        }

        if (EditorGUI.BeginPopupContextItem("##project_item_menu_" + entry))
        {
            selectedPath = entry;
            try
            {
                DrawItemContextMenu(entry, directory);
            }
            finally
            {
                EditorGUI.EndPopup();
            }
        }

        EditorGUI.TableSetColumnIndex(1);
        EditorGUI.Label(EditorAssetCatalog.Instance.GetSourceType(entry));
        if (expanded)
        {
            try { DrawSubAssets(entry, true); }
            finally { NativeEditorGUI.TreePop(); }
        }
    }

    /// <summary>列表和网格共用真实子资源条目，拖拽携带完整 Key。</summary>
    private void DrawSubAssets(string path, bool table)
    {
        EditorAssetInspection.Result result = EditorAssetInspection.Get(path);
        foreach (EditorAssetInspection.Asset asset in result.Objects)
        {
            if (table) { EditorGUI.TableNextRow(); EditorGUI.TableSetColumnIndex(0); }
            string label = asset.Key.Contains("//", StringComparison.Ordinal) ? asset.Key.Split("//", 2)[1] : asset.TypeName;
            int state = NativeEditorGUI.TreeNode(label + "##subasset_" + asset.Key,
                EditorAssetInspection.SourcePath == path && EditorAssetInspection.ObjectKey == asset.Key,
                leaf: true, icon: EditorIconCatalog.ForReference(asset.TypeName));
            if ((state & 2) != 0)
            {
                selectedEntryPath = path;
                EditorAssetInspection.Select(path, asset.Key);
            }
            if (!result.IsStale) NativeEditorGUI.DragSource(2, asset.Key);
            if ((state & 1) != 0) NativeEditorGUI.TreePop();
            if (table) { EditorGUI.TableSetColumnIndex(1); EditorGUI.Label(asset.TypeName); }
        }
        if (result.Objects.Count == 0 || result.Messages.Count != 0)
        {
            if (table) { EditorGUI.TableNextRow(); EditorGUI.TableSetColumnIndex(0); }
            EditorGUI.Label(result.Messages.Count != 0 ? string.Join("; ", result.Messages) : "No resource objects.");
        }
    }
    //绘制一个资源条目的右键菜单。
    private void DrawItemContextMenu(string entry, bool directory)
    {
        bool canModify = EditorAssetsNative.CanModifyAssets();
        if (EditorGUI.MenuItem("Open")) OpenEntry(entry);
        if (!directory && string.Equals(Path.GetExtension(entry), ".world", StringComparison.OrdinalIgnoreCase)
            && EditorGUI.MenuItem("Set as Startup World", canModify))
        {
            status = EditorAssetsNative.SetStartupWorld(EditorAssetCatalog.Instance.ToResourceKey(entry))
                ? "Startup World updated." : EditorAssetsNative.GetProjectError();
        }
        EditorGUI.Separator();
        //内容根不能改名，菜单项跟着置灰
        bool renamable = canModify
            && !string.Equals(entry, EditorAssetCatalog.Instance.ContentRoot, StringComparison.OrdinalIgnoreCase);
        if (EditorGUI.MenuItem("Rename", renamable)) BeginRename(entry);
        if (EditorGUI.MenuItem("Move...", canModify)) BeginOperation(PendingOperation.Move, entry);
        if (EditorGUI.MenuItem("Duplicate", canModify))
        {
            if (ProjectAssetOperations.Duplicate(entry, out string duplicate, out status)) selectedPath = duplicate;
        }
        if (EditorGUI.MenuItem("Copy")) CopyEntry(entry);
        if (EditorGUI.MenuItem("Paste", canModify && EditorAssetClipboard.HasEntry)) PasteEntry(PasteDestination(entry, directory));
        if (EditorGUI.MenuItem("Delete", canModify)) BeginDelete(entry);
        EditorGUI.Separator();
        if (EditorGUI.MenuItem("Reveal in Explorer")) EditorAssetCatalog.Reveal(entry);
        if (EditorGUI.MenuItem("Copy Resource Key")) EditorGUI.SetClipboardText(EditorAssetCatalog.Instance.ToResourceKey(entry));

        //创建类操作也放进条目菜单：列表铺满时空白处点不到背景菜单
        EditorGUI.Separator();
        if (EditorGUI.MenuItem("Create Folder", canModify)) CreateFolder();
        DrawCreateMenu(canModify);
        if (EditorGUI.MenuItem("Import...", canModify)) ImportFile();
        if (EditorGUI.MenuItem("Refresh"))
        {
            EditorAssetCatalog.Instance.Refresh();
            status = "Assets refreshed.";
        }

        //Reimport 重读磁盘上的源文件，会改已加载对象的内容，空白处的背景菜单也放一份
        EditorGUI.Separator();
        if (EditorGUI.MenuItem("Reimport")) ReimportEntry(entry);
        if (EditorGUI.MenuItem("Reimport All")) ReimportAll();

        ProjectAssetContext context = new(entry, EditorAssetCatalog.Instance.ToResourceKey(entry), directory);
        ProjectContextMenuRegistry.Draw(context, value => status = value);
    }

    //开始一个需要确认的文件操作。
    private void BeginOperation(PendingOperation operation, string? path)
    {
        if (path != null) selectedPath = path;
        pendingOperation = operation;
        operationValue = operation switch
        {
            PendingOperation.Move => EditorAssetCatalog.Instance.ToResourceKey(currentDirectory),
            _ => string.Empty,
        };
    }

    //确认当前文件操作。
    private void ConfirmOperation()
    {
        string? path = selectedPath;
        bool succeeded = false;
        switch (pendingOperation)
        {
        case PendingOperation.Move when path != null:
            string destinationDirectory = Path.GetFullPath(Path.Combine(contentRoot, operationValue.Replace('/', Path.DirectorySeparatorChar)));
            string moved = Path.Combine(destinationDirectory, Path.GetFileName(path));
            succeeded = ProjectAssetOperations.Move(path, moved, out status);
            if (succeeded) selectedPath = moved;
            break;
        }

        if (succeeded) pendingOperation = PendingOperation.None;
    }

    //打开文件或进入文件夹。
    private void OpenEntry(string entry)
    {
        if (Directory.Exists(entry))
        {
            currentDirectory = entry;
            selectedPath = null;
            return;
        }

        //场景由编辑器自己打开：交给系统默认程序只会用文本编辑器打开 XML。
        if (string.Equals(Path.GetExtension(entry), ".world", StringComparison.OrdinalIgnoreCase))
        {
            string key = EditorAssetCatalog.Instance.ToResourceKey(entry);
            EditorWorldActions.RequestOpen(key);
            return;
        }

        try
        {
            EditorAssetCatalog.OpenFile(entry);
        }
        catch (Exception ex)
        {
            status = "Open failed: " + ex.Message;
        }
    }

    //弹出 Windows 文件选择器并导入资源。
    private void ImportFile()
    {
        OpenFileName dialog = new()
        {
            InitialDirectory = currentDirectory,
            Title = "Import Asset",
            Flags = ExplorerDialog | FileMustExist | PathMustExist,
        };
        if (!GetOpenFileName(dialog)) return;

        if (ProjectAssetOperations.Import(dialog.File.ToString(), currentDirectory, out string imported, out status))
        {
            selectedPath = imported;
        }
    }

    //判断列表项是否匹配当前搜索。
    private bool MatchesSearch(string path)
    {
        return string.IsNullOrWhiteSpace(search)
            || Path.GetFileName(path).Contains(search.Trim(), StringComparison.OrdinalIgnoreCase);
    }

    //判断输入是否是单个合法文件名。
    private static bool IsValidName(string name)
    {
        return !string.IsNullOrWhiteSpace(name)
            && name is not "." and not ".."
            && name.IndexOfAny(Path.GetInvalidFileNameChars()) < 0;
    }
}
