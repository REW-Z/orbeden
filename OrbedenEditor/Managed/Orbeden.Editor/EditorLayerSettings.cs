using System.Globalization;
using System.Numerics;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

/// <summary>项目层名称与对称碰撞矩阵的持久化和 Inspector 控件。
/// 存在项目根 GameSettings.ini 的 [Layers] 分块里，本类只读写这一段，[Rendering] 等由 GameSettingsIni 原样保留。</summary>
internal static class EditorLayerSettings
{
    private const string SectionName = "Layers";
    internal static string[] Names { get; private set; } = Defaults();
    internal static uint[] Masks { get; private set; } = Enumerable.Repeat(uint.MaxValue, 32).ToArray();
    private static string[] labels = Enumerable.Range(0, 32).Select(Label).ToArray();
    internal static string Error { get; private set; } = string.Empty;
    private static string loadedRoot = "\0";
    private static DateTime loadedTime;

    /// <summary>创建稳定的默认名称，层零沿用 Default。</summary>
    internal static string[] Defaults() => Enumerable.Range(0, 32).Select(index => index == 0 ? "Default" : "Layer " + index).ToArray();

    /// <summary>在项目或文件变化时读取配置，无文件时保持旧项目默认行为。
    /// 文件在但缺 [Layers] 分块时，用默认值把这一段补进文件——项目一开始就能在 ini 里看到并直接改。</summary>
    internal static void Refresh()
    {
        string root = PathDefines.ContentRoot;
        string path = GameSettingsIni.GetPath();
        DateTime modified = path.Length == 0 ? DateTime.MinValue : File.GetLastWriteTimeUtc(path);
        if (root == loadedRoot && modified == loadedTime) return;
        loadedRoot = root;
        loadedTime = modified;
        Names = Defaults();
        labels = Enumerable.Range(0, 32).Select(Label).ToArray();
        Masks = Enumerable.Repeat(uint.MaxValue, 32).ToArray();
        Error = string.Empty;
        if (path.Length == 0 || !File.Exists(path)) return;

        List<string> lines = GameSettingsIni.ReadSection(GameSettingsIni.Read(path), SectionName);
        if (lines.Count == 0)
        {
            //读的时候顺手补一段默认值；走 Play 或读不了盘时就只按默认值用，不碰文件。
            //成功时 Save 内部已经重读过一次，Error 由那一次负责，这里只管失败的报错
            if (EditorAssetsNative.CanModifyAssets() && !Save(Names, Masks, out string materializeError))
            {
                Error = materializeError;
            }
            return;
        }

        try
        {
            string[] names = new string[32];
            uint[] masks = new uint[32];
            bool[] seen = new bool[32];
            foreach (string line in lines)
            {
                if (line.Length == 0) continue;
                string[] parts = line.Split('\t');
                if (parts.Length != 3 || !int.TryParse(parts[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out int index)
                    || index < 0 || index >= 32
                    || parts[2].Length != 8
                    || !uint.TryParse(parts[2], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out masks[index]))
                    throw new InvalidDataException("Invalid layer row: " + line);
                names[index] = parts[1];
                seen[index] = true;
            }
            for (int index = 0; index < 32; ++index)
                if (!seen[index]) throw new InvalidDataException("Missing layer row at index " + index);
            for (int row = 0; row < 32; ++row)
                for (int column = 0; column < 32; ++column)
                    if (((masks[row] >> column) & 1u) != ((masks[column] >> row) & 1u))
                        throw new InvalidDataException("Collision matrix must be symmetric.");
            Names = names;
            labels = Enumerable.Range(0, 32).Select(Label).ToArray();
            Masks = masks;
        }
        catch (Exception exception) when (exception is InvalidDataException)
        {
            Error = exception.Message;
        }
    }

    /// <summary>整块重写 [Layers]，其他分块原样保留；失败时磁盘原文件不动。</summary>
    internal static bool Save(string[] names, uint[] masks, out string error)
    {
        error = string.Empty;
        if (!EditorAssetsNative.CanModifyAssets()) { error = "Project settings cannot be changed while playing."; return false; }
        string path = GameSettingsIni.GetPath();
        if (path.Length == 0) { error = "Project settings cannot be changed without a project."; return false; }

        List<string> lines = new(32);
        for (int index = 0; index < 32; ++index)
        {
            string name = names[index].Trim();
            if (name.IndexOfAny(['\t', '\r', '\n']) >= 0) { error = "Layer names cannot contain tabs or line breaks."; return false; }
            lines.Add(index.ToString(CultureInfo.InvariantCulture) + "\t" + name + "\t" + masks[index].ToString("X8", CultureInfo.InvariantCulture));
        }

        string content = GameSettingsIni.ReplaceSection(GameSettingsIni.Read(path), SectionName, lines);
        if (!GameSettingsIni.WriteAtomic(path, content, out error)) return false;

        loadedRoot = "\0";
        Refresh();
        EditorApplication.RequestRepaint();
        return true;
    }

    /// <summary>名称为空时使用层号，始终显示索引以区分重名层。</summary>
    internal static string Label(int index) => string.IsNullOrWhiteSpace(Names[index]) ? "Layer " + index : $"{index}: {Names[index]}";

    /// <summary>按组件字段语义区分单层选择与多层掩码。</summary>
    internal static bool TryGetField(string type, string name, out bool mask)
    {
        bool? selection = (type, name) switch
        {
            ("Camera", "drawLayerMask") or ("WheelCollider", "groundQueryLayer") => true,
            _ when (type.EndsWith("Collider", StringComparison.Ordinal) || type is "HeightField" or "CharacterController")
                && name == "collisionMask" => true,
            _ => null,
        };
        mask = selection ?? false;
        return selection.HasValue;
    }

    /// <summary>绘制单层或位掩码选择，保持历史非单个位值直到用户明确选择。</summary>
    internal static bool Draw(string label, PropertyValue property, bool mask, out InteropValue value)
    {
        uint selected;
        if (property.Kind == InteropValueKind.Int32)
        {
            property.Value.TryGet(out int current);
            selected = unchecked((uint)current);
        }
        else property.Value.TryGet(out selected);
        bool changed = Draw(label, ref selected, mask, property.HasMultipleDifferentValues);
        value = property.Kind == InteropValueKind.Int32 ? InteropValue.From(unchecked((int)selected)) : InteropValue.From(selected);
        return changed;
    }

    /// <summary>绘制共享层选择控件，供组件属性与嵌套配置复用。</summary>
    internal static bool Draw(string label, ref uint selected, bool mask, bool mixed = false)
    {
        Refresh();
        uint current = selected;
        string preview = mixed ? "Mixed"
            : mask ? current == 0 ? "Nothing" : current == uint.MaxValue ? "Everything" : $"{BitOperations.PopCount(current)} layers"
            : BitOperations.IsPow2(current) ? Label(BitOperations.TrailingZeroCount(current)) : $"Custom (0x{current:X8})";
        if (mask)
        {
            Span<bool> selection = stackalloc bool[32];
            for (int index = 0; index < selection.Length; ++index) selection[index] = (current & (1u << index)) != 0;
            if (!EditorGUI.MultiSelectCombo(label, preview, labels, selection)) return false;
            selected = 0;
            for (int index = 0; index < selection.Length; ++index)
                if (selection[index]) selected |= 1u << index;
            return selected != current || mixed;
        }

        bool changed = false;
        if (!EditorGUI.BeginCombo(label, preview)) return false;
        try
        {
            for (int index = 0; index < 32; ++index)
            {
                uint bit = 1u << index;
                if (EditorGUI.Selectable(labels[index], current == bit)) { selected = bit; changed = true; }
            }
        }
        finally { EditorGUI.EndCombo(); }
        return changed && (selected != current || mixed);
    }
}
