namespace OrbedenEditor;

/// <summary>管理编辑 World 切换前的保存确认。</summary>
internal static class EditorWorldActions
{
    private static string? pendingKey;
    private static bool openRequested;
    internal static string Status { get; private set; } = string.Empty;

    //请求切换编辑 World
    internal static void RequestOpen(string key)
    {
        if (EditorApplication.IsPlaying || pendingKey != null) return;
        pendingKey = key;
        Status = string.Empty;
        if (EditorApplication.WorldDirty) openRequested = true;
        else if (!CommitOpen()) pendingKey = null;
    }

    //提交切换并清理旧场景编辑状态
    private static bool CommitOpen()
    {
        if (pendingKey == null) return false;
        string key = pendingKey;
        if (!EditorAssetsNative.OpenWorld(key))
        {
            Status = EditorAssetsNative.GetProjectError();
            if (string.IsNullOrEmpty(Status)) Status = "Failed to open World: " + key;
            return false;
        }
        EditorPropertyHistory.Clear();
        EditorObjectField.Clear();
        pendingKey = null;
        Status = "Opened World: " + key;
        return true;
    }

    //绘制保存、放弃与取消确认
    internal static void DrawPendingSwitch()
    {
        const string popup = "Unsaved World changes";
        if (openRequested)
        {
            NativeEditorGUI.OpenPopup(popup);
            openRequested = false;
        }
        if (pendingKey == null || !NativeEditorGUI.BeginPopup(popup)) return;
        try
        {
            EditorGUI.Label("Save changes before opening " + pendingKey + "?");
            if (!string.IsNullOrEmpty(Status)) EditorGUI.Label(Status);
            if (EditorGUI.Button("Save"))
            {
                if (EditorAssetsNative.SaveWorld()) CommitOpen();
                else Status = "World save failed. The current World remains open.";
                if (pendingKey == null) NativeEditorGUI.ClosePopup();
            }
            EditorGUI.SameLine();
            if (EditorGUI.Button("Discard") && CommitOpen()) NativeEditorGUI.ClosePopup();
            EditorGUI.SameLine();
            if (EditorGUI.Button("Cancel"))
            {
                pendingKey = null;
                Status = string.Empty;
                NativeEditorGUI.ClosePopup();
            }
        }
        finally { EditorGUI.EndPopup(); }
    }

    //重置项目切换时的确认状态
    internal static void Clear()
    {
        pendingKey = null;
        openRequested = false;
        Status = string.Empty;
    }
}
