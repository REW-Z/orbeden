using System.Globalization;
using System.IO;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

/// <summary>项目级显示设置（曝光）的持久化，格式与原生 DisplaySettings 的解析保持一致。</summary>
internal static class EditorDisplaySettings
{
    internal const string FileName = "ProjectSettings.display";
    private const string Header = "OrbedenDisplay1";
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
        string path = Path.Combine(root, FileName);
        DateTime modified = File.GetLastWriteTimeUtc(path);
        if (root == loadedRoot && modified == loadedTime) return;
        loadedRoot = root;
        loadedTime = modified;
        Exposure = DefaultExposure;
        Error = string.Empty;
        if (root.Length == 0 || !File.Exists(path)) return;

        try
        {
            string[] lines = File.ReadAllLines(path);
            if (lines.Length == 0 || lines[0] != Header) throw new InvalidDataException("Invalid display settings header.");
            for (int index = 1; index < lines.Length; ++index)
            {
                string[] parts = lines[index].Split('\t');
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

        string path = Path.Combine(PathDefines.ContentRoot, FileName);
        string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            string text = Header + "\nexposure\t" + exposure.ToString("R", CultureInfo.InvariantCulture) + "\n";
            File.WriteAllText(temporary, text, new UTF8Encoding(false));
            File.Move(temporary, path, true);
            loadedRoot = "\0";
            Refresh();
            EditorApplication.RequestRepaint();
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            error = exception.Message;
            return false;
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}
