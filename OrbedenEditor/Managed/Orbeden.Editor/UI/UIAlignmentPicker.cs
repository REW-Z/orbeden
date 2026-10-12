using Orbeden;

namespace OrbedenEditor;

/// <summary>文字与布局组共用的固定比例对齐图标选择器。</summary>
internal sealed class UIAlignmentPicker
{
    private readonly EditorRectPrimitive[] rects = new EditorRectPrimitive[9 * 7];
    private static readonly string[] textLabels =
    [
        "Top Left", "Top Center", "Top Right",
        "Middle Left", "Middle Center", "Middle Right",
        "Bottom Left", "Bottom Center", "Bottom Right"
    ];
    private static readonly string[] verticalLayoutLabels = ["Left (Start)", "Center", "Right (End)", "Stretch Width"];
    private static readonly string[] horizontalLayoutLabels = ["Bottom (Start)", "Center", "Top (End)", "Stretch Height"];

    /// <summary>绘制对齐选项；无方向时显示文字九宫格，有方向时显示布局交叉轴，返回点击项或 -1。</summary>
    internal int Draw(string id, int selected, UILayoutOrientation? orientation = null)
    {
        const float CellSize = 48;
        const float Gap = 4;
        bool crossAxis = orientation.HasValue;
        bool horizontal = orientation == UILayoutOrientation.Horizontal;
        int columns = crossAxis ? 4 : 3;
        string[] labels = crossAxis ? horizontal ? horizontalLayoutLabels : verticalLayoutLabels : textLabels;
        float width = columns * CellSize + (columns - 1) * Gap;
        float scale = MathF.Min(1, MathF.Max(1, NativeEditorGUI.GetContentRegionAvail().x) / width);
        vector2 origin = EditorGUI.CursorScreenPosition;
        EditorTheme theme = EditorTheme.Current;
        int count = 0;
        int clicked = -1;
        EditorGUI.PushId(id);
        try
        {
            for (int index = 0; index < labels.Length; ++index)
            {
                //布置方形点击区与主题边框
                int column = index % columns;
                int row = index / columns;
                float left = origin.x + column * (CellSize + Gap) * scale;
                float top = origin.y + row * (CellSize + Gap) * scale;
                float side = CellSize * scale;
                NativeEditorGUI.SetCursorScreenPos(new vector2(left, top));
                if (NativeEditorGUI.InvisibleButton(labels[index], new vector2(side, side))) clicked = index;
                bool hovered = NativeEditorGUI.IsItemHovered();
                if (hovered) EditorGUI.SetTooltip(labels[index]);
                bool active = index == selected;
                color background = hovered ? theme.Hovered : theme.Header;
                count = EditorRects.Append(rects, count, left, top, left + side, top + side,
                    active ? theme.Active : hovered ? theme.Text : theme.Border);
                rects[count - 1].Rounding = 5 * scale;
                float border = (active ? 2 : 1) * scale;
                count = EditorRects.Append(rects, count, left + border, top + border,
                    left + side - border, top + side - border, background);
                rects[count - 1].Rounding = 4 * scale;

                //绘制容器边界与三条内容线
                float x = left + 9 * scale;
                float y = top + 9 * scale;
                count = EditorRects.Append(rects, count, x, y, x + 30 * scale, y + 30 * scale, theme.Control);
                count = EditorRects.Append(rects, count, x + scale, y + scale,
                    x + 29 * scale, y + 29 * scale, background);
                for (int line = 0; line < 3; ++line)
                {
                    float length = crossAxis && index == 3 ? 24 : line == 1 ? 12 : 18;
                    float alignment = crossAxis ? index == 3 ? 0 : index * 0.5f : column * 0.5f;
                    float along = (24 - length) * alignment;
                    float across = (crossAxis ? 5 : row * 5) + line * 5;
                    float lineX = x + (3 + (horizontal ? across : along)) * scale;
                    float lineY = y + (3 + (horizontal ? 24 - length - along : across)) * scale;
                    count = EditorRects.Append(rects, count, lineX, lineY,
                        lineX + (horizontal ? 3 : length) * scale,
                        lineY + (horizontal ? length : 3) * scale, active ? theme.Active : theme.Text);
                }
            }
            NativeEditorGUI.DrawRects(rects, count);
            NativeEditorGUI.SetCursorScreenPos(new vector2(origin.x, NativeEditorGUI.GetCursorScreenPos().y));
        }
        finally { EditorGUI.PopId(); }
        return clicked;
    }
}
