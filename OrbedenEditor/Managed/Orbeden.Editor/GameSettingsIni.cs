using Orbeden;

namespace OrbedenEditor;

/// <summary>
/// 项目根 GameSettings.ini 的读写原语：分块格式，每个子系统整块重写自己那段，其他分块原样保留。
/// 目前三个分块各有主人——[Rendering] 归 EditorDisplaySettings，[Layers] 归 EditorLayerSettings；
/// 写同一个文件的代码因此必须共用这里的「只换自己那段」，各写一份迟早会把对方的分块抹掉。
/// 原生的 DisplaySettings 与 LayerSettings 读的是同一个文件，格式与原生解析保持一致。
/// </summary>
internal static class GameSettingsIni
{
    /// <summary>与原生 GameSettingsFile::FileName 同字面量。</summary>
    internal const string FileName = "GameSettings.ini";

    /// <summary>项目根下的配置文件路径。项目根是内容根的上级目录，内容根固定为 &lt;项目根&gt;/Content；
    /// 配置不放在内容根里，那里是会被导入与打包的游戏资源。</summary>
    internal static string GetPath()
    {
        string root = PathDefines.ContentRoot;
        if (root.Length == 0) return string.Empty;
        string? projectRoot = Path.GetDirectoryName(root.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
        return projectRoot is null ? string.Empty : Path.Combine(projectRoot, FileName);
    }

    /// <summary>读取整个文件；不存在或读不到时返回空串，调用方按「没有分块」处理。</summary>
    internal static string Read(string path)
    {
        if (path.Length == 0) return string.Empty;
        try
        {
            return File.Exists(path) ? File.ReadAllText(path, InteropText.CreateUtf8Encoding(false)) : string.Empty;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return string.Empty;
        }
    }

    /// <summary>识别 "[分块名]" 行并切换当前分块。</summary>
    internal static bool TryReadSection(string line, ref string section)
    {
        if (line.Length < 2 || line[0] != '[' || line[^1] != ']') return false;
        section = line[1..^1];
        return true;
    }

    /// <summary>取出某个分块里的原始行（不含分块标题）；没有该分块时返回空列表。</summary>
    internal static List<string> ReadSection(string content, string name)
    {
        List<string> lines = [];
        if (content.Length == 0) return lines;

        string section = string.Empty;
        bool inside = false;
        foreach (string line in content.Split('\n'))
        {
            string trimmed = line.TrimEnd('\r');
            if (TryReadSection(trimmed, ref section))
            {
                inside = section == name;
                continue;
            }
            if (inside) lines.Add(trimmed);
        }
        return lines;
    }

    /// <summary>整块替换某个分块：命中就先落新内容、丢弃块里的旧行（重复的同名前缀块一并丢弃），
    /// 其他分块与行逐字保留；文件里还没有这个分块时追加到末尾。</summary>
    internal static string ReplaceSection(string content, string name, IReadOnlyList<string> lines)
    {
        List<string> output = [];
        string section = string.Empty;
        bool inside = false;
        bool replaced = false;
        foreach (string raw in content.Split('\n'))
        {
            string line = raw.TrimEnd('\r');
            if (TryReadSection(line, ref section))
            {
                inside = section == name;
                if (!inside) output.Add(line);
                else if (!replaced)
                {
                    output.Add("[" + name + "]");
                    output.AddRange(lines);
                    replaced = true;
                }

                continue;
            }

            if (inside) continue;
            output.Add(line);
        }

        //Split('\n') 在末尾换行后会产生一个空尾项，去掉它，由下面统一补尾换行
        if (output.Count != 0 && output[^1].Length == 0) output.RemoveAt(output.Count - 1);
        if (!replaced)
        {
            //文件里还没有这个分块：追加到末尾，前面已有内容时空一行隔开
            if (output.Count != 0 && output[^1].Length != 0) output.Add(string.Empty);
            output.Add("[" + name + "]");
            output.AddRange(lines);
        }

        return string.Join("\n", output) + "\n";
    }

    /// <summary>原子替换配置文件，失败时保留磁盘原文件。</summary>
    internal static bool WriteAtomic(string path, string text, out string error)
    {
        error = string.Empty;
        if (path.Length == 0) { error = "Project settings cannot be written without a project."; return false; }

        string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporary, text, InteropText.CreateUtf8Encoding(false));
            File.Move(temporary, path, true);
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            error = exception.Message;
            return false;
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }
}
