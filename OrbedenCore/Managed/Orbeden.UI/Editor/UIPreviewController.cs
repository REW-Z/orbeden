using System;
using Orbeden;
using OrbedenEditor;

namespace OrbedenEditor;

/// <summary>
/// UI 预览控制器。它把预览分辨率与可见性写进画布视口，把注入的指针交给输入路由器，
/// 并在隐藏、失焦、换目标时取消输入占有。像素输出来自场景面板（UIRenderer 的实际绘制）。
/// </summary>
public sealed class UIPreviewController : IUIPreviewProvider
{
    /// <summary>进程内共享的实例；面板与编辑器启动都从这里取。</summary>
    public static UIPreviewController Shared { get; } = new();

    private int width = 1280;
    private int height = 720;
    private bool visible;
    private bool injectsInput;

    /// <summary>撤销时用的分辨率快照。</summary>
    private (int Width, int Height) previousResolution;

    /// <summary>预览分辨率。</summary>
    public (int Width, int Height) Resolution => (width, height);

    /// <summary>预览是否可见。</summary>
    public bool Visible => visible;

    /// <summary>是否注入交互；只有 Play 才打开。</summary>
    public bool InjectsInput => injectsInput;

    /// <summary>把控制器登记为当前提供者；重复调用无副作用。</summary>
    public static void Register() => UIPreviewRegistry.SetProvider(Shared);

    /// <summary>注销提供者。</summary>
    public static void Unregister() => UIPreviewRegistry.SetProvider(null);

    /// <summary>
    /// 预览纹理：离屏画布有自己的输出纹理，直接拿来显示；
    /// 屏幕画布画在窗口缓冲上，没有可取的纹理，面板会说明像素在场景面板里。
    /// </summary>
    public Texture2D? GetPreviewTexture()
    {
        UIWorldContext? context = UIWorldContext.Current;
        if (context == null) return null;

        foreach (Canvas canvas in context.Canvases)
        {
            if (canvas.GetRenderMode() != CanvasRenderMode.Offscreen) continue;
            Texture2D? output = canvas.GetOutputTexture();
            if (output != null) return output;
        }
        return null;
    }

    /// <summary>设置预览分辨率；旧命中快照随分辨率变化失效。</summary>
    public void SetResolution(int newWidth, int newHeight)
    {
        int clampedWidth = Math.Clamp(newWidth, 16, 8192);
        int clampedHeight = Math.Clamp(newHeight, 16, 8192);
        if (clampedWidth == width && clampedHeight == height) return;

        previousResolution = (width, height);
        width = clampedWidth;
        height = clampedHeight;
        //分辨率一变，按旧尺寸判出来的命中就作废。
        ApplyViewport();
        InvalidateHits();
    }

    /// <summary>设置可见性；隐藏时释放输入占有并停止驱动视口。</summary>
    public void SetVisible(bool value)
    {
        if (visible == value) return;
        visible = value;
        if (!visible)
        {
            //隐藏就放手，别让指针留在看不见的界面上。
            CancelInput();
            return;
        }
        ApplyViewport();
    }

    /// <summary>设置是否注入交互；编辑器文本框持焦点时应当关掉。</summary>
    public void SetInputEnabled(bool value)
    {
        if (injectsInput == value) return;
        injectsInput = value;
        if (!injectsInput) CancelInput();
    }

    /// <summary>注入一次指针事件；不可见或未开启注入时忽略。</summary>
    public void InjectPointer(in PreviewPointer input)
    {
        if (!visible || !injectsInput) return;

        UIWorldContext? context = UIWorldContext.Current;
        if (context == null) return;

        //预览坐标按窗口逻辑坐标喂给路由器：预览与窗口同尺寸时是一一对应的。
        UIPointerEvent mapped = new()
        {
            pointerId = input.pointerId,
            phase = (UIPointerPhase)input.phase,
            button = input.button,
            position = input.position,
            delta = input.delta,
            timestamp = input.timestamp,
            sequence = 0,
        };
        context.InputRouter.ProcessPointer(mapped);
    }

    /// <summary>取消输入：收回全部指针占有与捕获。</summary>
    public void CancelInput()
    {
        UIWorldContext? context = UIWorldContext.Current;
        if (context == null) return;
        context.InputRouter.Reset();
    }

    //把预览分辨率写进屏幕画布的视口；没有画布时什么都不做。
    private void ApplyViewport()
    {
        UIWorldContext? context = UIWorldContext.Current;
        if (context == null) return;

        vector2 size = new(width, height);
        foreach (Canvas canvas in context.Canvases)
        {
            if (canvas.GetRenderMode() != CanvasRenderMode.Overlay) continue;
            context.SetCanvasViewport(canvas, size);
        }
    }

    //分辨率变化后按旧尺寸算出来的命中不再可信。
    private static void InvalidateHits() => UIWorldContext.MarkAllInputDirty();
}
