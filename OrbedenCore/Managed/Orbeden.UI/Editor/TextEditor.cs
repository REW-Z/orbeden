using Orbeden;

namespace OrbedenEditor;

/// <summary>文本内容、字体与对齐检视面板。</summary>
[CustomEditor(typeof(Text))]
public sealed class TextEditor : ComponentEditor
{
    /// <summary>绘制多行文本与排版配置。</summary>
    public override void OnDrawInspector()
    {
        if (Target.Ens.GetComponent<Text>() is not Text text) return;
        DrawProperty("enabled");
        string content = text.GetText();
        string label = FindProperty("text")?.HasMultipleDifferentValues == true ? "Text (Mixed)" : "Text";
        if (EditorGUI.InputTextMultiline(label, ref content)) SetValue("text", InteropValue.From(content));
        DrawProperty("font");
        DrawProperty("fontSize");
        DrawProperty("tint");
        DrawProperty("wrap");
        DrawProperty("lineSpacing");

        //绘制九种水平与垂直对齐组合
        EditorGUI.Label("Alignment");
        if (EditorGUI.BeginTable("text_alignment", 3, false))
        {
            string[] columns = ["Left", "Center", "Right"];
            string[] rows = ["Top", "Center", "Bottom"];
            for (int row = 0; row < 3; ++row)
            {
                EditorGUI.TableNextRow();
                for (int column = 0; column < 3; ++column)
                {
                    EditorGUI.TableSetColumnIndex(column);
                    bool selected = (int)text.GetHorizontalAlignment() == column && (int)text.GetVerticalAlignment() == row;
                    if (!EditorGUI.ToggleButton($"{rows[row]} {columns[column]}", selected)) continue;
                    SetValue("horizontalAlignment", InteropValue.From((uint)column));
                    SetValue("verticalAlignment", InteropValue.From((uint)row));
                }
            }
            EditorGUI.EndTable();
        }
        DrawProperty("rasterMode");
        DrawProperty("raycastTarget");
        DrawProperty("material");
        if (text.GetFont() == null) EditorGUI.TextWrapped("Font is empty: using the built-in default font (Noto Sans SC).");
        if (text.GetWrap() && text.GetLayout()?.GetFitWidth() == true)
            EditorGUI.TextWrapped("Fit Width uses the unwrapped text width. Turn it off to wrap within a fixed width.");
    }
}
