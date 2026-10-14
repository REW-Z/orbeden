using Orbeden;

namespace OrbedenEditor;

/// <summary>使用带刻度的环和两个端点编辑循环区间。</summary>
public sealed class EditorCircularRangeSlider
{
    private int draggedEndpoint = -1;
    private float dragOffset;

    /// <summary>按可选步长编辑 [0, 1) 内的循环区间，默认连续取值，修改时返回 true。</summary>
    public bool Draw(string id, ref float start, ref float end, IReadOnlyList<string> tickLabels, float diameter = 220f, float? step = null)
    {
        ArgumentException.ThrowIfNullOrEmpty(id);
        ArgumentNullException.ThrowIfNull(tickLabels);
        if (tickLabels.Count == 0) throw new ArgumentException("At least one tick is required.", nameof(tickLabels));
        if (!float.IsFinite(start) || start < 0 || start >= 1) throw new ArgumentOutOfRangeException(nameof(start));
        if (!float.IsFinite(end) || end < 0 || end >= 1) throw new ArgumentOutOfRangeException(nameof(end));
        if (!float.IsFinite(diameter) || diameter < 100) throw new ArgumentOutOfRangeException(nameof(diameter));
        if (step.HasValue && (!float.IsFinite(step.Value) || step.Value < 0)) throw new ArgumentOutOfRangeException(nameof(step));

        //预留环形画布
        vector2 available = NativeEditorGUI.GetContentRegionAvail();
        vector2 origin = NativeEditorGUI.GetCursorScreenPos();
        float width = Math.Max(1, available.x);
        float side = Math.Min(diameter, width);
        NativeEditorGUI.InvisibleButton(id, new vector2(width, side));
        bool clicked = NativeEditorGUI.IsItemClicked();
        bool active = NativeEditorGUI.IsItemActive();
        bool hovered = NativeEditorGUI.IsItemHovered();
        if (!active || !NativeEditorGUI.IsMouseDown(0)) draggedEndpoint = -1;
        if (side < 100) return false;
        vector2 clipMax = new(origin.x + width, origin.y + side);
        vector2 center = new(origin.x + width * 0.5f, origin.y + side * 0.5f);
        float radius = side * 0.5f - 34;
        const float firstAngle = MathF.PI * 0.75f;
        const float turn = MathF.PI * 2;
        vector2 mouse = NativeEditorGUI.GetMousePos();
        bool changed = false;

        //计算端点命中位置
        float a = firstAngle - turn * start, b = firstAngle - turn * end;
        float separation = Math.Min(Math.Abs(start - end), 1 - Math.Abs(start - end)) * turn * radius;
        bool close = separation < 26;
        float startRadius = radius - (close ? 9 : 0), endRadius = radius + (close ? 9 : 0);
        vector2 startPoint = new(center.x + MathF.Cos(a) * startRadius, center.y + MathF.Sin(a) * startRadius);
        vector2 endPoint = new(center.x + MathF.Cos(b) * endRadius, center.y + MathF.Sin(b) * endRadius);
        float distanceStart = MathF.Sqrt(MathF.Pow(mouse.x - startPoint.x, 2) + MathF.Pow(mouse.y - startPoint.y, 2));
        float distanceEnd = MathF.Sqrt(MathF.Pow(mouse.x - endPoint.x, 2) + MathF.Pow(mouse.y - endPoint.y, 2));
        if (clicked && Math.Min(distanceStart, distanceEnd) <= 13)
        {
            draggedEndpoint = distanceStart <= distanceEnd ? 0 : 1;
            float pointer = (firstAngle - MathF.Atan2(mouse.y - center.y, mouse.x - center.x)) / turn;
            dragOffset = (draggedEndpoint == 0 ? start : end) - pointer;
        }

        //拖动端点并跨越闭合接缝
        float mouseRadius = MathF.Sqrt(MathF.Pow(mouse.x - center.x, 2) + MathF.Pow(mouse.y - center.y, 2));
        if (active && draggedEndpoint >= 0 && mouseRadius > 4)
        {
            double parameter = (firstAngle - MathF.Atan2(mouse.y - center.y, mouse.x - center.x)) / turn + dragOffset;
            parameter -= Math.Floor(parameter);
            if (step > 0) parameter = Math.Round(parameter / step.Value, MidpointRounding.AwayFromZero) * step.Value;
            parameter -= Math.Floor(parameter);
            float value = (float)parameter;
            if (value >= 1) value = 0;
            if (draggedEndpoint == 0 && value != start) { start = value; changed = true; }
            if (draggedEndpoint == 1 && value != end) { end = value; changed = true; }
        }

        //绘制底环和选中弧
        EditorTheme theme = EditorTheme.Current;
        color accent = theme.Active;
        DrawArc(center, radius, 0, 1, theme.Control, 4, origin, clipMax);
        float length = end - start;
        if (length < 0) length += 1;
        if (length > 0) DrawArc(center, radius, start, start + length, accent, 6, origin, clipMax);

        //绘制刻度及标签
        Span<vector2> tick = stackalloc vector2[2];
        for (int i = 0; i < tickLabels.Count; i++)
        {
            float angle = firstAngle - turn * i / tickLabels.Count;
            float x = MathF.Cos(angle), y = MathF.Sin(angle);
            tick[0] = new(center.x + x * (radius - 5), center.y + y * (radius - 5));
            tick[1] = new(center.x + x * (radius + 7), center.y + y * (radius + 7));
            NativeEditorGUI.DrawPolyline(tick, theme.Text, 1.5f, origin, clipMax);
            string text = tickLabels[i] ?? "";
            vector2 position = new(center.x + x * (radius + 21) - text.Length * 3.25f,
                center.y + y * (radius + 21) - 6.5f);
            NativeEditorGUI.DrawTextClipped(origin, clipMax, position, theme.Text, text, 13);
        }

        //绘制两个端点和标记
        separation = Math.Min(Math.Abs(start - end), 1 - Math.Abs(start - end)) * turn * radius;
        for (int i = 0; i < 2; i++)
        {
            float value = i == 0 ? start : end;
            float angle = firstAngle - turn * value;
            float offset = separation < 26 ? (i == 0 ? -9 : 9) : 0;
            float x = MathF.Cos(angle), y = MathF.Sin(angle);
            vector2 point = new(center.x + x * (radius + offset), center.y + y * (radius + offset));
            bool hot = draggedEndpoint == i || (hovered && (i == 0 ? distanceStart : distanceEnd) <= 13);
            DrawArc(point, 4, 0, 1, theme.Background, 8, origin, clipMax);
            DrawArc(point, 8, 0, 1, hot ? theme.Text : accent, hot ? 3.5f : 2.5f, origin, clipMax);
            NativeEditorGUI.DrawTextClipped(origin, clipMax,
                new(center.x + x * (radius - 22 + offset) - 4, center.y + y * (radius - 22 + offset) - 6.5f),
                theme.Text, i == 0 ? "S" : "E", 13);
        }
        return changed;
    }

    //绘制按循环参数展开的圆弧
    private void DrawArc(vector2 center, float radius, float from, float to, color tint, float thickness,
        vector2 clipMin, vector2 clipMax)
    {
        int count = Math.Clamp((int)MathF.Ceiling(Math.Abs(to - from) * 128), 8, 128);
        Span<vector2> points = stackalloc vector2[count + 1];
        for (int i = 0; i <= count; i++)
        {
            float angle = MathF.PI * 0.75f - MathF.PI * 2 * (from + (to - from) * i / count);
            points[i] = new(center.x + MathF.Cos(angle) * radius, center.y + MathF.Sin(angle) * radius);
        }
        NativeEditorGUI.DrawPolyline(points, tint, thickness, clipMin, clipMax);
    }
}
