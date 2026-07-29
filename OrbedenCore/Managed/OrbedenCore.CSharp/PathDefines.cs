namespace Orbeden;

/// <summary>当前内容路径定义访问。</summary>
public static class PathDefines
{
    /// <summary>当前内容根目录。</summary>
    public static string ContentRoot => PathDefinesBind.GetContentRoot();

    /// <summary>解析内容相对路径。</summary>
    public static string GetContentFilePath(string path)
    {
        return PathDefinesBind.GetContentFilePath(path);
    }
}
