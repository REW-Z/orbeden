using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct RuntimeGuiCurveApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, vector2*, int, color*, vector2*, int, vector2*, int, int, float, float, float, vector2*, float*, uint*, uint> Canvas;
    public delegate* unmanaged[Cdecl]<byte*, int, color*, byte> ColorField;
}

/// <summary>共享的归一化动画曲线与颜色渐变控件。</summary>
public static unsafe partial class GUI
{

    private static RuntimeGuiCurveApi curveApi;
    private static readonly Dictionary<uint, int> curveSelections = [];
    //每个画布自己的视图值域与右键菜单目标
    private static readonly Dictionary<uint, (float Minimum, float Maximum)> curveViews = [];
    private static readonly Dictionary<uint, int> curveMenuTargets = [];
    //滚轮要到画布调用之后才读得到，而值域是画布的入参：留下的滚轮与锚点给下一帧消费
    private static readonly Dictionary<uint, (float Delta, float Anchor)> curveWheels = [];
    private static uint draggedCurve;
    private static ParticleCurve curveDragStart;
    private static ParticleCurve curveDragValue;
    private static ParticleGradient gradientDragStart;
    private static ParticleGradient gradientDragValue;
    private static bool draggingGradient;
    //正在拖的切线手柄：-1 表示拖的是关键帧本身
    private static int handleDrag = -1;
    private static float dragMinimum;
    private static float dragMaximum;

    /// <summary>初始化曲线画布与颜色字段原语。</summary>
    internal static void InitializeCurveApi(RuntimeGuiCurveApi value)
    {
        curveApi = value;
        curveSelections.Clear();
        curveViews.Clear();
        curveMenuTargets.Clear();
        curveWheels.Clear();
        draggedCurve = 0;
        handleDrag = -1;
    }

    //结束一次拖动预览
    private static void ResetCurveDrag()
    {
        draggedCurve = 0;
        handleDrag = -1;
    }

    /// <summary>绘制 RGBA 颜色选择器。</summary>
    public static bool ColorField(string label, ref color value)
    {
        if (curveApi.ColorField == null) return false;
        byte[] bytes = InteropText.EncodeUtf8(label);
        fixed (byte* text = bytes)
        fixed (color* pointer = &value)
            return curveApi.ColorField(text, bytes.Length, pointer) != 0;
    }

    /// <summary>编辑归一化时间曲线；拖动释放后才修改值并返回 true，id 须在当前 GUI 作用域内唯一。
    /// 值域区间由调用方给出：视图不越出这个区间，编辑也夹在里面。</summary>
    public static bool AnimationCurve(string label, string id, ref ParticleCurve value,
        float minimumValue = float.MinValue, float maximumValue = float.MaxValue)
    {
        if (curveApi.Canvas == null) return false;
        Label(label);
        ParticleCurve source = value.keys is { Length: > 0 } ? value : new ParticleCurve
        {
            keys = [new() { time = 0, value = 1 }, new() { time = 1, value = 1 }],
        };
        vector2[] samples = new vector2[129];
        float minimum = float.MaxValue, maximum = float.MinValue;
        for (int i = 0; i < samples.Length; ++i)
        {
            float time = i / 128.0f;
            float sample = CurveMath.EvaluateCurve(source, time);
            samples[i] = new vector2(time, sample);
            minimum = MathF.Min(minimum, sample);
            maximum = MathF.Max(maximum, sample);
        }
        //初视图按数据拟合，留一成余量；之后由滚轮缩放，"Fit View" 再回到这里
        float padding = MathF.Max(0.5f, (maximum - minimum) * 0.1f);
        return DrawAnimationCurve(id, ref value, source, samples, minimum - padding, maximum + padding, minimumValue, maximumValue);
    }

    /// <summary>处理曲线选择、拖动与关键帧输入。</summary>
    private static bool DrawAnimationCurve(string id, ref ParticleCurve value, ParticleCurve source,
        vector2[] samples, float fitMinimum, float fitMaximum, float minimumValue, float maximumValue)
    {
        //读取画布作用域 ID
        uint identity = 0;
        vector2 mouse = default;
        float wheel = 0.0f;
        byte[] bytes = InteropText.EncodeUtf8(id);
        fixed (byte* text = bytes)
            curveApi.Canvas(text, bytes.Length, null, 0, null, null, 0, null, 0, -1, 0.0f, 0.0f, 0, &mouse, &wheel, &identity);
        if (draggedCurve == identity && (draggingGradient || !source.keys.SequenceEqual(curveDragStart.keys))) ResetCurveDrag();
        bool dragging = draggedCurve == identity;
        ParticleCurve working = dragging ? curveDragValue : source;
        (float minimum, float maximum) = ResolveCurveView(identity, fitMinimum, fitMaximum, minimumValue, maximumValue);
        //拖动期间视图冻结：手指下的数值不能跟着鼠标跑
        if (dragging) { minimum = dragMinimum; maximum = dragMaximum; }
        int selected = Math.Clamp(curveSelections.GetValueOrDefault(identity), 0, working.keys.Length - 1);
        for (int i = 0; i < samples.Length; ++i)
            samples[i].y = (CurveMath.EvaluateCurve(working, samples[i].x) - minimum) / (maximum - minimum);
        vector2[] keys = working.keys.Select(key => new vector2(key.time, (key.value - minimum) / (maximum - minimum))).ToArray();
        vector2[] handles = new vector2[2];
        int handleMask = FillCurveHandles(working, selected, handles);
        uint input;
        fixed (byte* text = bytes)
        fixed (vector2* points = samples)
        fixed (vector2* markers = keys)
        fixed (vector2* tangentHandles = handles)
            input = curveApi.Canvas(text, bytes.Length, points, samples.Length, null, markers, keys.Length,
                tangentHandles, handleMask, selected, minimum, maximum, 160, &mouse, &wheel, &identity);
        if (wheel != 0.0f) curveWheels[identity] = (wheel, mouse.y);
        bool changed = false;
        //右键菜单：点在帧上是帧的操作，点在空白处是视图操作。
        //目标只在弹窗打开那一帧记下来，之后鼠标移到菜单上也不会变
        if ((input & 32) != 0) curveMenuTargets[identity] = PickCurveKey(keys, mouse);
        if (BeginPopupContextItem($"{id}##menu"))
        {
            try { changed |= DrawCurveMenu(identity, ref working, ref selected, curveMenuTargets.GetValueOrDefault(identity, -1)); }
            finally { EndPopup(); }
        }
        //更新拖动工作副本：手柄的判定在原生侧（手柄长度是像素定长的），关键帧在这里认
        if ((input & 2) != 0)
        {
            int pressedHandle = (input & 128) != 0 ? 0 : (input & 256) != 0 ? 1 : -1;
            int picked = pressedHandle >= 0 ? -1 : PickCurveKey(keys, mouse);
            if (pressedHandle >= 0 || picked >= 0)
            {
                if (picked >= 0) selected = picked;
                draggedCurve = identity;
                draggingGradient = false;
                handleDrag = pressedHandle;
                curveDragStart = new() { keys = [.. source.keys] };
                curveDragValue = new() { keys = [.. source.keys] };
                dragMinimum = minimum;
                dragMaximum = maximum;
            }
            else if ((input & 4) != 0 && source.keys.Length < 64)
            {
                working = new() { keys = [.. source.keys] };
                changed = InsertCurveKey(ref working, mouse.x,
                    Math.Clamp(minimum + mouse.y * (maximum - minimum), minimumValue, maximumValue), out selected);
            }
        }
        if (draggedCurve == identity)
        {
            if ((input & 16) != 0) ResetCurveDrag();
            else if ((input & 8) != 0)
            {
                if (handleDrag >= 0) ApplyHandleDrag(curveDragValue, handleDrag, selected, mouse, dragMinimum, dragMaximum);
                else
                {
                    ParticleCurveKey key = curveDragValue.keys[selected];
                    key.time = ClampKeyTime(curveDragValue.keys.Select(k => k.time).ToArray(), selected, mouse.x);
                    key.value = Math.Clamp(dragMinimum + mouse.y * (dragMaximum - dragMinimum), minimumValue, maximumValue);
                    curveDragValue.keys[selected] = key;
                }
            }
            else
            {
                working = curveDragValue;
                changed = !working.keys.SequenceEqual(curveDragStart.keys);
                ResetCurveDrag();
            }
        }
        if ((input & 64) != 0 && draggedCurve != identity) changed |= RemoveCurveKey(ref working, ref selected);
        //编辑选中关键帧
        BeginDisabled(draggedCurve == identity);
        try
        {
            selected = SelectCurveKey(id, selected, working.keys.Select(k => k.time).ToArray());
            ParticleCurveKey key = working.keys[selected];
            bool keyChanged = false;
            bool interpolationChanged = false;
            float time = key.time;
            if (InputFloat($"Time##{id}", ref time) && float.IsFinite(time)) { key.time = ClampKeyTime(working.keys.Select(k => k.time).ToArray(), selected, time); keyChanged = true; }
            float number = key.value;
            if (InputFloat($"Value##{id}", ref number) && float.IsFinite(number)) { key.value = Math.Clamp(number, minimumValue, maximumValue); keyChanged = true; }
            if (BeginCombo($"Interpolation##{id}", key.interpolation.ToString()))
            {
                foreach (ParticleCurveInterpolation option in Enum.GetValues<ParticleCurveInterpolation>())
                    if (Selectable(option.ToString(), option == key.interpolation))
                    {
                        interpolationChanged = option != key.interpolation;
                        key.interpolation = option;
                        keyChanged = true;
                    }
                EndCombo();
            }
            //切线只画参与求值的那一侧：首帧没有入切线，末帧没有出切线
            if (selected > 0)
            {
                float tangent = key.inTangent;
                if (InputFloat($"In Tangent##{id}", ref tangent) && float.IsFinite(tangent)) { key.inTangent = ClampTangent(tangent); keyChanged = true; }
            }
            if (selected < working.keys.Length - 1)
            {
                float tangent = key.outTangent;
                if (InputFloat($"Out Tangent##{id}", ref tangent) && float.IsFinite(tangent)) { key.outTangent = ClampTangent(tangent); keyChanged = true; }
            }
            if (keyChanged) { working.keys = [.. working.keys]; working.keys[selected] = key; changed = true; }
            //刚切到三次插值又没有斜率时按邻居补一条，免得看上去还是折线
            if (interpolationChanged && working.keys[selected].interpolation == ParticleCurveInterpolation.Cubic)
            {
                ApplyAutoTangent(ref working, selected);
                changed = true;
            }
            if (working.keys.Length < 64 && selected < working.keys.Length - 1 && Button($"Add Key After Selected##{id}"))
            {
                float midpoint = (working.keys[selected].time + working.keys[selected + 1].time) * 0.5f;
                changed |= InsertCurveKey(ref working, midpoint, CurveMath.EvaluateCurve(working, midpoint), out selected);
            }
            //端点删不掉，但按钮照画：禁用状态本身就是"这里有删除功能"的提示
            bool deletable = selected > 0 && selected < working.keys.Length - 1;
            BeginDisabled(!deletable);
            try
            {
                if (Button($"Delete Selected Key##{id}")) changed |= RemoveCurveKey(ref working, ref selected);
            }
            finally { EndDisabled(); }
        }
        finally { EndDisabled(); }
        curveSelections[identity] = selected;
        if (!changed || working.keys.SequenceEqual(source.keys)) return false;
        value = working;
        return true;
    }

    /// <summary>编辑归一化时间的颜色与透明度渐变。</summary>
    public static bool ColorGradient(string label, string id, ref ParticleGradient value)
    {
        if (curveApi.Canvas == null) return false;
        Label(label);
        ParticleGradient source = value.keys is { Length: > 0 } ? value : new()
        {
            keys = [new() { time = 0, value = new color { r = 1, g = 1, b = 1, a = 1 } }, new() { time = 1, value = new color { r = 1, g = 1, b = 1, a = 1 } }],
        };
        byte[] bytes = InteropText.EncodeUtf8(id);
        uint identity = 0;
        vector2 mouse = default;
        float wheel = 0.0f;
        fixed (byte* text = bytes)
            curveApi.Canvas(text, bytes.Length, null, 0, null, null, 0, null, 0, -1, 0.0f, 0.0f, 0, &mouse, &wheel, &identity);
        if (draggedCurve == identity && (!draggingGradient || !source.keys.SequenceEqual(gradientDragStart.keys))) ResetCurveDrag();
        ParticleGradient working = draggedCurve == identity ? gradientDragValue : source;
        int selected = Math.Clamp(curveSelections.GetValueOrDefault(identity), 0, working.keys.Length - 1);
        color[] samples = new color[129];
        for (int i = 0; i < samples.Length; ++i)
        {
            color sample = CurveMath.EvaluateGradient(working, i / 128.0f);
            sample.r = LinearToDisplay(sample.r); sample.g = LinearToDisplay(sample.g); sample.b = LinearToDisplay(sample.b);
            samples[i] = sample;
        }
        vector2[] keys = working.keys.Select(key => new vector2(key.time, 0.5f)).ToArray();
        uint input;
        fixed (byte* text = bytes)
        fixed (color* colors = samples)
        fixed (vector2* markers = keys)
            input = curveApi.Canvas(text, bytes.Length, null, samples.Length, colors, markers, keys.Length,
                null, 0, selected, 0.0f, 0.0f, 64, &mouse, &wheel, &identity);
        bool changed = false;
        //渐变的加帧与删帧都走色标的右键菜单，版面上只留颜色选择器
        if ((input & 32) != 0) curveMenuTargets[identity] = PickCurveKey(keys, new vector2(mouse.x, 0.5f));
        if (BeginPopupContextItem($"{id}##menu"))
        {
            try { changed |= DrawGradientMenu(ref working, ref selected, curveMenuTargets.GetValueOrDefault(identity, -1)); }
            finally { EndPopup(); }
        }
        if ((input & 2) != 0)
        {
            int picked = PickCurveKey(keys, new vector2(mouse.x, 0.5f));
            if (picked >= 0)
            {
                selected = picked; draggedCurve = identity; draggingGradient = true; handleDrag = -1;
                gradientDragStart = new() { keys = [.. source.keys] };
                gradientDragValue = new() { keys = [.. source.keys] };
            }
            else if ((input & 4) != 0) changed = InsertGradientKey(ref working, mouse.x, out selected);
        }
        if (draggedCurve == identity)
        {
            if ((input & 16) != 0) ResetCurveDrag();
            else if ((input & 8) != 0)
                gradientDragValue.keys[selected].time = ClampKeyTime(gradientDragValue.keys.Select(k => k.time).ToArray(), selected, mouse.x);
            else
            {
                working = gradientDragValue;
                changed = !working.keys.SequenceEqual(gradientDragStart.keys);
                ResetCurveDrag();
            }
        }
        if ((input & 64) != 0 && draggedCurve != identity) changed |= RemoveGradientKey(ref working, ref selected);
        BeginDisabled(draggedCurve == identity);
        try
        {
            ParticleGradientKey key = working.keys[selected];
            //标题里带上第几个色标：选中的是哪一个只能从画布上看出来，这里给个数
            bool keyChanged = ColorField($"Color / Alpha  ({selected + 1} / {working.keys.Length})##{id}", ref key.value);
            if (keyChanged) { working.keys = [.. working.keys]; working.keys[selected] = key; changed = true; }
        }
        finally { EndDisabled(); }
        curveSelections[identity] = selected;
        if (!changed || working.keys.SequenceEqual(source.keys)) return false;
        value = working;
        return true;
    }

    /// <summary>选择关键帧并显示归一化时间。</summary>
    private static int SelectCurveKey(string id, int selected, float[] times)
    {
        if (BeginCombo($"Selected Key##{id}", $"{selected + 1} / {times.Length}  (t={times[selected]:G4})"))
        {
            for (int i = 0; i < times.Length; ++i)
                if (Selectable($"Key {i + 1}  (t={times[i]:G4})", selected == i)) selected = i;
            EndCombo();
        }
        return selected;
    }

    /// <summary>查找鼠标附近的归一化关键帧。</summary>
    private static int PickCurveKey(vector2[] keys, vector2 mouse)
    {
        int selected = -1;
        float distance = 0.0025f;
        for (int i = 0; i < keys.Length; ++i)
        {
            float dx = keys[i].x - mouse.x, dy = keys[i].y - mouse.y;
            float candidate = dx * dx + dy * dy;
            if (candidate > distance) continue;
            distance = candidate; selected = i;
        }
        return selected;
    }

    /// <summary>取这一帧的视图值域：没记过就按数据拟合，上一帧留下的滚轮以鼠标所在的值做锚点缩放。
    /// 视图不越出调用方给的值域，所以轴上不会出现不允许的数值。</summary>
    private static (float Minimum, float Maximum) ResolveCurveView(uint identity,
        float fitMinimum, float fitMaximum, float minimumValue, float maximumValue)
    {
        if (!curveViews.TryGetValue(identity, out (float Minimum, float Maximum) view)) view = (fitMinimum, fitMaximum);
        if (curveWheels.Remove(identity, out (float Delta, float Anchor) zoom))
        {
            float anchor = view.Minimum + zoom.Anchor * (view.Maximum - view.Minimum);
            float scale = MathF.Pow(1.15f, -zoom.Delta);
            view = (anchor + (view.Minimum - anchor) * scale, anchor + (view.Maximum - anchor) * scale);
        }

        view = (MathF.Max(view.Minimum, minimumValue), MathF.Min(view.Maximum, maximumValue));
        float span = view.Maximum - view.Minimum;
        if (!(span > 1.0e-6f) || !float.IsFinite(span))
        {
            //被值域夹没了或缩到没有跨度：退回一个最小窗口，仍然贴着值域边界
            float center = 0.5f * (fitMinimum + fitMaximum);
            float minimum = MathF.Max(center - 0.5f, minimumValue);
            float maximum = MathF.Min(minimum + 1.0f, maximumValue);
            view = maximum - minimum > 1.0e-6f ? (minimum, maximum) : (minimumValue, minimumValue + 1.0f);
        }

        curveViews[identity] = view;
        return view;
    }

    /// <summary>曲线关键帧的右键菜单：点类型、加一帧、删一帧；点在空白处给视图操作。</summary>
    private static bool DrawCurveMenu(uint identity, ref ParticleCurve curve, ref int selected, int target)
    {
        if (target < 0 || target >= curve.keys.Length)
        {
            if (MenuItem("Fit View")) curveViews.Remove(identity);
            return false;
        }

        bool changed = false;
        ParticleCurveInterpolation current = curve.keys[target].interpolation;
        if (MenuItem("Linear", current != ParticleCurveInterpolation.Linear)) changed |= SetInterpolation(ref curve, target, ParticleCurveInterpolation.Linear);
        if (MenuItem("Cubic", current != ParticleCurveInterpolation.Cubic)) changed |= SetInterpolation(ref curve, target, ParticleCurveInterpolation.Cubic);
        if (MenuItem("Constant", current != ParticleCurveInterpolation.Constant)) changed |= SetInterpolation(ref curve, target, ParticleCurveInterpolation.Constant);
        Separator();
        if (MenuItem("Add Key After", target < curve.keys.Length - 1 && curve.keys.Length < 64))
        {
            float midpoint = (curve.keys[target].time + curve.keys[target + 1].time) * 0.5f;
            changed |= InsertCurveKey(ref curve, midpoint, CurveMath.EvaluateCurve(curve, midpoint), out selected);
        }
        //两端是曲线的结构，删不掉：菜单项照画但禁用，让这条路径看得出来
        if (MenuItem("Delete Key", target > 0 && target < curve.keys.Length - 1))
        {
            selected = target;
            changed |= RemoveCurveKey(ref curve, ref selected);
        }
        return changed;
    }

    /// <summary>渐变色标的右键菜单：加一帧、删一帧。</summary>
    private static bool DrawGradientMenu(ref ParticleGradient gradient, ref int selected, int target)
    {
        if (target < 0 || target >= gradient.keys.Length) return false;

        bool changed = false;
        if (MenuItem("Add Key After", target < gradient.keys.Length - 1 && gradient.keys.Length < 64))
        {
            float midpoint = (gradient.keys[target].time + gradient.keys[target + 1].time) * 0.5f;
            int inserted = 0;
            if (InsertGradientKey(ref gradient, midpoint, out inserted)) { selected = inserted; changed = true; }
        }
        if (MenuItem("Delete Key", target > 0 && target < gradient.keys.Length - 1))
        {
            selected = target;
            changed |= RemoveGradientKey(ref gradient, ref selected);
        }
        return changed;
    }

    /// <summary>换插值方式；切到三次时补一条自动切线，免得看上去还是折线。</summary>
    private static bool SetInterpolation(ref ParticleCurve curve, int index, ParticleCurveInterpolation interpolation)
    {
        if (index < 0 || index >= curve.keys.Length || curve.keys[index].interpolation == interpolation) return false;
        curve.keys = [.. curve.keys];
        curve.keys[index].interpolation = interpolation;
        if (interpolation == ParticleCurveInterpolation.Cubic) ApplyAutoTangent(ref curve, index);
        return true;
    }

    /// <summary>用相邻关键帧的割线斜率当自动切线，两端沿用同一侧。</summary>
    private static void ApplyAutoTangent(ref ParticleCurve curve, int index)
    {
        ParticleCurveKey[] keys = curve.keys;
        if (index < 0 || index >= keys.Length) return;
        float span, slope = 0.0f;
        if (index > 0 && index < keys.Length - 1)
        {
            span = keys[index + 1].time - keys[index - 1].time;
            if (span > 0.0f) slope = (keys[index + 1].value - keys[index - 1].value) / span;
        }
        else if (index > 0)
        {
            span = keys[index].time - keys[index - 1].time;
            if (span > 0.0f) slope = (keys[index].value - keys[index - 1].value) / span;
        }
        else
        {
            span = keys[1].time - keys[0].time;
            if (span > 0.0f) slope = (keys[1].value - keys[0].value) / span;
        }

        ParticleCurveKey key = keys[index];
        key.interpolation = ParticleCurveInterpolation.Cubic;
        key.inTangent = ClampTangent(slope);
        key.outTangent = ClampTangent(slope);
        keys[index] = key;
    }

    /// <summary>取选中关键帧的两条切线，返回位掩码：1 = 入手柄，2 = 出手柄。
    /// 手柄的位置与长度由原生侧按像素算，这里只给斜率。</summary>
    private static int FillCurveHandles(ParticleCurve curve, int selected, vector2[] handles)
    {
        handles[0] = default;
        handles[1] = default;
        if (selected < 0 || selected >= curve.keys.Length) return 0;
        ParticleCurveKey key = curve.keys[selected];
        if (key.interpolation != ParticleCurveInterpolation.Cubic) return 0;
        handles[0] = new vector2(key.inTangent, 0.0f);
        handles[1] = new vector2(key.outTangent, 0.0f);
        //首帧没有入切线、末帧没有出切线：求值只读另一侧，手柄也就不画
        return (selected > 0 ? 1 : 0) | (selected < curve.keys.Length - 1 ? 2 : 0);
    }

    /// <summary>拖手柄只改切线，不改关键帧自身：鼠标在关键帧哪一侧就按哪一侧求斜率。</summary>
    private static void ApplyHandleDrag(ParticleCurve curve, int handle, int index, vector2 mouse, float minimum, float maximum)
    {
        if (index < 0 || index >= curve.keys.Length) return;
        ParticleCurveKey key = curve.keys[index];
        float value = minimum + mouse.y * (maximum - minimum);
        if (handle == 0)
        {
            if (index == 0) return;
            float time = MathF.Min(mouse.x, key.time - 0.004f);
            key.inTangent = ClampTangent((key.value - value) / (key.time - time));
        }
        else
        {
            if (index == curve.keys.Length - 1) return;
            float time = MathF.Max(mouse.x, key.time + 0.004f);
            key.outTangent = ClampTangent((value - key.value) / (time - key.time));
        }
        curve.keys[index] = key;
    }

    /// <summary>切线可以很陡，但不能是 nan 或无穷；引擎侧的上限是 1e6，这里收得更紧。</summary>
    private static float ClampTangent(float value)
        => float.IsFinite(value) ? Math.Clamp(value, -1.0e4f, 1.0e4f) : 0.0f;

    /// <summary>锁定端点并限制内部关键帧顺序。</summary>
    private static float ClampKeyTime(float[] times, int selected, float time)
        => selected == 0 ? 0 : selected == times.Length - 1 ? 1 : Math.Clamp(time, times[selected - 1] + 0.0001f, times[selected + 1] - 0.0001f);

    /// <summary>添加不与已有时间重叠的曲线关键帧；新帧按邻居斜率自动平滑，默认就是曲线而不是折线。</summary>
    private static bool InsertCurveKey(ref ParticleCurve curve, float time, float value, out int selected)
    {
        selected = 0;
        time = Math.Clamp(time, 0, 1);
        if (curve.keys.Length >= 64 || curve.keys.Any(key => MathF.Abs(key.time - time) < 0.0002f)) return false;
        curve.keys = [.. curve.keys.Append(new ParticleCurveKey { time = time, value = value }).OrderBy(key => key.time)];
        selected = Array.FindIndex(curve.keys, key => key.time == time);
        ApplyAutoTangent(ref curve, selected);
        return true;
    }

    /// <summary>删除选中的内部曲线关键帧；两端是曲线的结构，返回 false。</summary>
    private static bool RemoveCurveKey(ref ParticleCurve curve, ref int selected)
    {
        if (selected <= 0 || selected >= curve.keys.Length - 1) return false;
        int removed = selected;
        curve.keys = curve.keys.Where((_, index) => index != removed).ToArray();
        --selected;
        return true;
    }

    /// <summary>删除选中的内部渐变关键帧；两端同上。</summary>
    private static bool RemoveGradientKey(ref ParticleGradient gradient, ref int selected)
    {
        if (selected <= 0 || selected >= gradient.keys.Length - 1) return false;
        int removed = selected;
        gradient.keys = gradient.keys.Where((_, index) => index != removed).ToArray();
        --selected;
        return true;
    }

    /// <summary>采样并插入颜色关键帧。</summary>
    private static bool InsertGradientKey(ref ParticleGradient gradient, float time, out int selected)
    {
        selected = 0;
        time = Math.Clamp(time, 0, 1);
        if (gradient.keys.Length >= 64 || gradient.keys.Any(key => MathF.Abs(key.time - time) < 0.0002f)) return false;
        color value = CurveMath.EvaluateGradient(gradient, time);
        value.r = LinearToDisplay(value.r); value.g = LinearToDisplay(value.g); value.b = LinearToDisplay(value.b);
        gradient.keys = [.. gradient.keys.Append(new ParticleGradientKey { time = time, value = value }).OrderBy(key => key.time)];
        selected = Array.FindIndex(gradient.keys, key => key.time == time);
        return true;
    }

    /// <summary>将线性颜色转换为渐变配置与 GUI 使用的显示颜色。</summary>
    private static float LinearToDisplay(float value)
        => value <= 0.0031308f ? value * 12.92f : 1.055f * MathF.Pow(Math.Clamp(value, 0, 1), 1 / 2.4f) - 0.055f;
}

/// <summary>曲线与渐变的编辑预览求值。</summary>
internal static class CurveMath
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

