using Orbeden;
using System.Runtime.InteropServices;

namespace OrbedenEditor;

/// <summary>统一定义编辑器窗口、面板与控件外观。</summary>
public sealed record EditorTheme
{
    private static EditorTheme current = new();

    public static EditorTheme Current
    {
        get => current;
        set
        {
            ArgumentNullException.ThrowIfNull(value);
            foreach (float size in new[] { value.PaddingX, value.PaddingY, value.SpacingX, value.SpacingY,
                value.FramePaddingX, value.FramePaddingY, value.SplitterSize, value.CornerRadius })
                if (!float.IsFinite(size) || size < 0 || size > 100)
                    throw new ArgumentOutOfRangeException(nameof(value), "Theme sizes must be finite and between 0 and 100.");
            current = value;
            ApplyCurrent();
        }
    }

    //颜色按 HTML 习惯写成 0xRRGGBB，需要半透明时用第二个参数
    public color Background { get; init; } = ThemeColor.FromHex(0x242424);
    public color Text { get; init; } = ThemeColor.FromHex(0xE8E8E8);
    public color Border { get; init; } = ThemeColor.FromHex(0x505050);
    public color Header { get; init; } = ThemeColor.FromHex(0x383838);
    public color Control { get; init; } = ThemeColor.FromHex(0x505050);
    public color Hovered { get; init; } = ThemeColor.FromHex(0x385676);
    public color Active { get; init; } = ThemeColor.FromHex(0x9184EE);
    public float PaddingX { get; init; } = 6;
    public float PaddingY { get; init; } = 6;
    public float SpacingX { get; init; } = 6;
    public float SpacingY { get; init; } = 4;
    public float FramePaddingX { get; init; } = 6;
    public float FramePaddingY { get; init; } = 4;
    public float SplitterSize { get; init; } = 5;
    /// <summary>面板圆角半径。</summary>
    public float CornerRadius { get; init; } = 6;

    //日志级别配色只由托管侧使用，不进跨语言的主题结构
    /// <summary>Console 面板的信息级颜色。</summary>
    public color LogInfo { get; init; } = ThemeColor.FromHex(0xC8C8C8);
    /// <summary>Console 面板的警告级颜色。</summary>
    public color LogWarning { get; init; } = ThemeColor.FromHex(0xE0C36A);
    /// <summary>Console 面板的错误级颜色。</summary>
    public color LogError { get; init; } = ThemeColor.FromHex(0xE06C6C);
    /// <summary>Console 列表的隔行底色：面板底色算黑，这里用深灰把每一行分开。</summary>
    public color ConsoleRowStripe { get; init; } = ThemeColor.FromHex(0x313131);

    //性能剖析的分类配色，顺序与 ProfileCategory 一致
    /// <summary>Profiler 面板的未分类/其它未计时间颜色，浅灰；要压在深灰的限帧等待上，取亮一档。</summary>
    public color ProfileOther { get; init; } = ThemeColor.FromHex(0xBFBFBF);
    /// <summary>Profiler 面板的脚本分类颜色。</summary>
    public color ProfileScript { get; init; } = ThemeColor.FromHex(0x6C9BE0);
    /// <summary>Profiler 面板的渲染分类颜色。</summary>
    public color ProfileRender { get; init; } = ThemeColor.FromHex(0xE0C36A);
    /// <summary>Profiler 面板的物理分类颜色。</summary>
    public color ProfilePhysics { get; init; } = ThemeColor.FromHex(0xE09357);
    /// <summary>Profiler 面板的文件分类颜色。</summary>
    public color ProfileFileIO { get; init; } = ThemeColor.FromHex(0x5FBFA6);
    /// <summary>Profiler 面板的编辑器分类颜色，用洋红与脚本蓝拉开距离，一像素宽时也分得清。</summary>
    public color ProfileEditor { get; init; } = ThemeColor.FromHex(0xD98BD9);
    /// <summary>Profiler 面板的限帧等待颜色，深灰：主动等节拍而不是空闲。</summary>
    public color ProfileWait { get; init; } = ThemeColor.FromHex(0x3A3A3A);

    //提交共享主题，原生层在下一帧开始时应用
    internal static void ApplyCurrent()
    {
        EditorThemeData data = new()
        {
            Background = current.Background, Text = current.Text, Border = current.Border,
            Header = current.Header, Control = current.Control, Hovered = current.Hovered, Active = current.Active,
            PaddingX = current.PaddingX, PaddingY = current.PaddingY,
            SpacingX = current.SpacingX, SpacingY = current.SpacingY,
            FramePaddingX = current.FramePaddingX, FramePaddingY = current.FramePaddingY,
            SplitterSize = current.SplitterSize, CornerRadius = current.CornerRadius
        };
        NativeEditorGUI.SetTheme(data);
    }
}

/// <summary>把主题色写成 HTML 习惯的 0xRRGGBB 字面量。</summary>
internal static class ThemeColor
{
    /// <summary>按 0xRRGGBB 与可选透明度构造引擎颜色。</summary>
    public static color FromHex(uint rgb, float alpha = 1.0f)
    {
        return new color(
            ((rgb >> 16) & 0xFFu) / 255.0f,
            ((rgb >> 8) & 0xFFu) / 255.0f,
            (rgb & 0xFFu) / 255.0f,
            alpha);
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EditorThemeData
{
    public color Background, Text, Border, Header, Control, Hovered, Active;
    public float PaddingX, PaddingY, SpacingX, SpacingY, FramePaddingX, FramePaddingY, SplitterSize, CornerRadius;
}
