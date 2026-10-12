using Orbeden;

namespace OrbedenEditor;

/// <summary>布局组检视面板：按排列方向显示交叉轴对齐图标。</summary>
[CustomEditor(typeof(LayoutBox))]
public sealed class LayoutBoxEditor : ComponentEditor
{
    private readonly UIAlignmentPicker alignmentPicker = new();

    /// <summary>绘制布局属性并以图标替换交叉轴对齐字段。</summary>
    public override void OnDrawInspector()
    {
        foreach (PropertyValue property in Properties.Properties)
        {
            PropertyValue? orientation = FindProperty("orientation");
            if (property.Name != "crossAlignment" || orientation == null || orientation.HasMultipleDifferentValues)
            {
                DrawProperty(property.Name);
                continue;
            }

            //读取暂存方向并提交对齐选择
            orientation.Value.TryGet(out uint direction);
            property.Value.TryGet(out uint alignment);
            bool mixed = property.HasMultipleDifferentValues;
            EditorGUI.Label(mixed ? "Cross Alignment (Mixed)" : "Cross Alignment");
            int selected = alignmentPicker.Draw("layout_alignment", mixed ? -1 : (int)alignment,
                (UILayoutOrientation)direction);
            if (selected >= 0) SetValue(property.Name, InteropValue.From((uint)selected));
        }
    }
}
