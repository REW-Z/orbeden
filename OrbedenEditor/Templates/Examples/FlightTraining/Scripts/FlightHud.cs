using Orbeden;

namespace FlightTraining;

/// <summary>使用 RetainedGUI 管理飞行 PFD 和控制轴。</summary>
public sealed class FlightHud : Script
{
    private ComponentProxy? controller;
    private ComponentProxy? engine;
    private NativeMethod<float> airspeedMethod = null!;
    private NativeMethod<float> throttleMethod = null!;
    private NativeMethod<float> pitchMethod = null!;
    private NativeMethod<float> rollMethod = null!;
    private NativeMethod<float> headingMethod = null!;
    private NativeMethod<float> altitudeMethod = null!;
    private NativeMethod<float> climbRateMethod = null!;
    private NativeMethod<float> sideslipMethod = null!;
    private NativeMethod<float> flapMethod = null!;
    private NativeMethod<float> flapTargetMethod = null!;
    private NativeMethod<float> trimMethod = null!;
    private NativeAction<float> setTrim = null!;
    private NativeAction<float> setFlapTarget = null!;
    private NativeAction<float> setThrottle = null!;
    private NativeField<bool> showForcesField;
    private NativeField<float> brakeField;

    private Ens? controlsCanvas;
    private Canvas? canvas;
    private Ens? hudRoot;
    private UILayout? pfdLayout;
    private UILayout? leftControlsPanel;
    private UILayout? rightControlsPanel;
    private UILayout? attitudeLayout;
    private UILayout? speedTapeLayout;
    private UILayout? altitudeTapeLayout;
    private UILayout? headingTapeLayout;
    private UILayout? horizonLayout;
    private Ens? bankPointer;
    private Text? statusLabel;
    private Text? speedLabel;
    private Text? altitudeLabel;
    private Text? headingLabel;
    private Slider? brakeSlider;
    private Slider? trimSlider;
    private Slider? flapSlider;
    private Slider? throttleSlider;
    private Text? forcesLabel;
    private Text? brakeLabel;
    private Text? trimLabel;
    private Text? throttleLabel;
    private Text? flapLabel;
    private UILayout? speedTicksLayout;
    private UILayout? altitudeTicksLayout;
    private UILayout? headingTicksLayout;
    private readonly List<(Image mark, Text label, UILayout markLayout, UILayout labelLayout)> speedTicks = [];
    private readonly List<(Image mark, Text label, UILayout markLayout, UILayout labelLayout)> altitudeTicks = [];
    private readonly List<(Image mark, Text label, UILayout markLayout, UILayout labelLayout)> headingTicks = [];
    private vector2 viewportSize;
    private float headingWidth;

    private const float WindowHeight = 336;
    private const float ControlsWidth = 244;
    private const float ControlsHeight = 172;
    private const float PfdBottom = 42;
    private const float PixelsPerDegree = 3.5f;
    private const float DegToRad = MathF.PI / 180;

    //设置线性颜色
    private static readonly color ColorWhite = new(0.830770f, 0.887923f, 0.938686f, 1);
    private static readonly color ColorGrey = new(0.274677f, 0.356400f, 0.445201f, 1);
    private static readonly color ColorYellow = new(1, 0.651406f, 0.054480f, 1);
    private static readonly color ColorSky = new(0.020289f, 0.152926f, 0.423268f, 1);
    private static readonly color ColorGround = new(0.162029f, 0.070360f, 0.029557f, 1);
    private static readonly color ColorDark = new(0.004025f, 0.007499f, 0.012983f, 0.96f);
    private static readonly color ColorPanel = new(0.008568f, 0.015996f, 0.027321f, 1);
    private static readonly color ColorBorder = new(0.049707f, 0.082283f, 0.124772f, 1);
    private static readonly color SliderTrackTint = new(0.030074f, 0.050876f, 0.083535f, 1);
    private static readonly color SliderFillTint = new(0.050876f, 0.263273f, 0.692071f, 1);
    private static readonly color ForceEnabledTint = new(0.132868f, 0.787412f, 0.318547f, 1);

    /// <summary>创建飞行 HUD。</summary>
    public FlightHud(Ens ens) : base(ens) { }

    //绑定原生读数并创建界面
    private void OnStart()
    {
        controller = Ens.GetNativeComponent("FlightController");
        engine = Ens.GetNativeComponent("Engine");
        if (controller == null || engine == null)
        {
            return;
        }

        if (controller.TryBindMethod("GetAirspeed", out airspeedMethod) != InteropStatus.Ok
            || engine.TryBindMethod("GetThrottle", out throttleMethod) != InteropStatus.Ok
            || controller.TryBindMethod("GetPitchDegrees", out pitchMethod) != InteropStatus.Ok
            || controller.TryBindMethod("GetRollDegrees", out rollMethod) != InteropStatus.Ok
            || controller.TryBindMethod("GetHeadingDegrees", out headingMethod) != InteropStatus.Ok
            || controller.TryBindMethod("GetAltitude", out altitudeMethod) != InteropStatus.Ok
            || controller.TryBindMethod("GetClimbRate", out climbRateMethod) != InteropStatus.Ok
            || controller.TryBindMethod("GetSideslipDegrees", out sideslipMethod) != InteropStatus.Ok
            || controller.TryBindMethod("GetFlapPosition", out flapMethod) != InteropStatus.Ok
            || controller.TryBindMethod("GetFlapTarget", out flapTargetMethod) != InteropStatus.Ok
            || controller.TryBindMethod("GetPitchTrim", out trimMethod) != InteropStatus.Ok
            || controller.TryBindAction("SetPitchTrim", out setTrim) != InteropStatus.Ok
            || controller.TryBindAction("SetFlapTarget", out setFlapTarget) != InteropStatus.Ok
            || engine.TryBindAction("SetThrottle", out setThrottle) != InteropStatus.Ok
            || controller.TryBindField("showForces", out showForcesField) != InteropStatus.Ok
            || controller.TryBindField("brakeInput", out brakeField) != InteropStatus.Ok)
        {
            controller = null;
            engine = null;
            return;
        }

        //创建底部居中的 HUD 根节点
        controlsCanvas = CreateUiNode(null, "FlightHudCanvas", default, default);
        controlsCanvas.Transform.localScale = new vector3(0.01f, 0.01f, 0.01f);
        canvas = controlsCanvas.AddComponent<Canvas>()!;
        canvas.SetSortOrder(20);
        hudRoot = CreateUiNode(controlsCanvas, "FlightHud", default, new vector2(1072, 372));
        UILayout rootLayout = hudRoot.GetComponent<UILayout>()!;
        rootLayout.SetAnchors(new vector2(0.5f, 0), new vector2(0.5f, 0));
        rootLayout.SetPivot(new vector2(0.5f, 0));
        rootLayout.SetOffset(new vector2(0, 6));
        hudRoot.LocalActive = false;

        //创建左右控制区
        Ens leftPanel = CreateUiNode(hudRoot, "LeftFlightControls", default, new vector2(ControlsWidth, ControlsHeight));
        Ens rightPanel = CreateUiNode(hudRoot, "RightFlightControls", default, new vector2(ControlsWidth, ControlsHeight));
        leftControlsPanel = leftPanel.GetComponent<UILayout>()!;
        rightControlsPanel = rightPanel.GetComponent<UILayout>()!;
        leftControlsPanel.SetAnchors(default, default);
        leftControlsPanel.SetPivot(default);
        rightControlsPanel.SetAnchors(new vector2(1, 0), new vector2(1, 0));
        rightControlsPanel.SetPivot(new vector2(1, 0));
        float panelY = PfdBottom - 6 + (WindowHeight - ControlsHeight) * 0.5f;
        leftControlsPanel.SetOffset(new vector2(0, panelY));
        rightControlsPanel.SetOffset(new vector2(0, panelY));
        foreach (Ens panel in new[] { leftPanel, rightPanel })
        {
            Image background = panel.AddComponent<Image>()!;
            background.SetTint(ColorDark);
            background.SetRaycastTarget(false);
        }

        brakeSlider = CreateControlSlider(leftPanel, "BRAKE", 16, 0, 1);
        trimSlider = CreateControlSlider(leftPanel, "TRIM", 90, -1, 1);
        throttleSlider = CreateControlSlider(rightPanel, "THROTTLE", 16, 0, 1);
        flapSlider = CreateControlSlider(rightPanel, "FLAPS", 90, 0, 1);
        brakeLabel = brakeSlider.Ens.GetComponent<Text>()!;
        trimLabel = trimSlider.Ens.GetComponent<Text>()!;
        throttleLabel = throttleSlider.Ens.GetComponent<Text>()!;
        flapLabel = flapSlider.Ens.GetComponent<Text>()!;
        brakeSlider.ValueChanged.Subscribe(value => brakeField.Value = value);
        trimSlider.ValueChanged.Subscribe(value => setTrim(value));
        flapSlider.ValueChanged.Subscribe(value => setFlapTarget(value));
        throttleSlider.ValueChanged.Subscribe(value => setThrottle(value));

        //创建 PFD 与受力线开关
        CreatePfd(hudRoot);
        Ens toggle = CreateUiNode(hudRoot, "ForceVectors", default, new vector2(212, 26));
        UILayout toggleLayout = toggle.GetComponent<UILayout>()!;
        toggleLayout.SetAnchors(new vector2(0.5f, 0), new vector2(0.5f, 0));
        toggleLayout.SetPivot(new vector2(0.5f, 0));
        Button button = toggle.AddComponent<Button>()!;
        toggle.GetComponent<Image>()!.SetTint(ColorPanel);
        forcesLabel = CreateText(toggle, "Label", default, new vector2(212, 26), "", 12, ColorGrey);
        forcesLabel.SetHorizontalAlignment(UITextAlignment.Center);
        forcesLabel.SetVerticalAlignment(UITextVerticalAlignment.Center);
        button.ClickEvent.Subscribe(() => showForcesField.Value = !showForcesField.Value);

        //初始化首个可见帧的布局与读数
        OnUpdate(0);
    }

    //创建左上角定位的 UI 节点
    private Ens CreateUiNode(Ens? parent, string name, vector2 position, vector2 size)
    {
        Ens node = Orbeden.Ens.Create(name);
        if (parent != null)
        {
            node.Transform.SetParent(parent.Id);
        }
        UILayout layout = node.AddComponent<UILayout>()!;
        layout.SetAnchors(new vector2(0, 1), new vector2(0, 1));
        layout.SetPivot(new vector2(0, 1));
        layout.SetSizeDelta(size);
        layout.SetOffset(new vector2(position.x, -position.y));
        return node;
    }

    //创建纯色矩形
    private Image CreateImage(Ens parent, string name, vector2 position, vector2 size, color tint)
    {
        Image image = CreateUiNode(parent, name, position, size).AddComponent<Image>()!;
        image.SetTint(tint);
        image.SetRaycastTarget(false);
        return image;
    }

    //创建文字节点
    private Text CreateText(Ens parent, string name, vector2 position, vector2 size, string value, float fontSize, color tint)
    {
        Text text = CreateUiNode(parent, name, position, size).AddComponent<Text>()!;
        text.SetText(value);
        text.SetFontSize(fontSize);
        text.SetTint(tint);
        text.SetWrap(false);
        return text;
    }

    //创建连接两点的线段
    private void CreateLine(Ens parent, string name, vector2 start, vector2 end, color tint, float thickness = 1)
    {
        float dx = end.x - start.x;
        float dy = end.y - start.y;
        Image line = CreateImage(parent, name, default, new vector2(MathF.Sqrt(dx * dx + dy * dy), thickness), tint);
        UILayout layout = line.GetLayout()!;
        layout.SetPivot(new vector2(0.5f, 0.5f));
        layout.SetOffset(new vector2((start.x + end.x) * 0.5f, -(start.y + end.y) * 0.5f));
        float angle = MathF.Atan2(-dy, dx) * 0.5f;
        line.Ens.Transform.localRotation = new quaternion(0, 0, MathF.Sin(angle), MathF.Cos(angle));
    }

    //创建随父矩形伸缩的边框
    private void CreateBorder(Ens parent, color tint)
    {
        foreach (bool horizontal in new[] { true, false })
        {
            foreach (float edge in new[] { 0.0f, 1.0f })
            {
                Image border = CreateImage(parent, "Border", default, default, tint);
                UILayout layout = border.GetLayout()!;
                vector2 min = horizontal ? new vector2(0, edge) : new vector2(edge, 0);
                vector2 max = horizontal ? new vector2(1, edge) : new vector2(edge, 1);
                layout.SetAnchors(min, max);
                layout.SetPivot(horizontal ? new vector2(0, edge) : new vector2(edge, 0));
                layout.SetSizeDelta(horizontal ? new vector2(0, 1) : new vector2(1, 0));
            }
        }
    }

    //创建带轨道、填充和手柄的控制轴
    private Slider CreateControlSlider(Ens parent, string label, float y, float minimum, float maximum)
    {
        Ens node = CreateUiNode(parent, label, new vector2(16, y), new vector2(-32, 56));
        UILayout layout = node.GetComponent<UILayout>()!;
        layout.SetAnchors(new vector2(0, 1), new vector2(1, 1));
        Slider slider = node.AddComponent<Slider>()!;
        slider.SetRange(minimum, maximum);
        Text text = node.AddComponent<Text>()!;
        text.SetText(label);
        text.SetFontSize(14);
        text.SetWrap(true);
        text.SetTint(ColorWhite);

        Image track = CreateImage(node, "Track", new vector2(0, 36), new vector2(0, 8), SliderTrackTint);
        track.GetLayout()!.SetAnchors(new vector2(0, 1), new vector2(1, 1));
        track.SetRaycastTarget(true);
        Image fill = CreateImage(node, "Fill", new vector2(0, 36), new vector2(0, 8), SliderFillTint);
        Image thumb = CreateImage(node, "Thumb", new vector2(0, 31), new vector2(12, 18), ColorYellow);
        thumb.SetRaycastTarget(true);
        slider.SetTrack(track.GetLayout()!);
        slider.SetFill(fill.GetLayout()!);
        slider.SetThumb(thumb.GetLayout()!);
        return slider;
    }

    //创建 PFD 的固定节点
    private void CreatePfd(Ens parent)
    {
        Ens pfd = CreateImage(parent, "PFD", default, new vector2(560, WindowHeight), ColorDark).Ens;
        pfdLayout = pfd.GetComponent<UILayout>()!;
        pfdLayout.SetAnchors(new vector2(0.5f, 0), new vector2(0.5f, 0));
        pfdLayout.SetPivot(new vector2(0.5f, 0));
        pfdLayout.SetOffset(new vector2(0, PfdBottom - 6));
        pfd.AddComponent<Mask>();
        CreateBorder(pfd, ColorBorder);
        CreateStatusLine(pfd);
        CreateAttitude(pfd);
        speedTapeLayout = CreateValueTape(pfd, false);
        altitudeTapeLayout = CreateValueTape(pfd, true);
        CreateHeadingTape(pfd);
    }

    //创建标题与飞行状态文字
    private void CreateStatusLine(Ens parent)
    {
        CreateText(parent, "Title", new vector2(16, 9), new vector2(320, 18), "PRIMARY FLIGHT DISPLAY", 11, ColorWhite);
        statusLabel = CreateText(parent, "Status", new vector2(16, 31), new vector2(-32, 18), "", 11, ColorGrey);
        statusLabel.GetLayout()!.SetAnchors(new vector2(0, 1), new vector2(1, 1));
        Image rule = CreateImage(parent, "Rule", new vector2(16, 54), new vector2(-32, 1), ColorBorder);
        rule.GetLayout()!.SetAnchors(new vector2(0, 1), new vector2(1, 1));
    }

    //创建地平线、俯仰梯和倾斜刻度
    private void CreateAttitude(Ens parent)
    {
        Ens attitude = CreateUiNode(parent, "Attitude", new vector2(100, 62), new vector2(360, 220));
        attitudeLayout = attitude.GetComponent<UILayout>()!;
        attitude.AddComponent<Mask>();
        Ens horizon = CreateUiNode(attitude, "Horizon", default, default);
        horizonLayout = horizon.GetComponent<UILayout>()!;
        horizonLayout.SetAnchors(new vector2(0.5f, 0.5f), new vector2(0.5f, 0.5f));
        horizonLayout.SetPivot(new vector2(0.5f, 0.5f));
        const float extent = 2000;
        CreateImage(horizon, "Sky", new vector2(-extent, -extent), new vector2(extent * 2, extent), ColorSky);
        CreateImage(horizon, "Ground", new vector2(-extent, 0), new vector2(extent * 2, extent), ColorGround);
        CreateLine(horizon, "HorizonLine", new vector2(-extent, 0), new vector2(extent, 0), ColorWhite, 1.5f);

        //创建随地平线旋转的俯仰梯
        for (int value = -90; value <= 90; value += 5)
        {
            if (value == 0)
            {
                continue;
            }
            bool major = value % 10 == 0;
            float half = major ? 34 : 20;
            float y = -value * PixelsPerDegree;
            CreateLine(horizon, "PitchLeft", new vector2(-half, y), new vector2(-6, y), ColorWhite);
            CreateLine(horizon, "PitchRight", new vector2(6, y), new vector2(half, y), ColorWhite);
            if (major)
            {
                Text left = CreateText(horizon, "PitchLabel", new vector2(-half - 37, y - 8), new vector2(30, 16), $"{value}", 11, ColorWhite);
                left.SetHorizontalAlignment(UITextAlignment.Right);
                CreateText(horizon, "PitchLabel", new vector2(half + 7, y - 8), new vector2(30, 16), $"{value}", 11, ColorWhite);
            }
        }

        //创建固定机头符号与倾斜刻度
        Ens overlay = CreateUiNode(attitude, "AttitudeSymbols", default, default);
        UILayout overlayLayout = overlay.GetComponent<UILayout>()!;
        overlayLayout.SetAnchors(new vector2(0.5f, 0.5f), new vector2(0.5f, 0.5f));
        overlayLayout.SetOffset(default);
        foreach (int bank in new[] { -60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60 })
        {
            float angle = bank * DegToRad;
            float length = bank % 30 == 0 ? 10 : 5;
            vector2 start = new(MathF.Sin(angle) * 94, -MathF.Cos(angle) * 94);
            vector2 end = new(MathF.Sin(angle) * (94 - length), -MathF.Cos(angle) * (94 - length));
            CreateLine(overlay, "BankTick", start, end, ColorWhite, 1.5f);
        }
        bankPointer = CreateUiNode(overlay, "BankPointer", default, default);
        CreateLine(bankPointer, "PointerLeft", new vector2(0, -81), new vector2(-4, -73), ColorYellow, 2);
        CreateLine(bankPointer, "PointerRight", new vector2(0, -81), new vector2(4, -73), ColorYellow, 2);
        CreateLine(bankPointer, "PointerBase", new vector2(-4, -73), new vector2(4, -73), ColorYellow, 2);
        foreach (float side in new[] { -1.0f, 1.0f })
        {
            CreateLine(overlay, "AircraftOutline", new vector2(side * 9, 0), new vector2(side * 35, 0), ColorDark, 5);
            CreateLine(overlay, "AircraftWing", new vector2(side * 9, 0), new vector2(side * 35, 0), ColorYellow, 2);
        }
        CreateImage(overlay, "AircraftCenter", new vector2(-3, -3), new vector2(6, 6), ColorYellow);
        CreateBorder(attitude, ColorBorder);
    }

    //创建空速或高度数字带
    private UILayout CreateValueTape(Ens parent, bool altitude)
    {
        Ens tape = CreateImage(parent, altitude ? "AltitudeTape" : "SpeedTape", default, new vector2(72, 220), ColorPanel).Ens;
        Text title = CreateText(tape, "Title", new vector2(0, 4), new vector2(0, 16), altitude ? "ALT" : "IAS", 10, ColorGrey);
        title.GetLayout()!.SetAnchors(new vector2(0, 1), new vector2(1, 1));
        title.SetHorizontalAlignment(UITextAlignment.Center);
        Text units = CreateText(tape, "Units", default, new vector2(0, 17), altitude ? "ft" : "kt", 10, ColorGrey);
        units.GetLayout()!.SetAnchors(default, new vector2(1, 0));
        units.GetLayout()!.SetPivot(default);
        units.SetHorizontalAlignment(UITextAlignment.Center);

        //创建裁剪范围内的复用刻度
        Ens scroll = CreateUiNode(tape, "Ticks", new vector2(0, 22), new vector2(0, 178));
        scroll.GetComponent<UILayout>()!.SetAnchors(new vector2(0, 1), new vector2(1, 1));
        scroll.AddComponent<Mask>();
        Ens scale = CreateUiNode(scroll, "Scale", default, default);
        UILayout scaleLayout = scale.GetComponent<UILayout>()!;
        scaleLayout.SetAnchors(new vector2(0, 0.5f), new vector2(1, 0.5f));
        scaleLayout.SetPivot(new vector2(0, 0.5f));
        if (altitude)
        {
            altitudeTicksLayout = scaleLayout;
        }
        else
        {
            speedTicksLayout = scaleLayout;
        }
        List<(Image mark, Text label, UILayout markLayout, UILayout labelLayout)> ticks = altitude ? altitudeTicks : speedTicks;
        int count = altitude ? 11 : 13;
        for (int index = 0; index < count; ++index)
        {
            Image mark = CreateImage(scale, "Tick", default, new vector2(10, 1), ColorGrey);
            UILayout markLayout = mark.GetLayout()!;
            vector2 anchor = new(altitude ? 0 : 1, 0.5f);
            markLayout.SetAnchors(anchor, anchor);
            markLayout.SetPivot(anchor);
            Text label = CreateText(scale, "TickLabel", default, new vector2(0, 16), "", 11, ColorWhite);
            UILayout labelLayout = label.GetLayout()!;
            labelLayout.SetAnchors(new vector2(0, 0.5f), new vector2(1, 0.5f));
            labelLayout.SetPivot(new vector2(0, 0.5f));
            labelLayout.SetSizeDelta(new vector2(altitude ? -20 : -16, 16));
            label.SetHorizontalAlignment(UITextAlignment.Right);
            markLayout.SetOffset(new vector2(altitude ? 1 : -1, index * 20));
            labelLayout.SetOffset(new vector2(altitude ? 16 : 0, index * 20));
            ticks.Add((mark, label, markLayout, labelLayout));
        }

        Image readout = CreateImage(tape, "Readout", default, new vector2(-2, 34), ColorDark);
        UILayout readoutLayout = readout.GetLayout()!;
        readoutLayout.SetAnchors(new vector2(0, 0.5f), new vector2(1, 0.5f));
        readoutLayout.SetPivot(new vector2(0, 0.5f));
        readoutLayout.SetOffset(new vector2(1, 0));
        CreateBorder(readout.Ens, ColorWhite);
        Text valueLabel = CreateText(readout.Ens, "Value", default, new vector2(0, 34), "0", 16, ColorWhite);
        valueLabel.GetLayout()!.SetAnchors(new vector2(0, 1), new vector2(1, 1));
        valueLabel.SetHorizontalAlignment(UITextAlignment.Center);
        valueLabel.SetVerticalAlignment(UITextVerticalAlignment.Center);
        if (altitude)
        {
            altitudeLabel = valueLabel;
        }
        else
        {
            speedLabel = valueLabel;
        }
        return tape.GetComponent<UILayout>()!;
    }

    //创建航向数字带
    private void CreateHeadingTape(Ens parent)
    {
        Ens tape = CreateImage(parent, "HeadingTape", new vector2(100, 292), new vector2(360, 40), ColorPanel).Ens;
        headingTapeLayout = tape.GetComponent<UILayout>()!;
        tape.AddComponent<Mask>();
        Ens scale = CreateUiNode(tape, "Scale", default, new vector2(0, 40));
        headingTicksLayout = scale.GetComponent<UILayout>()!;
        headingTicksLayout.SetAnchors(new vector2(0.5f, 1), new vector2(0.5f, 1));
        headingTicksLayout.SetPivot(new vector2(0.5f, 1));
        for (int index = 0; index < 19; ++index)
        {
            Image mark = CreateImage(scale, "Tick", default, new vector2(1, 9), ColorGrey);
            UILayout markLayout = mark.GetLayout()!;
            markLayout.SetAnchors(new vector2(0.5f, 1), new vector2(0.5f, 1));
            markLayout.SetPivot(new vector2(0.5f, 1));
            Text label = CreateText(scale, "TickLabel", default, new vector2(44, 18), "", 11, ColorWhite);
            UILayout labelLayout = label.GetLayout()!;
            labelLayout.SetAnchors(new vector2(0.5f, 1), new vector2(0.5f, 1));
            labelLayout.SetPivot(new vector2(0.5f, 1));
            label.SetHorizontalAlignment(UITextAlignment.Center);
            headingTicks.Add((mark, label, markLayout, labelLayout));
        }
        Image readout = CreateImage(tape, "Readout", default, new vector2(62, 31), ColorDark);
        readout.GetLayout()!.SetAnchors(new vector2(0.5f, 1), new vector2(0.5f, 1));
        readout.GetLayout()!.SetPivot(new vector2(0.5f, 1));
        readout.GetLayout()!.SetOffset(new vector2(0, -8));
        CreateBorder(readout.Ens, ColorYellow);
        headingLabel = CreateText(readout.Ens, "Value", default, new vector2(62, 31), "000 deg", 11, ColorYellow);
        headingLabel.SetHorizontalAlignment(UITextAlignment.Center);
        headingLabel.SetVerticalAlignment(UITextVerticalAlignment.Center);
        Ens pointer = CreateUiNode(tape, "HeadingPointer", default, default);
        pointer.GetComponent<UILayout>()!.SetAnchors(new vector2(0.5f, 1), new vector2(0.5f, 1));
        CreateLine(pointer, "Left", new vector2(-4, 0), new vector2(0, 6), ColorYellow, 2);
        CreateLine(pointer, "Right", new vector2(4, 0), new vector2(0, 6), ColorYellow, 2);
    }

    //按显示目标尺寸同步整组布局
    private void UpdateLayout(UIWorldContext context, vector2 viewport)
    {
        context.SetCanvasViewport(canvas!, viewport);
        viewportSize = viewport;
        float sideWidth = MathF.Min(ControlsWidth, MathF.Max(96, (viewport.x - 368) * 0.5f));
        float pfdWidth = MathF.Min(560, MathF.Max(32, viewport.x - 2 * sideWidth - 48));
        float groupWidth = pfdWidth + 2 * sideWidth + 24;
        hudRoot!.GetComponent<UILayout>()!.SetSizeDelta(new vector2(groupWidth, 372));
        pfdLayout!.SetSizeDelta(new vector2(pfdWidth, WindowHeight));
        leftControlsPanel!.SetSizeDelta(new vector2(sideWidth, ControlsHeight));
        rightControlsPanel!.SetSizeDelta(new vector2(sideWidth, ControlsHeight));

        float tapeWidth = MathF.Min(72, pfdWidth * 0.17f);
        float attitudeWidth = MathF.Min(360, MathF.Max(32, pfdWidth - 2 * tapeWidth - 48));
        float attitudeX = (pfdWidth - attitudeWidth) * 0.5f;
        attitudeLayout!.SetSizeDelta(new vector2(attitudeWidth, 220));
        attitudeLayout.SetOffset(new vector2(attitudeX, -62));
        speedTapeLayout!.SetSizeDelta(new vector2(tapeWidth, 220));
        speedTapeLayout.SetOffset(new vector2(attitudeX - 12 - tapeWidth, -62));
        altitudeTapeLayout!.SetSizeDelta(new vector2(tapeWidth, 220));
        altitudeTapeLayout.SetOffset(new vector2(attitudeX + attitudeWidth + 12, -62));
        headingTapeLayout!.SetSizeDelta(new vector2(attitudeWidth, 40));
        headingTapeLayout.SetOffset(new vector2(attitudeX, -292));
        headingWidth = attitudeWidth;

        //按带宽设置刻度间距
        float visibleDegrees = headingWidth < 240 ? 40 : 80;
        for (int index = 0; index < headingTicks.Count; ++index)
        {
            float x = index * 5 * headingWidth / visibleDegrees;
            headingTicks[index].markLayout.SetOffset(new vector2(x, 0));
            headingTicks[index].labelLayout.SetOffset(new vector2(x, -16));
        }
    }

    //同步读数、姿态与控制轴
    private void OnUpdate(float deltaTime)
    {
        if (controller == null || !controller.IsValid || engine == null || !engine.IsValid || hudRoot == null)
        {
            return;
        }

        UIWorldContext? context = UIWorldContext.Current;
        RetainedGuiBridge? bridge = context?.NativeBridge;
        if (bridge == null || !bridge.TryReadDisplaySize(out int width, out int height))
        {
            return;
        }
        if (viewportSize.x != width || viewportSize.y != height)
        {
            UpdateLayout(context!, new vector2(width, height));
        }

        try
        {
            //更新地平线与倾斜指针
            float pitch = pitchMethod();
            float roll = rollMethod() * DegToRad;
            horizonLayout!.SetOffset(new vector2(MathF.Sin(roll) * pitch * PixelsPerDegree, -MathF.Cos(roll) * pitch * PixelsPerDegree));
            horizonLayout.Ens.Transform.localRotation = new quaternion(0, 0, MathF.Sin(roll * 0.5f), MathF.Cos(roll * 0.5f));
            bankPointer!.Transform.localRotation = new quaternion(0, 0, -MathF.Sin(roll * 0.5f), MathF.Cos(roll * 0.5f));
            statusLabel!.SetText($"V/S {climbRateMethod():+0.0;-0.0;0.0} m/s   SLIP {sideslipMethod():+0.0;-0.0;0.0} deg");

            //更新空速与高度刻度
            //换算显示单位：空速 m/s → kt，高度 m → ft
            float airspeed = airspeedMethod() * 1.943844f;
            float altitude = altitudeMethod() * 3.280840f;
            speedLabel!.SetText($"{airspeed:F0}");
            altitudeLabel!.SetText($"{altitude:F0}");
            altitudeLabel.SetFontSize(altitude >= 10000 || altitude <= -1000 ? 12 : 16);
            for (int tapeIndex = 0; tapeIndex < 2; ++tapeIndex)
            {
                bool altitudeTape = tapeIndex == 1;
                List<(Image mark, Text label, UILayout markLayout, UILayout labelLayout)> ticks = altitudeTape ? altitudeTicks : speedTicks;
                float current = altitudeTape ? altitude : airspeed;
                int step = altitudeTape ? 10 : 5;
                int start = (int)MathF.Floor(current / step) * step - (altitudeTape ? 50 : 30);
                UILayout scaleLayout = (altitudeTape ? altitudeTicksLayout : speedTicksLayout)!;
                scaleLayout.SetOffset(new vector2(0, (start - current) * (altitudeTape ? 2 : 4) + 1));
                for (int index = 0; index < ticks.Count; ++index)
                {
                    int value = start + index * step;
                    bool visible = altitudeTape || value >= 0;
                    bool major = value % (step * 2) == 0;
                    (Image mark, Text label, UILayout markLayout, UILayout labelLayout) = ticks[index];
                    if (mark.IsRuntimeHidden == visible)
                    {
                        mark.SetRuntimeVisible(visible, this);
                    }
                    markLayout.SetSizeDelta(new vector2(major ? 10 : 5, 1));
                    label.SetText(visible && major ? $"{value}" : "");
                    label.SetFontSize(altitudeTape && (altitude >= 10000 || altitude <= -1000) ? 9 : 11);
                }
            }

            //更新航向刻度与当前值
            float heading = (headingMethod() % 360 + 360) % 360;
            float visibleDegrees = headingWidth < 240 ? 40 : 80;
            int headingStart = (int)MathF.Floor(heading / 5) * 5 - 45;
            headingTicksLayout!.SetOffset(new vector2((headingStart - heading) * headingWidth / visibleDegrees, 0));
            for (int index = 0; index < headingTicks.Count; ++index)
            {
                int value = headingStart + index * 5;
                bool major = value % 10 == 0;
                (Image mark, Text label, UILayout markLayout, UILayout labelLayout) = headingTicks[index];
                markLayout.SetSizeDelta(new vector2(1, major ? 9 : 5));
                int degrees = (value % 360 + 360) % 360;
                label.SetText(major ? degrees switch { 0 => "N", 90 => "E", 180 => "S", 270 => "W", _ => $"{degrees:D3}" } : "");
            }
            headingLabel!.SetText($"{((int)MathF.Round(heading) + 360) % 360:D3} deg");

            //同步滑块与键盘控制状态
            float trim = trimMethod();
            float flaps = flapTargetMethod();
            float throttle = throttleMethod();
            float brake = Math.Clamp(brakeField.Value, 0, 1);
            brakeSlider!.SetValue(brake, false);
            trimSlider!.SetValue(trim, false);
            flapSlider!.SetValue(flaps, false);
            throttleSlider!.SetValue(throttle, false);
            brakeLabel!.SetText($"BRAKE  {brake * 100:F0}%");
            trimLabel!.SetText($"TRIM  {trim * 100:+0;-0;0}%");
            flapLabel!.SetText($"FLAPS  {flaps * 100:F0}% / {flapMethod() * 100:F0}%");
            throttleLabel!.SetText($"THROTTLE  {throttle * 100:F0}%");
            bool showForces = showForcesField.Value;
            forcesLabel!.SetText(showForces ? "FORCE VECTORS  ON  (F)" : "FORCE VECTORS  OFF  (F)");
            forcesLabel.SetTint(showForces ? ForceEnabledTint : ColorGrey);
            hudRoot.LocalActive = true;
        }
        catch (NativeCallException)
        {
            hudRoot.LocalActive = false;
        }
    }

    //清理运行时 UI 节点和引用
    private void OnEnd()
    {
        if (controlsCanvas is { IsValid: true })
        {
            controlsCanvas.Destroy();
        }
        controlsCanvas = hudRoot = bankPointer = null;
        canvas = null;
        pfdLayout = leftControlsPanel = rightControlsPanel = null;
        attitudeLayout = speedTapeLayout = altitudeTapeLayout = headingTapeLayout = horizonLayout = null;
        statusLabel = speedLabel = altitudeLabel = headingLabel = forcesLabel = null;
        brakeLabel = trimLabel = flapLabel = throttleLabel = null;
        speedTicksLayout = altitudeTicksLayout = headingTicksLayout = null;
        brakeSlider = trimSlider = flapSlider = throttleSlider = null;
        speedTicks.Clear();
        altitudeTicks.Clear();
        headingTicks.Clear();
        viewportSize = default;
    }
}
