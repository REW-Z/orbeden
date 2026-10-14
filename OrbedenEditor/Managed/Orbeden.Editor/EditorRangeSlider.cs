using Orbeden;

namespace OrbedenEditor;

/// <summary>使用可选刻度在指定数值范围内滑动取值。</summary>
public sealed class EditorRangeSlider
{
    private bool dragging;
    private float pressMouseX;
    private float pressValue;
    private bool pressedTick;
    private readonly EditorRectPrimitive[] thumb = new EditorRectPrimitive[1];

    /// <summary>按范围、可选刻度和步长取值，未设步长时有刻度连续、无刻度按范围的 1% 取值。</summary>
    public bool Draw(string id, ref float value, float minimum, float maximum,
        IReadOnlyList<(float value, string label)>? ticks = null, float width = 0f, float? step = null)
    {
        ArgumentException.ThrowIfNullOrEmpty(id);
        if (!float.IsFinite(minimum)) throw new ArgumentOutOfRangeException(nameof(minimum));
        if (!float.IsFinite(maximum) || maximum <= minimum) throw new ArgumentOutOfRangeException(nameof(maximum));
        if (!float.IsFinite(value) || value < minimum || value > maximum) throw new ArgumentOutOfRangeException(nameof(value));
        if (!float.IsFinite(width) || width < 0) throw new ArgumentOutOfRangeException(nameof(width));
        if (step.HasValue && (!float.IsFinite(step.Value) || step.Value < 0)) throw new ArgumentOutOfRangeException(nameof(step));
        var markers = ticks?.OrderBy(tick => tick.value).ToArray() ?? [];
        for (int i = 0; i < markers.Length; i++)
            if (!float.IsFinite(markers[i].value) || markers[i].value < minimum || markers[i].value > maximum
                || (i > 0 && markers[i].value == markers[i - 1].value))
                throw new ArgumentException("Tick values must be distinct finite values within the slider range.", nameof(ticks));

        //预留滑条和刻度区域
        vector2 origin = NativeEditorGUI.GetCursorScreenPos();
        float available = Math.Max(1, NativeEditorGUI.GetContentRegionAvail().x);
        float size = width > 0 ? Math.Min(width, available) : available;
        float height = markers.Length == 0 ? 28 : 54;
        NativeEditorGUI.InvisibleButton(id, new vector2(size, height));
        bool clicked = NativeEditorGUI.IsItemClicked();
        bool active = NativeEditorGUI.IsItemActive();
        bool hovered = NativeEditorGUI.IsItemHovered();
        if (!active || !NativeEditorGUI.IsMouseDown(0)) dragging = false;
        if (size <= 24) return false;
        vector2 clipMax = new(origin.x + size, origin.y + height);
        float left = origin.x + 12, right = origin.x + size - 12, y = origin.y + 13;
        double range = (double)maximum - minimum;
        double increment = step ?? (markers.Length == 0 ? range * 0.01 : 0);
        double trackWidth = right - left;
        vector2 mouse = NativeEditorGUI.GetMousePos();
        float previous = value;

        //点击刻度或滑轨并开始拖动
        if (clicked)
        {
            double fraction = Math.Clamp((mouse.x - left) / trackWidth, 0, 1);
            float candidate = (float)(minimum + fraction * range);
            double closestDistance = double.PositiveInfinity;
            int closest = -1;
            for (int i = 0; i < markers.Length; i++)
            {
                double x = left + ((double)markers[i].value - minimum) / range * trackWidth;
                double distance = Math.Abs(mouse.x - x);
                if (distance < closestDistance) { closestDistance = distance; closest = i; }
            }
            double thumbX = left + ((double)value - minimum) / range * trackWidth;
            pressedTick = closest >= 0 && (closestDistance <= 6 || mouse.y >= y + 8);
            if (pressedTick) candidate = markers[closest].value;
            else if (Math.Abs(mouse.x - thumbX) <= 9 && Math.Abs(mouse.y - y) <= 11) candidate = value;
            pressValue = candidate;
            pressMouseX = mouse.x;
            dragging = true;
        }

        //按步长拖动并保留刻度的精确值
        if (active && dragging)
        {
            double candidate = Math.Clamp(pressValue + (mouse.x - pressMouseX) / trackWidth * range, minimum, maximum);
            if (increment > 0 && candidate > minimum && candidate < maximum && !(pressedTick && mouse.x == pressMouseX))
                candidate = minimum + Math.Round((candidate - minimum) / increment, MidpointRounding.AwayFromZero) * increment;
            value = (float)Math.Clamp(candidate, minimum, maximum);
        }

        //绘制滑轨和已滑过的范围
        EditorTheme theme = EditorTheme.Current;
        float currentX = (float)(left + ((double)value - minimum) / range * trackWidth);
        Span<vector2> line = stackalloc vector2[2];
        line[0] = new(left, y);
        line[1] = new(right, y);
        NativeEditorGUI.DrawPolyline(line, theme.Control, 4, origin, clipMax);
        if (currentX > left)
        {
            line[1] = new(currentX, y);
            NativeEditorGUI.DrawPolyline(line, theme.Active, 4, origin, clipMax);
        }

        //绘制刻度和各自区域内的标签
        for (int i = 0; i < markers.Length; i++)
        {
            double fraction = ((double)markers[i].value - minimum) / range;
            float x = (float)(left + fraction * trackWidth);
            line[0] = new(x, y + 7);
            line[1] = new(x, y + 14);
            color tint = value == markers[i].value ? theme.Active : theme.Text;
            NativeEditorGUI.DrawPolyline(line, tint, 1.5f, origin, clipMax);
            float labelLeft = i == 0 ? origin.x
                : (float)(left + (((double)markers[i - 1].value + markers[i].value) * 0.5 - minimum) / range * trackWidth) + 2;
            float labelRight = i == markers.Length - 1 ? clipMax.x
                : (float)(left + (((double)markers[i].value + markers[i + 1].value) * 0.5 - minimum) / range * trackWidth) - 2;
            if (labelRight <= labelLeft) continue;
            string text = markers[i].label ?? "";
            float textWidth = text.Length * 6.5f;
            float textX = Math.Clamp(x - textWidth * 0.5f, labelLeft, Math.Max(labelLeft, labelRight - textWidth));
            NativeEditorGUI.DrawTextClipped(new(labelLeft, y + 17), new(labelRight, clipMax.y),
                new(textX, y + 19), tint, text, 13);
        }

        //绘制滑块
        color thumbColor = active || hovered ? theme.Text : theme.Active;
        thumb[0] = new EditorRectPrimitive
        {
            MinX = currentX - 5, MinY = y - 9, MaxX = currentX + 5, MaxY = y + 9,
            R = thumbColor.r, G = thumbColor.g, B = thumbColor.b, A = thumbColor.a, Rounding = 4
        };
        NativeEditorGUI.DrawRects(thumb, 1);
        return value != previous;
    }
}
