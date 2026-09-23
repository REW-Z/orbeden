using System.Globalization;
using System.Numerics;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

/// <summary>项目层名称与对称碰撞矩阵的持久化和 Inspector 控件。</summary>
internal static class EditorLayerSettings
{
    internal const string FileName = "ProjectSettings.layers";
    internal static string[] Names { get; private set; } = Defaults();
    internal static uint[] Masks { get; private set; } = Enumerable.Repeat(uint.MaxValue, 32).ToArray();
    internal static string Error { get; private set; } = string.Empty;
    private static string loadedRoot = "\0";
    private static DateTime loadedTime;

    /// <summary>创建稳定的默认名称，层零沿用 Default。</summary>
    internal static string[] Defaults() => Enumerable.Range(0, 32).Select(index => index == 0 ? "Default" : "Layer " + index).ToArray();

    /// <summary>在项目或文件变化时读取配置，无文件时保持旧项目默认行为。</summary>
    internal static void Refresh()
    {
        string root = PathDefines.ContentRoot;
        string path = Path.Combine(root, FileName);
        DateTime modified = File.GetLastWriteTimeUtc(path);
        if (root == loadedRoot && modified == loadedTime) return;
        loadedRoot = root;
        loadedTime = modified;
        Names = Defaults();
        Masks = Enumerable.Repeat(uint.MaxValue, 32).ToArray();
        Error = string.Empty;
        if (root.Length == 0 || !File.Exists(path)) return;
        try
        {
            string[] lines = File.ReadAllLines(path);
            if (lines.Length != 33 || lines[0] != "OrbedenLayers1") throw new InvalidDataException("Invalid layer settings header or row count.");
            string[] names = new string[32];
            uint[] masks = new uint[32];
            for (int index = 0; index < 32; ++index)
            {
                string[] parts = lines[index + 1].Split('\t', 2);
                if (parts.Length != 2 || parts[0].Length != 8
                    || !uint.TryParse(parts[0], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out masks[index]))
                    throw new InvalidDataException("Invalid collision mask at layer " + index);
                names[index] = parts[1];
            }
            for (int row = 0; row < 32; ++row)
                for (int column = 0; column < 32; ++column)
                    if (((masks[row] >> column) & 1u) != ((masks[column] >> row) & 1u))
                        throw new InvalidDataException("Collision matrix must be symmetric.");
            Names = names;
            Masks = masks;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            Error = exception.Message;
        }
    }

    /// <summary>原子替换配置文件，失败时保留磁盘原文件。</summary>
    internal static bool Save(string[] names, uint[] masks, out string error)
    {
        error = string.Empty;
        if (!EditorAssetsNative.CanModifyAssets()) { error = "Project settings cannot be changed while playing."; return false; }
        string path = Path.Combine(PathDefines.ContentRoot, FileName);
        string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            StringBuilder text = new("OrbedenLayers1\n");
            for (int index = 0; index < 32; ++index)
            {
                string name = names[index].Trim();
                if (name.IndexOfAny(['\t', '\r', '\n']) >= 0) throw new InvalidDataException("Layer names cannot contain tabs or line breaks.");
                text.Append(masks[index].ToString("X8", CultureInfo.InvariantCulture)).Append('\t').Append(name).Append('\n');
            }
            File.WriteAllText(temporary, text.ToString(), new UTF8Encoding(false));
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

    /// <summary>名称为空时使用层号，始终显示索引以区分重名层。</summary>
    internal static string Label(int index) => string.IsNullOrWhiteSpace(Names[index]) ? "Layer " + index : $"{index}: {Names[index]}";

    /// <summary>仅对内建组件具有层语义的字段启用专用控件。</summary>
    internal static bool Handles(string type, string name) =>
        (type == "StaticMeshRenderer" && name == "drawLayer")
        || (type == "Camera" && name == "drawLayerMask")
        || ((type.EndsWith("Collider", StringComparison.Ordinal) || type is "HeightField" or "CharacterController")
            && name is "collisionLayer" or "collisionMask");

    /// <summary>绘制单层或位掩码选择，保持历史非单个位值直到用户明确选择。</summary>
    internal static bool Draw(string label, PropertyValue property, out InteropValue value)
    {
        Refresh();
        property.Value.TryGet(out uint current);
        uint selected = current;
        bool mask = property.Name.EndsWith("Mask", StringComparison.Ordinal);
        string preview = property.HasMultipleDifferentValues ? "Mixed"
            : mask ? current == 0 ? "Nothing" : current == uint.MaxValue ? "Everything" : $"{BitOperations.PopCount(current)} layers"
            : BitOperations.IsPow2(current) ? Label(BitOperations.TrailingZeroCount(current)) : $"Custom (0x{current:X8})";
        bool changed = false;
        value = property.Value;
        if (!EditorGUI.BeginCombo(label, preview)) return false;
        try
        {
            if (mask)
            {
                if (EditorGUI.Selectable("Nothing", current == 0)) { selected = 0; changed = true; }
                if (EditorGUI.Selectable("Everything", current == uint.MaxValue)) { selected = uint.MaxValue; changed = true; }
            }
            for (int index = 0; index < 32; ++index)
            {
                uint bit = 1u << index;
                if (mask)
                {
                    bool enabled = (selected & bit) != 0;
                    if (EditorGUI.Checkbox(Label(index) + "##layer_" + index, ref enabled))
                    {
                        selected = enabled ? selected | bit : selected & ~bit;
                        changed = true;
                    }
                }
                else if (EditorGUI.Selectable(Label(index), current == bit)) { selected = bit; changed = true; }
            }
        }
        finally { EditorGUI.EndCombo(); }
        value = InteropValue.From(selected);
        return changed && (selected != current || property.HasMultipleDifferentValues);
    }
}
