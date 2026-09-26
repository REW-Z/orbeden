using System.Text.Json;

namespace OrbedenEditor;

/// <summary>最近项目的持久化记录；访问时间只用于排序，编辑时间来自内容目录。</summary>
internal sealed record RecentProject(string ProjectFile, string Name, DateTime LastOpenedUtc, DateTime LastEditedUtc);
internal sealed record RecentProjectView(RecentProject Project, bool Exists);

/// <summary>管理用户级项目历史，后台读取内容时间，不扫描构建与资源缓存。</summary>
internal static class EditorRecentProjects
{
    private static readonly string historyFile = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Orbeden", "Editor", "recent-projects.json");
    private static List<RecentProject>? projects;
    private static string currentProject = string.Empty;
    internal static string Error { get; private set; } = string.Empty;

    /// <summary>读取有上限且去重的历史，忽略损坏或无效路径。</summary>
    internal static IReadOnlyList<RecentProject> GetProjects()
    {
        if (projects != null) return projects;
        projects = [];
        try
        {
            if (!File.Exists(historyFile)) return projects;
            var stored = JsonSerializer.Deserialize<List<RecentProject>>(File.ReadAllText(historyFile));
            foreach (RecentProject entry in stored ?? [])
            {
                if (entry == null || string.IsNullOrWhiteSpace(entry.ProjectFile)
                    || !Path.GetExtension(entry.ProjectFile).Equals(".oeproj", StringComparison.OrdinalIgnoreCase)) continue;
                try
                {
                    string path = Path.GetFullPath(entry.ProjectFile);
                    if (!projects.Any(item => item.ProjectFile.Equals(path, StringComparison.OrdinalIgnoreCase)))
                        projects.Add(entry with { ProjectFile = path, Name = string.IsNullOrWhiteSpace(entry.Name) ? Path.GetFileNameWithoutExtension(path) : entry.Name });
                }
                catch (ArgumentException) { }
            }
            projects = projects.OrderByDescending(entry => entry.LastOpenedUtc).Take(24).ToList();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        { Error = "Could not read recent projects: " + exception.Message; }
        return projects;
    }

    /// <summary>在当前项目变化时记录访问，覆盖菜单打开、新建和欢迎页打开三条路径。</summary>
    internal static void TrackCurrentProject()
    {
        string path = EditorApplication.GetProjectText(EditorProjectField.ProjectFile);
        if (string.Equals(path, currentProject, StringComparison.OrdinalIgnoreCase)) return;
        currentProject = path;
        if (path.Length == 0) return;
        GetProjects();
        path = Path.GetFullPath(path);
        List<RecentProject> history = projects!;
        RecentProject? previous = history.FirstOrDefault(entry => entry.ProjectFile.Equals(path, StringComparison.OrdinalIgnoreCase));
        string name = EditorApplication.GetProjectText(EditorProjectField.Name);
        if (string.IsNullOrWhiteSpace(name)) name = Path.GetFileNameWithoutExtension(path);
        history.RemoveAll(entry => entry.ProjectFile.Equals(path, StringComparison.OrdinalIgnoreCase));
        history.Insert(0, new(path, name, DateTime.UtcNow, previous?.LastEditedUtc ?? DateTime.MinValue));
        if (history.Count > 24) history.RemoveRange(24, history.Count - 24);
        Save();
    }

    /// <summary>异步读取项目与 Content 修改时间，跳过链接目录并支持取消。</summary>
    internal static Task<RecentProjectView[]> ScanAsync(CancellationToken cancellation)
    {
        RecentProject[] snapshot = GetProjects().ToArray();
        return Task.Run(() =>
        {
            List<RecentProjectView> result = [];
            foreach (RecentProject entry in snapshot)
            {
                cancellation.ThrowIfCancellationRequested();
                bool exists = File.Exists(entry.ProjectFile);
                DateTime edited = entry.LastEditedUtc;
                if (exists)
                {
                    try
                    {
                        string content = Path.Combine(Path.GetDirectoryName(entry.ProjectFile)!, "Content");
                        edited = File.GetCreationTimeUtc(entry.ProjectFile);
                        if (Directory.Exists(content))
                        {
                            edited = Directory.GetLastWriteTimeUtc(content);
                            EnumerationOptions options = new() { RecurseSubdirectories = true, IgnoreInaccessible = true, AttributesToSkip = FileAttributes.ReparsePoint };
                            foreach (string file in Directory.EnumerateFileSystemEntries(content, "*", options))
                            {
                                cancellation.ThrowIfCancellationRequested();
                                try
                                {
                                    DateTime time = File.GetLastWriteTimeUtc(file);
                                    if (time > edited) edited = time;
                                }
                                catch (Exception exception) when (exception is IOException or UnauthorizedAccessException) { }
                            }
                        }
                    }
                    catch (Exception exception) when (exception is IOException or UnauthorizedAccessException) { }
                }
                result.Add(new(entry with { LastEditedUtc = edited }, exists));
            }
            return result.ToArray();
        }, cancellation);
    }

    /// <summary>把扫描结果合并回历史，不覆盖扫描期间新增的访问记录。</summary>
    internal static void UpdateTimes(IReadOnlyList<RecentProjectView> views)
    {
        bool changed = false;
        foreach (RecentProjectView view in views)
        {
            int index = projects!.FindIndex(entry => entry.ProjectFile.Equals(view.Project.ProjectFile, StringComparison.OrdinalIgnoreCase));
            if (index < 0 || projects[index].LastEditedUtc == view.Project.LastEditedUtc) continue;
            projects[index] = projects[index] with { LastEditedUtc = view.Project.LastEditedUtc };
            changed = true;
        }
        if (changed) Save();
    }

    /// <summary>只删除访问记录，不修改项目文件。</summary>
    internal static void Remove(string projectFile)
    {
        GetProjects();
        projects!.RemoveAll(entry => entry.ProjectFile.Equals(projectFile, StringComparison.OrdinalIgnoreCase));
        Save();
    }

    /// <summary>先写临时文件再替换，避免中断时留下半份历史。</summary>
    private static void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(historyFile)!);
            string temporary = historyFile + "." + Environment.ProcessId + ".tmp";
            File.WriteAllText(temporary, JsonSerializer.Serialize(projects, new JsonSerializerOptions { WriteIndented = true }));
            File.Move(temporary, historyFile, overwrite: true);
            Error = string.Empty;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        { Error = "Could not save recent projects: " + exception.Message; }
    }
}
