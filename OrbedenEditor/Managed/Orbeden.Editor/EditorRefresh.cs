using System.Diagnostics;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

/// <summary>Ctrl+R Refresh：重导已加载资源，脚本过期时异步跑 dotnet build，最后由原生在帧边界重载程序集。
///
/// 一次只跑一个子进程，与 EditorAssetCache 一样严格串行。两路输出收进 Task，
/// 只在进程退出后于主线程取用，因此没有锁。
/// 不变量：任何路径都必须恰好结算一次（EditorApplication.CompleteScriptBuild），
/// 否则原生会一直停在"有构建在跑"，Ctrl+R 与 Play 一起失效。</summary>
internal static class EditorRefresh
{
    //构建期间默认用模态浮窗（带取消）；改成 StatusBar 即默认不打断操作
    private const EditorProgressSurface BuildSurface = EditorProgressSurface.Modal;
    //构建转发的输出上限：原生日志保留窗口只有 512 条，整段转发会挤掉用户已有的日志
    private const int MaxForwardedLines = 200;
    //重导原因清单的打印上限：理由同上，首次全量重导时清单可能上百条
    private const int MaxReportedReimports = 50;

    private enum Phase { Idle, Reimport, Compiling }

    private static Phase phase = Phase.Idle;
    //先空转一帧：让进度浮窗画到屏幕上再进阻塞段，否则重导期间屏幕停在上一帧
    private static bool waitOneFrame;
    private static string scriptProject = string.Empty;
    private static bool buildOutdated;
    private static bool compiledScripts;
    private static int reimportedCount;
    private static Process? process;
    private static Task<string>? standardOutput;
    private static Task<string>? standardError;

    /// <summary>开始一次后台构建：先重导已加载资源，再按需编译脚本。</summary>
    internal static void Start(string projectPath, bool reimport, bool outdated)
    {
        if (phase != Phase.Idle)
        {
            //原生已经挡过重入；这里不能结算，否则会把正在跑的这次构建结果顶掉
            EditorConsole.Warning("A script build is already running.");
            return;
        }

        scriptProject = projectPath;
        buildOutdated = outdated;
        compiledScripts = false;
        reimportedCount = 0;
        waitOneFrame = true;
        phase = reimport ? Phase.Reimport : Phase.Compiling;
        EditorProgress.Begin("Script Build", reimport ? "Reimporting resources..." : "Compiling scripts...",
            BuildSurface, true, Cancel);
        EditorApplication.RequestRepaint();
    }

    /// <summary>调度失败时的兜底结算：原生已经在等结果，这里必须无条件补上。</summary>
    internal static void Abort(string reason)
    {
        phase = Phase.Idle;
        waitOneFrame = false;
        KillProcess();
        EditorProgress.End();
        EditorConsole.Error("Script build could not start: " + reason);
        EditorApplication.CompleteScriptBuild(false, false, false, 0);
    }

    /// <summary>推进后台构建；由状态栏每帧调用一次。</summary>
    internal static void Pump()
    {
        try
        {
            if (waitOneFrame)
            {
                waitOneFrame = false;
                EditorApplication.RequestRepaint();
                return;
            }

            if (phase == Phase.Reimport)
            {
                //判定与重导都在本帧同步完成，期间不出帧
                ReimportChangedSources();
                if (!buildOutdated)
                {
                    Finish(true, false);
                    return;
                }
                EditorProgress.Report("Compiling scripts...");
                phase = Phase.Compiling;
                EditorApplication.RequestRepaint();
                return;
            }

            if (phase == Phase.Compiling) PumpCompiling();
        }
        catch (Exception exception)
        {
            EditorConsole.Error("Script build failed: " + exception.Message);
            Finish(false, false);
        }
    }

    //按内容哈希判定逐个重导已加载源：只有导入输入变了的才真正重新导入
    private static void ReimportChangedSources()
    {
        //顺带杀掉在飞的后台导入进程，让判定与重导期间资源缓存静止
        EditorAssetCatalog.Instance.Refresh();

        List<string>? keys = EditorAssetReimportNative.GetLoadedSources();
        if (keys == null)
        {
            //拿不到已加载源就退回无条件全量重导：慢，但不会漏
            EditorConsole.Warning("Loaded source enumeration is unavailable; reimporting everything.");
            reimportedCount = EditorAssetsNative.ReimportAllAssets(EditorAssetCache.EncodeAllSettings());
            return;
        }

        EditorProgress.Report($"Scanning {keys.Count} loaded source file(s)...");
        List<(string Key, string Path, string Settings, string Reason)> pending = [];
        foreach (string key in keys)
        {
            string settingsRows = string.Empty;
            string path = EditorAssetCache.ResolveSourcePath(key);
            if (path.Length == 0)
            {
                pending.Add((key, path, settingsRows, "source file was not found"));
                continue;
            }
            if (EditorAssetCache.NeedsReimport(path, out settingsRows, out string reason))
                pending.Add((key, path, settingsRows, reason));
        }

        if (pending.Count == 0)
        {
            //两种入口都要有反馈，否则一次什么都没做的刷新看起来像没生效
            EditorConsole.Info($"Refresh: nothing to reimport among {keys.Count} loaded source(s).");
            return;
        }

        //先报出触发原因再干活：重导失败时这份清单就是线索
        EditorConsole.Info($"Refresh: {pending.Count} of {keys.Count} loaded source(s) need reimporting.");
        for (int index = 0; index < pending.Count && index < MaxReportedReimports; ++index)
            EditorConsole.Info($"  {pending[index].Key} — {pending[index].Reason}");
        if (pending.Count > MaxReportedReimports)
            EditorConsole.Info($"  ... and {pending.Count - MaxReportedReimports} more.");

        EditorProgress.Report($"Reimporting {pending.Count} of {keys.Count} source file(s)...");
        int count = 0;
        foreach ((string key, string path, string settingsRows, _) in pending)
        {
            if (EditorAssetsNative.ReimportAsset(key, false, settingsRows) <= 0) continue;
            ++count;
            //重导成功才记快照：失败时下次仍判为重导
            if (path.Length != 0) EditorAssetCache.RecordRuntimeImport(path);
        }
        reimportedCount = count;
    }

    //推进编译阶段：起进程、等退出、收结果
    private static void PumpCompiling()
    {
        if (process == null)
        {
            StartBuild();
            return;
        }
        if (!process.HasExited)
        {
            //空闲编辑器不出帧，构建期间自己把每一帧要过来
            EditorApplication.RequestRepaint();
            return;
        }

        int exitCode = process.ExitCode;
        string output = standardOutput?.GetAwaiter().GetResult() ?? string.Empty;
        string error = standardError?.GetAwaiter().GetResult() ?? string.Empty;
        process.Dispose();
        process = null;
        standardOutput = null;
        standardError = null;

        compiledScripts = exitCode == 0;
        ForwardBuildOutput(output, error);
        Finish(exitCode == 0, false);
    }

    //启动 dotnet build 子进程并接上两路异步输出
    private static void StartBuild()
    {
        ProcessStartInfo start = new("dotnet")
        {
            UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
            RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = InteropText.Utf8, StandardErrorEncoding = InteropText.Utf8,
        };
        start.ArgumentList.Add("build");
        start.ArgumentList.Add(scriptProject);
        start.ArgumentList.Add("-c");
        start.ArgumentList.Add("Debug");
        process = Process.Start(start) ?? throw new IOException("Could not start dotnet build.");
        standardOutput = process.StandardOutput.ReadToEndAsync();
        standardError = process.StandardError.ReadToEndAsync();
        EditorApplication.RequestRepaint();
    }

    //取消按钮：结束进程树并结算为已取消
    private static void Cancel()
    {
        if (phase == Phase.Idle) return;
        Finish(false, true);
    }

    //结算一次构建：杀进程、收起进度，再把结果交给原生
    private static void Finish(bool succeeded, bool cancelled)
    {
        if (phase == Phase.Idle) return;
        phase = Phase.Idle;
        waitOneFrame = false;
        KillProcess();
        if (cancelled) compiledScripts = false;
        EditorProgress.End();
        if (cancelled) EditorConsole.Warning("Script build cancelled.");
        else if (!succeeded) EditorConsole.Error("Script build failed. See the lines above for the compiler output.");
        EditorApplication.CompleteScriptBuild(succeeded, cancelled, compiledScripts, reimportedCount);
    }

    //结束正在跑的子进程；与 EditorAssetCache.Stop 同法，被杀进程的异步读取不再回收
    private static void KillProcess()
    {
        Process? dying = process;
        process = null;
        standardOutput = null;
        standardError = null;
        if (dying == null) return;

        try
        {
            if (!dying.HasExited) dying.Kill(entireProcessTree: true);
            dying.WaitForExit();
        }
        catch (Exception exception) when (exception is InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            //进程已经自己结束或权限不足：结算不依赖它，继续往下走
        }
        finally { dying.Dispose(); }
    }

    //把编译输出按行转发进 Console 并分级；只保留最后若干行，避免挤掉用户已有的日志
    private static void ForwardBuildOutput(string output, string error)
    {
        string[] lines = (output + "\n" + error).Split('\n');
        int first = Math.Max(lines.Length - MaxForwardedLines, 0);
        if (first > 0) EditorConsole.Info($"dotnet build printed {lines.Length} line(s); showing the last {MaxForwardedLines}.");

        for (int index = first; index < lines.Length; index++)
        {
            string line = lines[index].TrimEnd('\r');
            if (line.Length == 0) continue;
            if (line.Contains(": error ", StringComparison.Ordinal)
                || line.StartsWith("Build FAILED", StringComparison.Ordinal))
            {
                EditorConsole.Error(line);
            }
            else if (line.Contains(": warning ", StringComparison.Ordinal))
            {
                EditorConsole.Warning(line);
            }
            else EditorConsole.Info(line);
        }
    }
}
