using Orbeden;

namespace OrbedenEditor;

/// <summary>文本内容、字体与对齐检视面板。</summary>
[CustomEditor(typeof(Text))]
public sealed class TextEditor : ComponentEditor
{
    private readonly UIAlignmentPicker alignmentPicker = new();

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
        PropertyValue? horizontal = FindProperty("horizontalAlignment");
        PropertyValue? vertical = FindProperty("verticalAlignment");
        bool mixed = horizontal?.HasMultipleDifferentValues == true || vertical?.HasMultipleDifferentValues == true;
        EditorGUI.Label(mixed ? "Alignment (Mixed)" : "Alignment");
        int current = mixed ? -1 : (int)text.GetVerticalAlignment() * 3 + (int)text.GetHorizontalAlignment();
        int selected = alignmentPicker.Draw("text_alignment", current);
        if (selected >= 0)
        {
            SetValue("horizontalAlignment", InteropValue.From((uint)(selected % 3)));
            SetValue("verticalAlignment", InteropValue.From((uint)(selected / 3)));
        }
        DrawProperty("raycastTarget");
        DrawProperty("material");
        if (text.GetWrap() && text.GetLayout()?.GetFitWidth() == true)
            EditorGUI.Warning("Fit Width prevents wrapping. Disable it to wrap within a fixed width.");
    }
}
