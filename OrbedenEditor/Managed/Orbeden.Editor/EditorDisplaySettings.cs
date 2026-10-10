using System.Globalization;
using System.IO;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

/// <summary>项目级显示设置（曝光）的持久化，存于项目根的 GameSettings.ini，格式与原生 DisplaySettings 的解析保持一致。
/// 文件按分块组织，本类只读写 [Rendering] 分块，其他分块原样保留。</summary>
internal static class EditorDisplaySettings
{
    internal const string FileName = "GameSettings.ini";
    private const string SectionName = "Rendering";
    internal const float DefaultExposure = 1.0f;

    /// <summary>线性曝光倍数，作用于色调映射之前。</summary>
    internal static float Exposure { get; private set; } = DefaultExposure;
    internal static string Error { get; private set; } = string.Empty;

    private static string loadedRoot = "\0";
    private static DateTime loadedTime;

    /// <summary>在项目或文件变化时读取配置，无文件时使用默认曝光。</summary>
    internal static void Refresh()
    {
        string root = PathDefines.ContentRoot;
        string path = GameSettingsIni.GetPath();
        DateTime modified = path.Length == 0 ? DateTime.MinValue : File.GetLastWriteTimeUtc(path);
        if (root == loadedRoot && modified == loadedTime) return;
        loadedRoot = root;
        loadedTime = modified;
        Exposure = DefaultExposure;
        Error = string.Empty;
        if (path.Length == 0 || !File.Exists(path)) return;

        try
        {
            string[] lines = File.ReadAllLines(path);
            string section = string.Empty;
            for (int index = 0; index < lines.Length; ++index)
            {
                string line = lines[index];
                if (line.Length == 0) continue;
                if (GameSettingsIni.TryReadSection(line, ref section)) continue;
                //不认识的块整块跳过：文件里的块可能属于更晚的引擎版本
                if (section != SectionName) continue;

                string[] parts = line.Split('\t');
                //不认识的键直接跳过，与原生解析一致，便于以后追加参数
                if (parts.Length != 2 || parts[0] != "exposure") continue;
                if (!float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out float parsed) || !(parsed > 0.0f))
                    throw new InvalidDataException("Exposure must be a positive number.");
                Exposure = parsed;
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            Error = exception.Message;
        }
    }

    /// <summary>原子替换配置文件，失败时保留磁盘原文件。</summary>
    internal static bool Save(float exposure, out string error)
    {
        error = string.Empty;
        if (!EditorAssetsNative.CanModifyAssets()) { error = "Project settings cannot be changed while playing."; return false; }
        if (!(exposure > 0.0f)) { error = "Exposure must be a positive number."; return false; }

        string path = GameSettingsIni.GetPath();
        if (path.Length == 0) { error = "Project settings cannot be changed without a project."; return false; }

        string content = GameSettingsIni.ReplaceSection(GameSettingsIni.Read(path), SectionName,
            ["exposure\t" + exposure.ToString("R", CultureInfo.InvariantCulture)]);
        if (!GameSettingsIni.WriteAtomic(path, content, out error)) return false;

        loadedRoot = "\0";
        Refresh();
        EditorApplication.RequestRepaint();
        return true;
    }
}
