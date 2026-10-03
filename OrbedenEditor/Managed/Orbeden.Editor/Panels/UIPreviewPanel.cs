using System;
using Orbeden;

namespace OrbedenEditor;

/// <summary>
/// UI 预览面板。它只认公共合同 IUIPreviewProvider：分辨率、可见性与输入注入都经注册表转发，
/// 不引用任何 UI 运行时类型。像素来自离屏画布的输出纹理，直接画在面板上；
/// 屏幕画布没有独立纹理，只以文字说明像素在场景面板里。
/// </summary>
internal sealed class UIPreviewPanel : EditorPanel
{
    //输入框直接编辑字符串，应用时再解析成整数。
    private string width = "1280";
    private string height = "720";
    private bool visible = true;
    private bool injectInput;
    private DateTime lastRepaint = DateTime.UtcNow;

    //注入用的指针状态：按下这一帧发 Down，拖动发 Move，松开发 Up。
    private bool pointerDown;
    private vector2 lastPointer;

    /// <summary>面板登记信息。</summary>
    public override EditorPanelInfo Info { get; } = new(
        "ui_preview",
        "UI Preview",
        defaultVisible: false,
        defaultSize: new vector2(420.0f, 320.0f),
        defaultDock: PanelDockPlacement.Right,
        defaultDockRatio: 0.3f,
        order: 60);

    /// <summary>面板显示：把提供者切到可见。</summary>
    public override void OnShown()
    {
        UIPreviewRegistry.GetProvider()?.SetVisible(true);
    }

    /// <summary>面板隐藏：释放输入占有，别让指针留在看不见的界面上。</summary>
    public override void OnHidden()
    {
        UIPreviewRegistry.GetProvider()?.CancelInput();
    }

    /// <summary>绘制面板内容。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        IUIPreviewProvider? provider = UIPreviewRegistry.GetProvider();
        if (provider == null)
        {
            EditorGUI.Label("没有注册 UI 预览提供者。");
            return;
        }

        EditorGUI.Label("预览分辨率");
        EditorGUI.InputText("宽", ref width);
        EditorGUI.InputText("高", ref height);
        if (EditorGUI.Button("应用分辨率"))
        {
            //分辨率变化会让旧命中快照失效，由提供者负责。
            if (int.TryParse(width, out int parsedWidth) && int.TryParse(height, out int parsedHeight))
                provider.SetResolution(parsedWidth, parsedHeight);
            EditorApplication.RequestRepaint();
        }

        bool nextVisible = EditorGUI.ToggleButton(visible ? "预览：开" : "预览：关", visible);
        if (nextVisible != visible)
        {
            visible = nextVisible;
            provider.SetVisible(visible);
        }

        //只有 Play 才注入交互：编辑状态只显示布局与渲染。
        bool canInject = EditorApplication.IsPlaying;
        if (!canInject && injectInput)
        {
            injectInput = false;
            provider.CancelInput();
        }
        //提供者按这个开关决定收不收指针，面板只管把它同步过去。
        provider.SetInputEnabled(canInject && injectInput);
        EditorGUI.BeginDisabled(!canInject);
        try
        {
            bool nextInject = EditorGUI.ToggleButton(injectInput ? "注入交互：开" : "注入交互：关", injectInput);
            if (nextInject != injectInput)
            {
                injectInput = nextInject;
                provider.SetInputEnabled(canInject && injectInput);
                if (!injectInput) provider.CancelInput();
            }
        }
        finally
        {
            EditorGUI.EndDisabled();
        }

        EditorGUI.Separator();
        //尺寸以提供者的当前值为准：输入框里可能有还没应用的文本。
        (int appliedWidth, int appliedHeight) = provider.Resolution;
        float displayWidth = Math.Max(appliedWidth, 16);
        float displayHeight = Math.Max(appliedHeight, 16);

        //有独立纹理时直接显示：DrawTexture 内部解析对象身份，面板拿不到 GL 句柄。
        Texture2D? texture = provider.GetPreviewTexture();
        if (texture == null)
        {
            //屏幕画布没有独立纹理，它的像素由 UIRenderer 画进窗口缓冲，只能在场景面板里看。
            ReleasePointer(provider);
            EditorGUI.Label("屏幕画布没有独立纹理：像素在场景面板里。离屏画布会在这里出图。");
            EditorGUI.Label($"当前分辨率：{displayWidth:0} × {displayHeight:0}");
            return;
        }

        vector2 display = new(displayWidth, displayHeight);
        vector2 surface = NativeEditorGUI.GetCursorScreenPos();
        EditorGUI.DrawTexture(texture, display);

        //纹理之上再盖一层等大的不可见拖动区：它不画东西，只负责把这块矩形内的鼠标
        //变成"按下/拖动中/悬停"，注入关闭时保留它但不转发事件。
        NativeEditorGUI.SetCursorScreenPos(surface);
        NativeEditorGUI.InvisibleButton("##ui_preview_surface", display);
        if (canInject && injectInput) ForwardPointer(provider, surface, display);
        else ReleasePointer(provider);

        //半透明的预乘输出在 ImGui 里按直通混合显示，颜色会偏暗，这是预览的已知表现。
        RequestPeriodicRepaint(ref lastRepaint, 0.1);
    }

    //把这块矩形内的鼠标转成预览指针事件。
    private void ForwardPointer(IUIPreviewProvider provider, vector2 surface, vector2 display)
    {
        vector2 mouse = NativeEditorGUI.GetMousePos();
        vector2 local = new(mouse.x - surface.x, mouse.y - surface.y);
        bool active = NativeEditorGUI.IsItemActive();
        bool hovered = NativeEditorGUI.IsItemHovered();
        //离开矩形时把基准点跟过去：再进来时第一帧不会算出一段假位移。
        if (!active && !hovered) lastPointer = local;

        //先判"按下过没有"：松开那一帧鼠标仍停在矩形上，悬停分支会把它误判成移动，
        //那样 Up 永远发不出去，UI 侧会一直攥着这次捕获。
        if (pointerDown)
        {
            if (active)
            {
                SendMove(provider, local);
            }
            else
            {
                pointerDown = false;
                lastPointer = local;
                Send(provider, PreviewPointerPhase.Up, local, default);
            }
        }
        else if (active)
        {
            //活动条目意味着按下就落在矩形里，这一帧就是 Down。
            pointerDown = true;
            lastPointer = local;
            Send(provider, PreviewPointerPhase.Down, local, default);
        }
        else if (hovered)
        {
            //没按下也要发 Move：悬停高亮与滑动这类交互靠它。
            SendMove(provider, local);
        }

        //ImGui 的滚轮向上为正，UI 的约定是向上为正，这里差一个符号。
        float wheel = NativeEditorGUI.GetMouseWheel();
        if (hovered && wheel != 0.0f) Send(provider, PreviewPointerPhase.Scroll, local, new vector2(0.0f, -wheel));
    }

    //拖动中的移动：位移为零就不发，免得每帧都喂一条空事件。
    private void SendMove(IUIPreviewProvider provider, vector2 local)
    {
        vector2 delta = new(local.x - lastPointer.x, local.y - lastPointer.y);
        lastPointer = local;
        if (delta.x != 0.0f || delta.y != 0.0f) Send(provider, PreviewPointerPhase.Move, local, delta);
    }

    //注入关闭、纹理消失或面板隐藏时收回指针占有。
    private void ReleasePointer(IUIPreviewProvider provider)
    {
        if (!pointerDown) return;
        pointerDown = false;
        provider.CancelInput();
    }

    private static void Send(IUIPreviewProvider provider, uint phase, vector2 position, vector2 delta)
    {
        //时间戳只用于节流与双击判定，这里给单调递增的秒数即可。
        double timestamp = Environment.TickCount64 / 1000.0;
        provider.InjectPointer(new PreviewPointer(0, phase, 0, position, delta, timestamp));
    }
}
