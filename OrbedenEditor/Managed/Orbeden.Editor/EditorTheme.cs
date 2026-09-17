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
