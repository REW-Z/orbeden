using Orbeden;
using System.Globalization;

namespace OrbedenEditor;

/// <summary>
/// 项目级编辑器外观配置：属性行标签列宽度、界面字体与字号。
/// 存在项目文件（.oeproj）的 &lt;EditorGuiConfig&gt; 块里，与面板布局 &lt;EditorLayout&gt; 同级。
///
/// 原生侧 EditorProject.cpp 的 ReadEditorGuiConfig / BuildEditorGuiConfigBlock 读写的是同一个块，
/// 块名与属性名两边逐字对应，改一处必须改两处。两边都是「读最新文件 → 只替换自己那一块 →
/// 原子替换」，因此谁先写都不会把对方的改动抹掉。
/// </summary>
internal static class EditorGuiSettings
{
    //内置矢量字体在 font 里的保留值；与原生 EditorGUI.h 的 BuiltinVectorFont 同字面量
    internal const string BuiltinVectorFont = "builtin:vector";

    internal const float DefaultLabelWidth = 120.0f;
    //与原生 EditorGuiConfigState 的缺省值同数，块缺失时两端才不会各说各话
    internal const float DefaultFontSize = 16.0f;
    //内置点阵字体只适合 13px，选它时字号禁用并锁在这个值
    internal const float BitmapFontSize = 13.0f;

    //缓存：换了项目或文件被改过才重新解析
    private static string loadedPath = "\0";
    private static DateTime loadedWriteTime;

    /// <summary>标签列宽度。</summary>
    internal static float LabelWidth { get; private set; } = DefaultLabelWidth;

    /// <summary>字体：空 = 内置点阵，BuiltinVectorFont = 内置矢量，其余是字体文件路径。</summary>
    internal static string Font { get; private set; } = string.Empty;

    /// <summary>字号。</summary>
    internal static float FontSize { get; private set; } = DefaultFontSize;

    /// <summary>最近一次读取或写入的错误。</summary>
    internal static string Error { get; private set; } = string.Empty;

    /// <summary>当前字体是否是内置点阵字体；是的话字号没有意义。</summary>
    internal static bool UsesBitmapFont => string.IsNullOrEmpty(Font);

    /// <summary>从项目文件重新读取；没有项目或没有该块时回默认值。</summary>
    internal static void Refresh()
    {
        string path = ProjectFilePath();
        if (path.Length == 0)
        {
            Reset();
            loadedPath = "\0";
            return;
        }

        DateTime writeTime;
        try
        {
            writeTime = File.GetLastWriteTimeUtc(path);
        }
        catch (IOException)
        {
            writeTime = default;
        }
        catch (UnauthorizedAccessException)
        {
            writeTime = default;
        }
        if (string.Equals(path, loadedPath, StringComparison.Ordinal) && writeTime == loadedWriteTime) return;

        Reset();
        loadedPath = path;
        loadedWriteTime = writeTime;
        try
        {
            Apply(ExtractBlock(File.ReadAllText(path, InteropText.CreateUtf8Encoding(false))));
        }
        catch (IOException exception)
        {
            Error = "Project file could not be read: " + exception.Message;
        }
        catch (UnauthorizedAccessException exception)
        {
            Error = "Project file could not be read: " + exception.Message;
        }
    }

    /// <summary>把配置写回项目文件的配置块；其它内容原样保留。</summary>
    internal static bool Save(float labelWidth, string font, float fontSize, out string error)
    {
        error = string.Empty;
        if (!EditorAssetsNative.CanModifyAssets())
        {
            error = "Editor settings cannot be changed while playing.";
            return false;
        }
        if (!float.IsFinite(labelWidth) || labelWidth < 1.0f || labelWidth > 600.0f)
        {
            error = "Label width must be between 1 and 600.";
            return false;
        }
        bool bitmap = string.IsNullOrEmpty(font);
        if (!float.IsFinite(fontSize) || (!bitmap && (fontSize < 8.0f || fontSize > 72.0f)))
        {
            error = "Font size must be between 8 and 72.";
            return false;
        }

        string path = ProjectFilePath();
        if (path.Length == 0)
        {
            error = "Editor settings cannot be changed without a project.";
            return false;
        }

        //内置点阵锁 13：值写进文件也照锁，读回来时说的和实际用的是同一个数
        float storedSize = bitmap ? BitmapFontSize : fontSize;
        string text = ReplaceBlock(ReadText(path), labelWidth, font ?? string.Empty, storedSize);
        string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporary, text, InteropText.CreateUtf8Encoding(false));
            File.Move(temporary, path, true);
        }
        catch (IOException exception)
        {
            error = "Editor settings could not be saved: " + exception.Message;
            return false;
        }
        catch (UnauthorizedAccessException exception)
        {
            error = "Editor settings could not be saved: " + exception.Message;
            return false;
        }

        Error = string.Empty;
        loadedPath = "\0";
        Refresh();
        //原生按新的配置重读一遍项目文件，再重建字体图集、重设标签列宽度
        EditorApplication.RequestEditorAction(EditorRequestKind.EditorAppearance);
        EditorApplication.RequestRepaint();
        return true;
    }

    //回到默认值
    private static void Reset()
    {
        LabelWidth = DefaultLabelWidth;
        Font = string.Empty;
        FontSize = DefaultFontSize;
        Error = string.Empty;
    }

    //读取基准：项目文件的完整路径
    private static string ProjectFilePath() => EditorApplication.GetProjectText(EditorProjectField.ProjectFile);

    //从项目文件文本里取出配置块；没有块返回空串
    private static string ExtractBlock(string content)
    {
        int start = content.IndexOf("<EditorGuiConfig", StringComparison.Ordinal);
        if (start < 0) return string.Empty;
        int end = content.IndexOf('>', start);
        return end < 0 ? string.Empty : content[start..(end + 1)];
    }

    //把块里的属性读进静态字段；缺哪个就用哪个的默认值
    private static void Apply(string token)
    {
        if (token.Length == 0) return;
        LabelWidth = ReadFloat(token, "labelWidth", DefaultLabelWidth);
        Font = ReadAttribute(token, "font");
        FontSize = ReadFloat(token, "fontSize", DefaultFontSize);
        if (UsesBitmapFont) FontSize = BitmapFontSize;
        //@ 前缀用来挡 Windows 的 \\?\ 长路径语义，值本身不该带
        if (Font.StartsWith('@')) Font = Font[1..];
    }

    //替换或追加配置块，其余内容逐字保留
    private static string ReplaceBlock(string content, float labelWidth, string font, float fontSize)
    {
        string block = $"    <EditorGuiConfig labelWidth=\"{Format(labelWidth)}\" font=\"{Escape(font)}\" fontSize=\"{Format(fontSize)}\" />";
        int start = content.IndexOf("<EditorGuiConfig", StringComparison.Ordinal);
        if (start >= 0)
        {
            //连同它前面那一段缩进与后面那一行换行一起换掉，重复写不会越堆越多行
            int lineStart = start;
            while (lineStart > 0 && (content[lineStart - 1] == ' ' || content[lineStart - 1] == '\t')) --lineStart;
            int end = content.IndexOf('>', start);
            if (end >= 0)
            {
                ++end;
                while (end < content.Length && (content[end] == '\r' || content[end] == '\n')) ++end;
                return content[..lineStart] + block + "\n" + content[end..];
            }
        }

        int close = content.LastIndexOf("</OrbedenProject>", StringComparison.Ordinal);
        if (close < 0) return content;
        string prefix = close > 0 && content[close - 1] != '\n' ? "\n" : string.Empty;
        return content[..close] + prefix + block + "\n" + content[close..];
    }

    //读取文本文件；失败返回空串，调用方按"没有块"处理
    private static string ReadText(string path)
    {
        try
        {
            return File.ReadAllText(path, InteropText.CreateUtf8Encoding(false));
        }
        catch (IOException)
        {
            return string.Empty;
        }
        catch (UnauthorizedAccessException)
        {
            return string.Empty;
        }
    }

    //取一个浮点属性
    private static float ReadFloat(string token, string name, float fallback)
    {
        string text = ReadAttribute(token, name);
        return float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float value) && float.IsFinite(value)
            ? value : fallback;
    }

    //取一个字符串属性；属性不存在返回空串
    private static string ReadAttribute(string token, string name)
    {
        string marker = name + "=\"";
        int start = token.IndexOf(marker, StringComparison.Ordinal);
        if (start < 0) return string.Empty;
        start += marker.Length;
        int end = token.IndexOf('"', start);
        return end < 0 ? string.Empty : Unescape(token[start..end]);
    }

    private static string Escape(string value) =>
        value.Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;").Replace("\"", "&quot;");

    private static string Unescape(string value) =>
        value.Replace("&quot;", "\"").Replace("&lt;", "<").Replace("&gt;", ">").Replace("&amp;", "&");

    private static string Format(float value) => value.ToString("0.###", CultureInfo.InvariantCulture);
}
