using Orbeden;

namespace OrbedenEditor;

/// <summary>
/// 编辑器自身的外观设置：属性行标签列宽度、界面字体与字号。
/// 存在项目文件的 &lt;EditorGuiConfig&gt; 块里（见 EditorGuiSettings），随项目走，
/// 与「游戏设置」那类随包发布的配置分开。
/// </summary>
internal sealed class EditorSettingsPanel : EditorPanel
{
    private string root = "\0";
    private float labelWidth = EditorGuiSettings.DefaultLabelWidth;
    private float fontSize = EditorGuiSettings.DefaultFontSize;
    private bool fontDirty;
    private string font = string.Empty;
    private string status = string.Empty;
    //OS 字体目录只在首次展开下拉时枚举一次，之后复用
    private List<string> systemFonts = [];
    private bool fontsEnumerated;
    private string fontSearch = string.Empty;

    public override EditorPanelInfo Info => new("editor_settings", "Editor Settings", false,
        new vector2(520, 300), PanelDockPlacement.Floating, 0.3f, 120);

    /// <summary>关闭项目时把未提交的外观改动落盘。</summary>
    public override bool SavePendingChanges()
    {
        if (!Dirty) return true;
        if (root != PathDefines.ContentRoot) return false;
        if (!Apply()) return false;
        status = "Editor settings saved.";
        return true;
    }

    //草稿与已保存值是否不同
    private bool Dirty => fontDirty || labelWidth != EditorGuiSettings.LabelWidth
        || fontSize != EditorGuiSettings.FontSize
        || !string.Equals(font, EditorGuiSettings.Font, StringComparison.Ordinal);

    //把草稿写回项目文件；生效由原生按新配置重建负责
    private bool Apply()
    {
        if (!EditorGuiSettings.Save(labelWidth, font, fontSize, out string error))
        {
            status = error;
            return false;
        }
        labelWidth = EditorGuiSettings.LabelWidth;
        font = EditorGuiSettings.Font;
        fontSize = EditorGuiSettings.FontSize;
        fontDirty = false;
        status = "Editor settings applied.";
        return true;
    }

    /// <summary>从项目文件重新读取草稿。</summary>
    private void Reload()
    {
        root = PathDefines.ContentRoot;
        EditorGuiSettings.Refresh();
        labelWidth = EditorGuiSettings.LabelWidth;
        font = EditorGuiSettings.Font;
        fontSize = EditorGuiSettings.FontSize;
        fontSearch = string.Empty;
        fontDirty = false;
        status = string.Empty;
    }

    /// <summary>绘制三个外观项与 Apply / Revert。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        if (string.IsNullOrWhiteSpace(PathDefines.ContentRoot)) { EditorGUI.Label("No project loaded."); return; }
        if (root != PathDefines.ContentRoot) Reload();
        if (EditorGuiSettings.Error.Length != 0) EditorGUI.Label(EditorGuiSettings.Error);

        EditorGUI.Label(Dirty ? "Editor Settings (Unsaved)" : "Editor Settings");
        EditorGUI.BeginDisabled(!EditorAssetsNative.CanModifyAssets());
        try
        {
            if (EditorGUI.Button(Dirty ? "Apply *" : "Apply")) Apply();
            EditorGUI.SameLine();
            if (EditorGUI.Button("Revert")) Reload();
            if (status.Length != 0) EditorGUI.Label(status);
            EditorGUI.Separator();

            if (EditorGUI.InputFloat("Label Width", ref labelWidth)) labelWidth = Math.Clamp(labelWidth, 1.0f, 600.0f);
            EditorGUI.Label("Width of the label column in property rows. Every input, dropdown, color and object field starts at this column.");
            DrawFontPicker();
        }
        finally { EditorGUI.EndDisabled(); }
    }

    //字体来源：内置点阵 / 内置矢量 / 系统字体目录里的字体文件
    private void DrawFontPicker()
    {
        bool bitmap = string.IsNullOrEmpty(font);
        string preview = bitmap ? BuiltinBitmapLabel
            : font == EditorGuiSettings.BuiltinVectorFont ? BuiltinVectorLabel
            : Path.GetFileNameWithoutExtension(font);
        if (EditorGUI.BeginCombo("Font", preview))
        {
            try
            {
                if (EditorGUI.Selectable(BuiltinBitmapLabel, bitmap)) { font = string.Empty; fontDirty = true; }
                if (EditorGUI.Selectable(BuiltinVectorLabel, font == EditorGuiSettings.BuiltinVectorFont))
                {
                    font = EditorGuiSettings.BuiltinVectorFont;
                    fontDirty = true;
                }
                EnumerateSystemFonts();
                EditorGUI.InputText("Search##editor_settings_font", ref fontSearch);
                foreach (string path in systemFonts)
                {
                    string name = Path.GetFileNameWithoutExtension(path);
                    if (fontSearch.Length != 0 && name.IndexOf(fontSearch, StringComparison.OrdinalIgnoreCase) < 0) continue;
                    if (!EditorGUI.Selectable(name + "##" + path, string.Equals(font, path, StringComparison.Ordinal)))
                        continue;
                    font = path;
                    fontDirty = true;
                }
            }
            finally { EditorGUI.EndCombo(); }
        }

        //内置点阵只适合 13px：选它时字号没有意义，按 13 显示并禁用
        EditorGUI.BeginDisabled(bitmap);
        try
        {
            if (EditorGUI.InputFloat("Font Size", ref fontSize)) fontSize = Math.Clamp(fontSize, 8.0f, 72.0f);
        }
        finally { EditorGUI.EndDisabled(); }
        if (bitmap) fontSize = EditorGuiSettings.BitmapFontSize;
        EditorGUI.Label(bitmap
            ? "The built-in pixel font only reads well at 13 px. Pick the vector font or a system font to set a size."
            : "Font size in pixels. The atlas is rebuilt at this size when you apply.");
    }

    //枚举 OS 字体目录里的可缩放字体；失败就当没有系统字体，内置两种仍然可用
    private void EnumerateSystemFonts()
    {
        if (fontsEnumerated) return;
        fontsEnumerated = true;
        try
        {
            string directory = Environment.GetFolderPath(Environment.SpecialFolder.Fonts);
            if (directory.Length == 0 || !Directory.Exists(directory)) return;
            foreach (string path in Directory.EnumerateFiles(directory))
            {
                string extension = Path.GetExtension(path);
                if (!extension.Equals(".ttf", StringComparison.OrdinalIgnoreCase)
                    && !extension.Equals(".otf", StringComparison.OrdinalIgnoreCase))
                    continue;
                //.fon 之类的点阵字体不在枚举范围内：它们不能按字号缩放
                systemFonts.Add(path);
            }
            systemFonts.Sort(StringComparer.OrdinalIgnoreCase);
        }
        catch (IOException exception)
        {
            status = "System fonts could not be listed: " + exception.Message;
        }
        catch (UnauthorizedAccessException exception)
        {
            status = "System fonts could not be listed: " + exception.Message;
        }
    }

    private const string BuiltinBitmapLabel = "Built-in (ProggyClean, 13 px)";
    private const string BuiltinVectorLabel = "Built-in (ProggyForever, scalable)";
}
