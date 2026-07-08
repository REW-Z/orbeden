namespace Orbeden;

/// <summary>当前项目路径定义访问。</summary>
public static class PathDefines
{
    /// <summary>当前项目根目录。</summary>
    public static string ProjectRoot => PathDefinesBind.GetProjectRoot();

    /// <summary>解析项目相对路径。</summary>
    public static string GetProjectFilePath(string path)
    {
        return PathDefinesBind.GetProjectFilePath(path);
    }
}
