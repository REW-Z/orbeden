namespace OrbedenEditor;

/// <summary>
/// 项目资源复制的进程内剪贴板。只活在本次编辑器会话内，不经过系统剪贴板：
/// 粘贴按内容根内的绝对路径复制源文件，路径出了内容根或被删掉都会被粘贴操作自己挡下。
/// </summary>
internal static class EditorAssetClipboard
{
    /// <summary>复制下来的资源绝对路径，空串表示剪贴板里没有内容。</summary>
    internal static string Entry { get; private set; } = string.Empty;

    /// <summary>剪贴板里是否有可粘贴的资源路径。</summary>
    internal static bool HasEntry => Entry.Length != 0;

    /// <summary>记录一次资源路径复制。</summary>
    internal static void Capture(string fullPath) => Entry = fullPath;
}
