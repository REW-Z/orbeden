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

    //颜色使用 0xAABBGGRR 排列
    public uint Background { get; init; } = 0xff242424;
    public uint Text { get; init; } = 0xffe8e8e8;
    public uint Border { get; init; } = 0xff505050;
    /// <summary>面板最外层描边色，暂定紫色便于调整。</summary>
    public uint PanelOutline { get; init; } = 0xffff00ff;
    public uint Header { get; init; } = 0xff383838;
    public uint Control { get; init; } = 0xff505050;
    public uint Hovered { get; init; } = 0xff765638;
    public uint Active { get; init; } = 0xff9c683c;
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
            PanelOutline = current.PanelOutline,
            Header = current.Header, Control = current.Control, Hovered = current.Hovered, Active = current.Active,
            PaddingX = current.PaddingX, PaddingY = current.PaddingY,
            SpacingX = current.SpacingX, SpacingY = current.SpacingY,
            FramePaddingX = current.FramePaddingX, FramePaddingY = current.FramePaddingY,
            SplitterSize = current.SplitterSize, CornerRadius = current.CornerRadius
        };
        NativeEditorGUI.SetTheme(data);
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EditorThemeData
{
    //颜色使用 0xAABBGGRR 排列
    public uint Background, Text, Border, PanelOutline, Header, Control, Hovered, Active;
    public float PaddingX, PaddingY, SpacingX, SpacingY, FramePaddingX, FramePaddingY, SplitterSize, CornerRadius;
}
