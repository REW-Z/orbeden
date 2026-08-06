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

/// <summary>以两列表格浏览和管理项目资源文件。</summary>
internal sealed class ProjectPanel : EditorPanel
{
    private enum PendingOperation
    {
        None,
        Rename,
        Move,
        Delete,
        CreateFolder,
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

    private string projectRoot = string.Empty;
    private string resourceRoot = string.Empty;
    private string currentDirectory = string.Empty;
    private string? selectedPath;
    private string search = string.Empty;
    private string status = string.Empty;
    private string operationValue = string.Empty;
    private PendingOperation pendingOperation;
    private FileSystemWatcher? watcher;
    private int refreshRequested;

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

    /// <summary>Panel 隐藏时停止目录监听。</summary>
    public override void OnHidden()
    {
        watcher?.Dispose();
        watcher = null;
    }

    /// <summary>绘制 ProjectPanel。</summary>
    public override void Draw(EditorPanelContext context)
    {
        if (!EnsureProject())
        {
            EditorGUI.Label("No project loaded.");
            return;
        }

        if (Interlocked.Exchange(ref refreshRequested, 0) != 0)
        {
            EditorAssetCatalog.Instance.Refresh();
            if (!Directory.Exists(currentDirectory)) currentDirectory = EditorAssetCatalog.Instance.ResourceRootPath;
        }

        DrawToolbar();
        DrawPendingOperation();
        if (!string.IsNullOrEmpty(status)) EditorGUI.Label(status);
        EditorGUI.Label(EditorAssetCatalog.Instance.ToResourceKey(currentDirectory));
        DrawAssetTable();
    }

    //检测项目切换并重置目录状态。
    private bool EnsureProject()
    {
        string currentProjectRoot = PathDefines.ContentRoot;
        if (string.IsNullOrWhiteSpace(currentProjectRoot)) return false;

        string currentResourceRoot = EditorAssetsNative.GetResourceRoot();
        if (string.Equals(projectRoot, currentProjectRoot, StringComparison.OrdinalIgnoreCase)
            && string.Equals(resourceRoot, currentResourceRoot, StringComparison.OrdinalIgnoreCase)
            && Directory.Exists(currentDirectory)) return true;

        projectRoot = currentProjectRoot;
        resourceRoot = currentResourceRoot;
        EditorAssetCatalog.Instance.Refresh();
        currentDirectory = EditorAssetCatalog.Instance.ResourceRootPath;
        Directory.CreateDirectory(currentDirectory);
        SetupWatcher();
        selectedPath = null;
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

        string path = EditorAssetCatalog.Instance.ResourceRootPath;
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
        Interlocked.Exchange(ref refreshRequested, 1);
        EditorApplication.RequestRepaint();
    }

    //绘制导航和常用操作栏。
    private void DrawToolbar()
    {
        bool atRoot = string.Equals(currentDirectory, EditorAssetCatalog.Instance.ResourceRootPath, StringComparison.OrdinalIgnoreCase);
        EditorGUI.BeginDisabled(atRoot);
        if (EditorGUI.Button("Up") && !atRoot)
        {
            currentDirectory = Path.GetDirectoryName(currentDirectory) ?? EditorAssetCatalog.Instance.ResourceRootPath;
            selectedPath = null;
        }
        EditorGUI.EndDisabled();

        EditorGUI.SameLine();
        if (EditorGUI.Button("Refresh"))
        {
            EditorAssetCatalog.Instance.Refresh();
            status = "Assets refreshed.";
        }

        bool canModify = EditorAssetsNative.CanModifyAssets();
        EditorGUI.SameLine();
        EditorGUI.BeginDisabled(!canModify);
        if (EditorGUI.Button("Import...")) ImportFile();
        EditorGUI.SameLine();
        if (EditorGUI.Button("Create Folder")) BeginOperation(PendingOperation.CreateFolder, null);
        EditorGUI.EndDisabled();

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
        case PendingOperation.Rename:
            EditorGUI.Label("Rename selected asset:");
            EditorGUI.InputText("Name##project_operation", ref operationValue);
            break;
        case PendingOperation.Move:
            EditorGUI.Label("Destination folder (project-relative):");
            EditorGUI.InputText("Folder##project_operation", ref operationValue);
            break;
        case PendingOperation.Delete:
            EditorGUI.Label($"Move '{Path.GetFileName(selectedPath)}' to Recycle Bin? Soft references will be cleared.");
            break;
        case PendingOperation.CreateFolder:
            EditorGUI.Label("Create folder in the current directory:");
            EditorGUI.InputText("Name##project_operation", ref operationValue);
            break;
        }

        if (EditorGUI.Button("Confirm##project_operation")) ConfirmOperation();
        EditorGUI.SameLine();
        if (EditorGUI.Button("Cancel##project_operation")) pendingOperation = PendingOperation.None;
        EditorGUI.Separator();
    }

    //绘制两列资源列表。
    private void DrawAssetTable()
    {
        List<string> entries;
        try
        {
            entries = Directory.EnumerateFileSystemEntries(currentDirectory)
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

        if (EditorGUI.BeginPopupContextWindow("##project_background_menu"))
        {
            try
            {
                bool canModify = EditorAssetsNative.CanModifyAssets();
                if (EditorGUI.MenuItem("Create Folder", canModify)) BeginOperation(PendingOperation.CreateFolder, null);
                if (EditorGUI.MenuItem("Import...", canModify)) ImportFile();
                if (EditorGUI.MenuItem("Refresh"))
                {
                    EditorAssetCatalog.Instance.Refresh();
                    status = "Assets refreshed.";
                }
            }
            finally
            {
                EditorGUI.EndPopup();
            }
        }
    }

    //绘制一行资源。
    private void DrawAssetRow(string entry)
    {
        bool directory = Directory.Exists(entry);
        string name = Path.GetFileName(entry);
        EditorGUI.TableNextRow();
        EditorGUI.TableSetColumnIndex(0);
        bool clicked = EditorGUI.TableSelectable((directory ? "[Folder] " : string.Empty) + name + "##" + entry,
            string.Equals(selectedPath, entry, StringComparison.OrdinalIgnoreCase));
        bool doubleClicked = EditorGUI.IsItemDoubleClicked();
        if (clicked) selectedPath = entry;
        if (doubleClicked) OpenEntry(entry);

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
    }

    //绘制一个资源条目的右键菜单。
    private void DrawItemContextMenu(string entry, bool directory)
    {
        bool canModify = EditorAssetsNative.CanModifyAssets();
        if (EditorGUI.MenuItem("Open")) OpenEntry(entry);
        EditorGUI.Separator();
        if (EditorGUI.MenuItem("Rename", canModify)) BeginOperation(PendingOperation.Rename, entry);
        if (EditorGUI.MenuItem("Move...", canModify)) BeginOperation(PendingOperation.Move, entry);
        if (EditorGUI.MenuItem("Duplicate", canModify))
        {
            if (ProjectAssetOperations.Duplicate(entry, out string duplicate, out status)) selectedPath = duplicate;
        }
        if (EditorGUI.MenuItem("Delete", canModify)) BeginOperation(PendingOperation.Delete, entry);
        EditorGUI.Separator();
        if (EditorGUI.MenuItem("Reveal in Explorer")) EditorAssetCatalog.Reveal(entry);
        if (EditorGUI.MenuItem("Copy Resource Key")) EditorGUI.SetClipboardText(EditorAssetCatalog.Instance.ToResourceKey(entry));

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
            PendingOperation.Rename => selectedPath == null ? string.Empty : Path.GetFileName(selectedPath),
            PendingOperation.Move => EditorAssetCatalog.Instance.ToResourceKey(currentDirectory),
            PendingOperation.CreateFolder => "New Folder",
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
        case PendingOperation.Rename when path != null:
            if (!IsValidName(operationValue))
            {
                status = "Name contains invalid path characters.";
                return;
            }
            string renamed = Path.Combine(Path.GetDirectoryName(path)!, operationValue);
            succeeded = ProjectAssetOperations.Move(path, renamed, out status);
            if (succeeded) selectedPath = renamed;
            break;
        case PendingOperation.Move when path != null:
            string destinationDirectory = Path.GetFullPath(Path.Combine(projectRoot, operationValue.Replace('/', Path.DirectorySeparatorChar)));
            string moved = Path.Combine(destinationDirectory, Path.GetFileName(path));
            succeeded = ProjectAssetOperations.Move(path, moved, out status);
            if (succeeded) selectedPath = moved;
            break;
        case PendingOperation.Delete when path != null:
            succeeded = ProjectAssetOperations.Delete(path, out status);
            if (succeeded) selectedPath = null;
            break;
        case PendingOperation.CreateFolder:
            if (!IsValidName(operationValue))
            {
                status = "Folder name contains invalid path characters.";
                return;
            }
            string folder = Path.Combine(currentDirectory, operationValue);
            succeeded = ProjectAssetOperations.CreateFolder(folder, out status);
            if (succeeded) selectedPath = folder;
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
