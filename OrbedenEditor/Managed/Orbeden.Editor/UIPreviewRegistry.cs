using System;
using Orbeden;

namespace OrbedenEditor;

/// <summary>注入预览的一次指针事件；坐标是预览目标内的逻辑坐标。</summary>
public readonly record struct PreviewPointer(
    uint pointerId, uint phase, uint button, vector2 position, vector2 delta, double timestamp);

/// <summary>
/// 注入指针的阶段常量。数值与 UI 运行时的 UIPointerPhase 一一对应，
/// 面板因此不必引用运行时类型——这是包与编辑器之间唯一的耦合点，改动要两边一起改。
/// </summary>
public static class PreviewPointerPhase
{
    /// <summary>按下。</summary>
    public const uint Down = 0;

    /// <summary>移动。</summary>
    public const uint Move = 1;

    /// <summary>抬起。</summary>
    public const uint Up = 2;

    /// <summary>取消。</summary>
    public const uint Cancel = 3;

    /// <summary>滚轮；增量放在 delta 里。</summary>
    public const uint Scroll = 4;
}

/// <summary>
/// UI 预览的提供者合同。编辑器面板只认这个接口，不认识任何 UI 内部类型，
/// 因此运行时包与编辑器面板之间没有反向引用。
/// </summary>
public interface IUIPreviewProvider
{
    /// <summary>预览纹理；没有可显示的内容时为空。</summary>
    Texture2D? GetPreviewTexture();

    /// <summary>当前生效的预览分辨率；面板按它摆放像素，而不是按输入框里的文本。</summary>
    (int Width, int Height) Resolution { get; }

    /// <summary>设置预览分辨率；旧命中快照随分辨率变化失效。</summary>
    void SetResolution(int width, int height);

    /// <summary>设置预览是否可见；隐藏时停止更新目标并释放输入占有。</summary>
    void SetVisible(bool visible);

    /// <summary>设置是否接受注入的交互；关闭时提供者不再把指针喂给 UI。</summary>
    void SetInputEnabled(bool enabled);

    /// <summary>注入一次指针事件。</summary>
    void InjectPointer(in PreviewPointer input);

    /// <summary>取消输入：收回全部指针占有与捕获。</summary>
    void CancelInput();
}

/// <summary>预览提供者的注册表。面板通过它拿到当前提供者，不直接引用实现。</summary>
public static class UIPreviewRegistry
{
    private static IUIPreviewProvider? provider;

    /// <summary>当前提供者；没有注册时为空。</summary>
    public static IUIPreviewProvider? GetProvider() => provider;

    /// <summary>设置提供者；替换之前先让旧提供者取消输入。</summary>
    public static void SetProvider(IUIPreviewProvider? value)
    {
        if (ReferenceEquals(provider, value)) return;
        //旧提供者可能还占着指针，先让它放手。
        provider?.CancelInput();
        provider = value;
    }
}
