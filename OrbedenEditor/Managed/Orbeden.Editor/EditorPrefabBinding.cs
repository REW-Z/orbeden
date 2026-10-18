namespace OrbedenEditor;

/// <summary>记录预制体来源和实例对象身份</summary>
internal sealed class EditorPrefabBinding
{
    internal string AssetKey = string.Empty;
    internal string RootKey = string.Empty;
    internal Dictionary<string, string> ObjectKeys = new(StringComparer.Ordinal);
}
