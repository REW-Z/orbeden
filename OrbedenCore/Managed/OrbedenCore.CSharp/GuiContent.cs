using System.Text;

namespace Orbeden;

/// <summary>由控件持有的显示文本；内容变化时更新原生绘制所需的 UTF-8 表示。</summary>
public sealed class GuiContent
{
    private string text = string.Empty;
    private byte[] utf8 = [];

    /// <summary>创建显示文本并准备其原生表示。</summary>
    public GuiContent(string? text = null) => Text = text ?? string.Empty;

    /// <summary>显示文本；相同内容不会重复编码。</summary>
    public string Text
    {
        get => text;
        set
        {
            string current = value ?? string.Empty;
            if (text == current) return;
            byte[] bytes = InteropText.EncodeUtf8(current);
            text = current;
            utf8 = bytes;
        }
    }

    /// <summary>只读的原生文本表示，内容修改后更新。</summary>
    public ReadOnlySpan<byte> Utf8 => utf8;
}
