namespace OrbedenEditor;

/// <summary>
/// Ens 子树复制的进程内剪贴板。只活在本次编辑器会话内，不经过系统剪贴板：
/// 粘贴是按快照重建子树，载体与预制体投放、撤销事务同源，不需要另做一种可序列化格式。
/// </summary>
internal static class EditorEnsClipboard
{
    /// <summary>复制下来的子树快照，空串表示剪贴板里没有内容。</summary>
    internal static string Entry { get; private set; } = string.Empty;

    /// <summary>剪贴板里是否有可粘贴的子树快照。</summary>
    internal static bool HasEntry => Entry.Length != 0;

    /// <summary>记录一次 Ens 子树复制；快照为空表示捕获失败，剪贴板保持空。</summary>
    internal static void Capture(string snapshot) => Entry = snapshot;
}
