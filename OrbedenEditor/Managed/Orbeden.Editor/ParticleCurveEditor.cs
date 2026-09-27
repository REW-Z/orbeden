using Orbeden;

namespace OrbedenEditor;

/// <summary>曲线与渐变控件的返回结果。</summary>
internal enum ParticleCurveEditResult
{
    /// <summary>没有变化。</summary>
    None,
    /// <summary>正在拖动，只画临时结果，不写配置也不记脏。</summary>
    Preview,
    /// <summary>拖动结束或数值输入提交，生成一次事务。</summary>
    Commit,
    /// <summary>取消本次编辑。</summary>
    Cancel,
}

/// <summary>曲线与渐变的求值，与 C++ 侧 ParticleSettings.cpp 的公式保持一致，只用于编辑态显示。</summary>
internal static class ParticleCurveMath
{
    /// <summary>按归一化时间求曲线值，端点精确。</summary>
    internal static float EvaluateCurve(ParticleCurve curve, float t)
    {
        if (curve.keys == null || curve.keys.Length == 0) return 0.0f;
        if (curve.keys.Length == 1) return curve.keys[0].value;

        float time = Math.Clamp(t, 0.0f, 1.0f);
        if (time >= curve.keys[^1].time) return curve.keys[^1].value;
        if (time <= curve.keys[0].time) return curve.keys[0].value;

        //找到右端点，段模式取左 key 的插值方式
        int right = 1;
        while (right < curve.keys.Length && curve.keys[right].time <= time) ++right;
        ParticleCurveKey left = curve.keys[right - 1];
        ParticleCurveKey rightKey = curve.keys[right];

        float span = rightKey.time - left.time;
        if (span <= 0.0f) return left.value;
        float local = (time - left.time) / span;

        return left.interpolation switch
        {
            ParticleCurveInterpolation.Constant => left.value,
            ParticleCurveInterpolation.Cubic => Cubic(left, rightKey, local, span),
            _ => left.value + (rightKey.value - left.value) * local,
        };
    }

    private static float Cubic(ParticleCurveKey left, ParticleCurveKey right, float local, float span)
    {
        float local2 = local * local;
        float local3 = local2 * local;
        float h00 = 2.0f * local3 - 3.0f * local2 + 1.0f;
        float h10 = local3 - 2.0f * local2 + local;
        float h01 = -2.0f * local3 + 3.0f * local2;
        float h11 = local3 - local2;
        return h00 * left.value + h10 * span * left.outTangent + h01 * right.value + h11 * span * right.inTangent;
    }

    /// <summary>按归一化时间求渐变颜色，RGB 先转线性再插值。</summary>
    internal static color EvaluateGradient(ParticleGradient gradient, float t)
    {
        color fallback = new() { r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f };
        if (gradient.keys == null || gradient.keys.Length == 0) return fallback;
        if (gradient.keys.Length == 1) return ToLinear(gradient.keys[0].value);

        float time = Math.Clamp(t, 0.0f, 1.0f);
        if (time <= gradient.keys[0].time) return ToLinear(gradient.keys[0].value);
        if (time >= gradient.keys[^1].time) return ToLinear(gradient.keys[^1].value);

        int right = 1;
        while (right < gradient.keys.Length && gradient.keys[right].time <= time) ++right;
        ParticleGradientKey left = gradient.keys[right - 1];
        ParticleGradientKey rightKey = gradient.keys[right];

        float span = rightKey.time - left.time;
        if (span <= 0.0f) return ToLinear(left.value);
        float local = (time - left.time) / span;

        color leftLinear = ToLinear(left.value);
        color rightLinear = ToLinear(rightKey.value);
        return new color
        {
            r = leftLinear.r + (rightLinear.r - leftLinear.r) * local,
            g = leftLinear.g + (rightLinear.g - leftLinear.g) * local,
            b = leftLinear.b + (rightLinear.b - leftLinear.b) * local,
            a = left.value.a + (rightKey.value.a - left.value.a) * local,
        };
    }

    private static color ToLinear(color value)
    {
        return new color
        {
            r = SrgbToLinear(value.r),
            g = SrgbToLinear(value.g),
            b = SrgbToLinear(value.b),
            a = value.a,
        };
    }

    private static float SrgbToLinear(float value)
    {
        float clamped = Math.Clamp(value, 0.0f, 1.0f);
        return clamped <= 0.04045f ? clamped / 12.92f : MathF.Pow((clamped + 0.055f) / 1.055f, 2.4f);
    }
}

/// <summary>归一化时间到取值的曲线画布。</summary>
internal static class ParticleCurveEditor
{
    private const int CanvasHeight = 180;
    private const float CanvasPadding = 12.0f;
    //key 标记的边长与选中半径
    private const float KeyMarkerSize = 6.0f;
    private const float KeyPickRadius = 8.0f;
    //拖动手柄时的归一化时间距离
    private const float TangentHandleSpan = 0.08f;

    /// <summary>拖动中的状态。目标或源文本变化时整体作废。</summary>
    private sealed class DragState
    {
        internal string FieldPath = string.Empty;
        internal int TargetIdentity;
        internal int KeyIndex = -1;
        //0 键体、1 入切线、2 出切线
        internal int Handle;
        internal ParticleCurve Start = new();
        internal ParticleCurve Working = new();
        internal bool Canceled;
    }

    private static DragState? drag;

    /// <summary>绘制曲线控件；拖动期间返回 Preview，释放时返回 Commit。</summary>
    internal static ParticleCurveEditResult Draw(string id, ParticleCurve value, bool mixed, out ParticleCurve edited)
    {
        edited = value;

        //混合选中时不给编辑入口，避免用第一个组件的完整曲线覆盖其它组件
        if (mixed)
        {
            EditorGUI.Label("多个值");
            return ParticleCurveEditResult.None;
        }

        EditorGUI.PushId(id);
        try
        {
            vector2 canvasMin = NativeEditorGUI.GetCursorScreenPos();
            float width = MathF.Max(240.0f, NativeEditorGUI.GetContentRegionAvail().x);
            NativeEditorGUI.InvisibleButton(id + "-canvas", new vector2 { x = width, y = CanvasHeight });
            bool hovered = NativeEditorGUI.IsItemHovered();
            bool active = NativeEditorGUI.IsItemActive();

            vector2 mouse = NativeEditorGUI.GetMousePos();
            (float minimum, float maximum) = ComputeRange(value);

            ParticleCurveEditResult result = ParticleCurveEditResult.None;
            ParticleCurve working = drag != null && drag.FieldPath == id ? drag.Working : value;

            //拖动开始
            if (hovered && NativeEditorGUI.IsMouseDown(0) && drag == null)
            {
                int picked = PickKey(working, canvasMin, width, minimum, maximum, mouse);
                if (picked >= 0)
                {
                    drag = new DragState
                    {
                        FieldPath = id,
                        KeyIndex = picked,
                        Handle = 0,
                        Start = Clone(value),
                        Working = Clone(working),
                    };
                }
            }

            //拖动中：只改工作副本
            if (drag != null && drag.FieldPath == id)
            {
                if (NativeEditorGUI.IsMouseDown(0))
                {
                    if (drag.Handle == 0) MoveKey(drag, canvasMin, width, minimum, maximum, mouse);
                    else MoveTangent(drag, canvasMin, width, minimum, maximum, mouse);
                    result = ParticleCurveEditResult.Preview;
                    working = drag.Working;
                }
                else
                {
                    //释放：有实际变化才提交
                    working = drag.Working;
                    result = drag.Canceled ? ParticleCurveEditResult.Cancel : ParticleCurveEditResult.Commit;
                    edited = working;
                    drag = null;
                    DrawCanvas(id + "-canvas", canvasMin, width, working, minimum, maximum, -1);
                    DrawKeyFields(id, ref working, ref result, ref edited);
                    EditorGUI.PopId();
                    return result;
                }
            }

            //双击空白新增关键帧
            if (hovered && NativeEditorGUI.IsItemDoubleClicked() && drag == null)
            {
                if (value.keys.Length < 64)
                {
                    working = Clone(value);
                    float time = Math.Clamp((mouse.x - canvasMin.x - CanvasPadding) / MathF.Max(1.0f, width - CanvasPadding * 2.0f), 0.0f, 1.0f);
                    float frameValue = ValueFromPixel(mouse.y, canvasMin.y, minimum, maximum);
                    ParticleCurveKey added = new()
                    {
                        time = time,
                        value = frameValue,
                        inTangent = 0.0f,
                        outTangent = 0.0f,
                        interpolation = ParticleCurveInterpolation.Linear,
                    };
                    working.keys = [.. value.keys, added];
                    working.keys = [.. working.keys.OrderBy(key => key.time)];
                    edited = working;
                    result = ParticleCurveEditResult.Commit;
                }
            }

            DrawCanvas(id + "-canvas", canvasMin, width, working, minimum, maximum,
                result == ParticleCurveEditResult.Preview || drag == null ? -1 : drag.KeyIndex);
            DrawKeyFields(id, ref working, ref result, ref edited);
            EditorGUI.PopId();
            return result;
        }
        catch
        {
            drag = null;
            EditorGUI.PopId();
            throw;
        }
    }

    private static ParticleCurve Clone(ParticleCurve value)
    {
        return new ParticleCurve { keys = [.. value.keys] };
    }

    //画布纵轴范围：曲线值加 129 个采样点，再上下留一成余量
    private static (float Minimum, float Maximum) ComputeRange(ParticleCurve curve)
    {
        float minimum = float.MaxValue;
        float maximum = float.MinValue;
        foreach (ParticleCurveKey key in curve.keys)
        {
            minimum = MathF.Min(minimum, key.value);
            maximum = MathF.Max(maximum, key.value);
        }

        for (int index = 0; index <= 128; ++index)
        {
            float sample = ParticleCurveMath.EvaluateCurve(curve, index / 128.0f);
            minimum = MathF.Min(minimum, sample);
            maximum = MathF.Max(maximum, sample);
        }

        if (minimum > maximum) { minimum = 0.0f; maximum = 1.0f; }
        float span = maximum - minimum;
        if (span < 1.0e-4f)
        {
            float center = (minimum + maximum) * 0.5f;
            minimum = center - 0.5f;
            maximum = center + 0.5f;
        }
        else
        {
            minimum -= span * 0.1f;
            maximum += span * 0.1f;
        }

        return (minimum, maximum);
    }

    private static float ValueFromPixel(float pixelY, float canvasTop, float minimum, float maximum)
    {
        float top = canvasTop + CanvasPadding;
        float height = CanvasHeight - CanvasPadding * 2.0f;
        float ratio = Math.Clamp(1.0f - (pixelY - top) / MathF.Max(1.0f, height), 0.0f, 1.0f);
        return minimum + (maximum - minimum) * ratio;
    }

    private static float TimeFromPixel(float pixelX, float canvasLeft, float width)
    {
        float left = canvasLeft + CanvasPadding;
        float usable = MathF.Max(1.0f, width - CanvasPadding * 2.0f);
        return Math.Clamp((pixelX - left) / usable, 0.0f, 1.0f);
    }

    private static int PickKey(ParticleCurve curve, vector2 canvasMin, float width, float minimum, float maximum, vector2 mouse)
    {
        int picked = -1;
        float bestDistance = KeyPickRadius;
        for (int index = 0; index < curve.keys.Length; ++index)
        {
            float x = canvasMin.x + CanvasPadding + curve.keys[index].time * MathF.Max(1.0f, width - CanvasPadding * 2.0f);
            float y = canvasMin.y + CanvasPadding + (1.0f - (curve.keys[index].value - minimum) / MathF.Max(1.0e-6f, maximum - minimum)) * (CanvasHeight - CanvasPadding * 2.0f);
            float distance = MathF.Sqrt((x - mouse.x) * (x - mouse.x) + (y - mouse.y) * (y - mouse.y));
            if (distance > bestDistance) continue;
            bestDistance = distance;
            picked = index;
        }

        return picked;
    }

    private static void MoveKey(DragState state, vector2 canvasMin, float width, float minimum, float maximum, vector2 mouse)
    {
        int index = state.KeyIndex;
        if (index < 0 || index >= state.Working.keys.Length) return;

        ParticleCurveKey[] keys = state.Working.keys;
        float time = TimeFromPixel(mouse.x, canvasMin.x, width);

        //首尾 key 的时间锁在 0 与 1，内部 key 被邻居夹住
        if (index == 0) time = 0.0f;
        else if (index == keys.Length - 1) time = 1.0f;
        else time = Math.Clamp(time, keys[index - 1].time + 1.0e-4f, keys[index + 1].time - 1.0e-4f);

        keys[index].time = time;
        keys[index].value = ValueFromPixel(mouse.y, canvasMin.y, minimum, maximum);
    }

    private static void MoveTangent(DragState state, vector2 canvasMin, float width, float minimum, float maximum, vector2 mouse)
    {
        int index = state.KeyIndex;
        if (index < 0 || index >= state.Working.keys.Length) return;

        ParticleCurveKey[] keys = state.Working.keys;
        float span = CanvasHeight - CanvasPadding * 2.0f;
        float scale = MathF.Max(1.0e-6f, maximum - minimum);
        float value = ValueFromPixel(mouse.y, canvasMin.y, minimum, maximum);

        //斜率按固定归一化时间跨度折算，避免灵敏度随控件宽度变化
        float slope = (value - keys[index].value) / TangentHandleSpan;
        if (state.Handle == 1) keys[index].inTangent = slope;
        else keys[index].outTangent = slope;
    }

    private static void DrawCanvas(string id, vector2 canvasMin, float width, ParticleCurve curve,
        float minimum, float maximum, int selected)
    {
        EditorRectPrimitive[] rects = new EditorRectPrimitive[256];
        int count = 0;

        float left = canvasMin.x + CanvasPadding;
        float top = canvasMin.y + CanvasPadding;
        float usableWidth = MathF.Max(1.0f, width - CanvasPadding * 2.0f);
        float usableHeight = CanvasHeight - CanvasPadding * 2.0f;

        //背景与边框
        count = EditorRects.Append(rects, count, canvasMin.x, canvasMin.y, canvasMin.x + width, canvasMin.y + CanvasHeight,
            new color { r = 0.13f, g = 0.13f, b = 0.15f, a = 1.0f });
        //0、0.25、0.5、0.75、1 五条竖网格
        for (int index = 0; index <= 4; ++index)
        {
            float x = left + usableWidth * index / 4.0f;
            count = EditorRects.Append(rects, count, x - 0.5f, top, x + 0.5f, top + usableHeight,
                new color { r = 0.25f, g = 0.25f, b = 0.28f, a = 1.0f });
        }
        //五条水平网格
        for (int index = 0; index <= 4; ++index)
        {
            float y = top + usableHeight * index / 4.0f;
            count = EditorRects.Append(rects, count, left, y - 0.5f, left + usableWidth, y + 0.5f,
                new color { r = 0.25f, g = 0.25f, b = 0.28f, a = 1.0f });
        }

        NativeEditorGUI.DrawRects(rects, count);

        //129 点折线
        if (curve.keys.Length >= 2)
        {
            vector2[] points = new vector2[129];
            float scale = MathF.Max(1.0e-6f, maximum - minimum);
            for (int index = 0; index <= 128; ++index)
            {
                float time = index / 128.0f;
                float sample = ParticleCurveMath.EvaluateCurve(curve, time);
                points[index] = new vector2
                {
                    x = left + usableWidth * time,
                    y = top + (1.0f - (sample - minimum) / scale) * usableHeight,
                };
            }

            NativeEditorGUI.DrawPolyline(points, new color { r = 0.45f, g = 0.78f, b = 1.0f, a = 1.0f }, 1.6f,
                canvasMin, new vector2 { x = canvasMin.x + width, y = canvasMin.y + CanvasHeight });
        }

        //key 标记
        EditorRectPrimitive[] markers = new EditorRectPrimitive[64];
        int markerCount = 0;
        float valueScale = MathF.Max(1.0e-6f, maximum - minimum);
        for (int index = 0; index < curve.keys.Length; ++index)
        {
            float x = left + usableWidth * curve.keys[index].time;
            float y = top + (1.0f - (curve.keys[index].value - minimum) / valueScale) * usableHeight;
            color tint = index == selected
                ? new color { r = 1.0f, g = 0.72f, b = 0.25f, a = 1.0f }
                : new color { r = 0.9f, g = 0.9f, b = 0.95f, a = 1.0f };
            markerCount = EditorRects.Append(markers, markerCount, x - KeyMarkerSize * 0.5f, y - KeyMarkerSize * 0.5f,
                x + KeyMarkerSize * 0.5f, y + KeyMarkerSize * 0.5f, tint);
        }

        if (markerCount > 0) NativeEditorGUI.DrawRects(markers, markerCount);
    }

    //数值输入：任一字段变化就生成一次事务
    private static void DrawKeyFields(string id, ref ParticleCurve working, ref ParticleCurveEditResult result, ref ParticleCurve edited)
    {
        if (working.keys.Length == 0) return;

        int index = drag?.KeyIndex ?? -1;
        if (index < 0 || index >= working.keys.Length) index = 0;

        ParticleCurveKey[] keys = working.keys;
        ParticleCurveKey key = keys[index];
        bool changed = false;

        float time = key.time;
        if (EditorGUI.InputFloat($"{id}-time", ref time)) { key.time = Math.Clamp(time, 0.0f, 1.0f); changed = true; }
        EditorGUI.SameLine();
        float value = key.value;
        if (EditorGUI.InputFloat($"{id}-value", ref value)) { key.value = value; changed = true; }

        float inTangent = key.inTangent;
        if (EditorGUI.InputFloat($"{id}-in", ref inTangent)) { key.inTangent = inTangent; changed = true; }
        EditorGUI.SameLine();
        float outTangent = key.outTangent;
        if (EditorGUI.InputFloat($"{id}-out", ref outTangent)) { key.outTangent = outTangent; changed = true; }

        string interpolation = key.interpolation.ToString();
        if (EditorGUI.BeginCombo($"{id}-mode", interpolation))
        {
            foreach (ParticleCurveInterpolation option in Enum.GetValues<ParticleCurveInterpolation>())
            {
                if (!EditorGUI.Selectable(option.ToString(), option == key.interpolation)) continue;
                key.interpolation = option;
                changed = true;
            }
            EditorGUI.EndCombo();
        }

        bool removeRequested = false;
        if (index > 0 && index < keys.Length - 1)
        {
            EditorGUI.SameLine();
            removeRequested = EditorGUI.Button("删除");
        }

        if (!changed && !removeRequested) return;

        //首尾 key 的时间不可改，内部 key 仍然夹在邻居之间
        if (index == 0) key.time = 0.0f;
        else if (index == keys.Length - 1) key.time = 1.0f;

        List<ParticleCurveKey> updated = [.. keys];
        if (removeRequested) updated.RemoveAt(index);
        else updated[index] = key;
        updated.Sort((left, right) => left.time.CompareTo(right.time));

        edited = new ParticleCurve { keys = [.. updated] };
        result = ParticleCurveEditResult.Commit;
    }
}

/// <summary>归一化时间到颜色的渐变画布。</summary>
internal static class ParticleGradientEditor
{
    private const int CanvasHeight = 96;
    private const float MarkerSize = 8.0f;
    private const int PreviewSteps = 128;

    private sealed class GradientDragState
    {
        internal string FieldPath = string.Empty;
        internal int KeyIndex = -1;
        internal ParticleGradient Working = new();
        internal bool Canceled;
    }

    private static GradientDragState? drag;

    /// <summary>绘制渐变控件；拖动期间返回 Preview，释放时返回 Commit。</summary>
    internal static ParticleCurveEditResult Draw(string id, ParticleGradient value, bool mixed, out ParticleGradient edited)
    {
        edited = value;
        if (mixed)
        {
            EditorGUI.Label("多个值");
            return ParticleCurveEditResult.None;
        }

        EditorGUI.PushId(id);
        try
        {
            vector2 canvasMin = NativeEditorGUI.GetCursorScreenPos();
            float width = MathF.Max(240.0f, NativeEditorGUI.GetContentRegionAvail().x);
            NativeEditorGUI.InvisibleButton(id + "-bar", new vector2 { x = width, y = CanvasHeight / 2.0f });
            bool hovered = NativeEditorGUI.IsItemHovered();

            vector2 mouse = NativeEditorGUI.GetMousePos();
            ParticleCurveEditResult result = ParticleCurveEditResult.None;
            ParticleGradient working = drag != null && drag.FieldPath == id ? drag.Working : value;

            if (hovered && NativeEditorGUI.IsMouseDown(0) && drag == null)
            {
                int picked = PickKey(working, canvasMin.x, width, mouse.x);
                if (picked >= 0)
                {
                    drag = new GradientDragState { FieldPath = id, KeyIndex = picked, Working = Clone(working) };
                }
            }

            if (drag != null && drag.FieldPath == id)
            {
                if (NativeEditorGUI.IsMouseDown(0))
                {
                    int index = drag.KeyIndex;
                    if (index > 0 && index < drag.Working.keys.Length - 1)
                    {
                        float time = (mouse.x - canvasMin.x) / MathF.Max(1.0f, width);
                        drag.Working.keys[index].time = Math.Clamp(time,
                            drag.Working.keys[index - 1].time + 1.0e-4f, drag.Working.keys[index + 1].time - 1.0e-4f);
                    }
                    result = ParticleCurveEditResult.Preview;
                    working = drag.Working;
                }
                else
                {
                    working = drag.Working;
                    edited = working;
                    result = drag.Canceled ? ParticleCurveEditResult.Cancel : ParticleCurveEditResult.Commit;
                    drag = null;
                    DrawBar(id + "-bar", canvasMin.x, width, working, -1);
                    EditorGUI.PopId();
                    return result;
                }
            }

            if (hovered && NativeEditorGUI.IsItemDoubleClicked() && drag == null && value.keys.Length < 64)
            {
                float time = Math.Clamp((mouse.x - canvasMin.x) / MathF.Max(1.0f, width), 0.0f, 1.0f);
                color sampled = ParticleCurveMath.EvaluateGradient(value, time);
                //反算时要回到配置空间的 sRGB 表示
                ParticleGradientKey added = new()
                {
                    time = time,
                    value = new color
                    {
                        r = LinearToSrgb(sampled.r),
                        g = LinearToSrgb(sampled.g),
                        b = LinearToSrgb(sampled.b),
                        a = sampled.a,
                    },
                };
                List<ParticleGradientKey> keys = [.. value.keys, added];
                keys.Sort((left, right) => left.time.CompareTo(right.time));
                edited = new ParticleGradient { keys = [.. keys] };
                result = ParticleCurveEditResult.Commit;
            }

            DrawBar(id + "-bar", canvasMin.x, width, working, drag?.KeyIndex ?? -1);
            EditorGUI.PopId();
            return result;
        }
        catch
        {
            drag = null;
            EditorGUI.PopId();
            throw;
        }
    }

    private static ParticleGradient Clone(ParticleGradient value)
    {
        return new ParticleGradient { keys = [.. value.keys] };
    }

    private static float LinearToSrgb(float value)
    {
        float clamped = Math.Clamp(value, 0.0f, 1.0f);
        return clamped <= 0.0031308f ? clamped * 12.92f : 1.055f * MathF.Pow(clamped, 1.0f / 2.4f) - 0.055f;
    }

    private static int PickKey(ParticleGradient gradient, float canvasLeft, float width, float mouseX)
    {
        int picked = -1;
        float best = MarkerSize;
        for (int index = 0; index < gradient.keys.Length; ++index)
        {
            float x = canvasLeft + width * gradient.keys[index].time;
            float distance = MathF.Abs(x - mouseX);
            if (distance > best) continue;
            best = distance;
            picked = index;
        }

        return picked;
    }

    private static void DrawBar(string id, float canvasLeft, float width, ParticleGradient gradient, int selected)
    {
        EditorRectPrimitive[] rects = new EditorRectPrimitive[PreviewSteps + 64];
        int count = 0;
        float barHeight = CanvasHeight / 2.0f;
        float step = width / PreviewSteps;

        //渐变条本身按显示空间编码后再画，线性值直接当显示色会整体偏暗
        for (int index = 0; index < PreviewSteps; ++index)
        {
            float time = (index + 0.5f) / PreviewSteps;
            color linearColor = ParticleCurveMath.EvaluateGradient(gradient, time);
            color display = new()
            {
                r = LinearToSrgb(linearColor.r),
                g = LinearToSrgb(linearColor.g),
                b = LinearToSrgb(linearColor.b),
                a = 1.0f,
            };
            count = EditorRects.Append(rects, count, canvasLeft + step * index, barHeight + 6.0f,
                canvasLeft + step * (index + 1) + 0.5f, barHeight + 6.0f + barHeight * 0.5f, display);
        }

        //下方 key 标记
        for (int index = 0; index < gradient.keys.Length; ++index)
        {
            float x = canvasLeft + width * gradient.keys[index].time;
            color tint = index == selected
                ? new color { r = 1.0f, g = 0.72f, b = 0.25f, a = 1.0f }
                : new color { r = 0.9f, g = 0.9f, b = 0.95f, a = 1.0f };
            count = EditorRects.Append(rects, count, x - MarkerSize * 0.5f, barHeight * 1.5f + 6.0f,
                x + MarkerSize * 0.5f, barHeight * 1.5f + 6.0f + MarkerSize, tint);
        }

        NativeEditorGUI.DrawRects(rects, count);
    }
}
