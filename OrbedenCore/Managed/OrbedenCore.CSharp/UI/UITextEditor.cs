using System;
using System.Collections.Generic;
using System.Text;

namespace Orbeden;

/// <summary>
/// 单行文本编辑模型。对外一律用 Unicode 标量下标，内部改动走 UTF-16 映射，
/// 因此任何操作都不会切断代理对。组合（输入法）独立于实际文本，只有提交才落到文本上。
/// </summary>
public sealed class UITextEditor
{
    /// <summary>非法代理项替换成的标量。</summary>
    public const uint ReplacementScalar = 0xFFFD;

    private string text = string.Empty;
    //标量下标：光标位置与选区锚点；两者相等表示没有选区。
    private int caret;
    private int selectionAnchor;
    //组合状态：令牌、组合文本与组合内的光标（标量）。
    private ulong compositionToken;
    private string compositionText = string.Empty;
    private int compositionCaret;
    private bool composing;

    /// <summary>文本变化时触发；由控件转成事件。参数是新文本。</summary>
    public event Action<string>? TextChanged;

    /// <summary>最大长度，按 Unicode 标量计数；0 表示不限制。</summary>
    public int MaxLength { get; set; }

    /// <summary>只读：允许选区与复制，拒绝改值、剪切与粘贴。</summary>
    public bool ReadOnly { get; set; }

    /// <summary>当前文本。</summary>
    public string GetText() => text;

    /// <summary>文本的标量数量。</summary>
    public int ScalarCount => CountScalars(text);

    /// <summary>光标位置（标量下标）。</summary>
    public int GetCaret() => caret;

    /// <summary>选区起点（标量下标）。</summary>
    public int GetSelectionStart() => Math.Min(caret, selectionAnchor);

    /// <summary>选区终点（标量下标）。</summary>
    public int GetSelectionEnd() => Math.Max(caret, selectionAnchor);

    /// <summary>是否存在选区。</summary>
    public bool HasSelection() => caret != selectionAnchor;

    /// <summary>是否处于组合状态。</summary>
    public bool HasComposition => composing;

    /// <summary>组合文本；没有组合时为空串。</summary>
    public string GetCompositionText() => composing ? compositionText : string.Empty;

    /// <summary>组合内的光标（标量）；没有组合时为 0。</summary>
    public int GetCompositionCaret() => composing ? compositionCaret : 0;

    /// <summary>组合令牌；没有组合时为 0。</summary>
    public ulong GetCompositionToken() => composing ? compositionToken : 0;

    /// <summary>整体设置文本：归一、按最大长度截断，光标落到末尾，选区清除。</summary>
    public void SetText(string value)
    {
        string normalized = Normalize(value);
        normalized = Truncate(normalized, MaxLength);
        if (normalized == text)
        {
            //值没变也要把光标收到有效范围里。
            caret = Math.Clamp(caret, 0, ScalarCount);
            selectionAnchor = caret;
            return;
        }

        text = normalized;
        caret = ScalarCount;
        selectionAnchor = caret;
        TextChanged?.Invoke(text);
    }

    /// <summary>设置选区；两端都是标量下标，越界夹紧，顺序自动理顺。</summary>
    public void SetSelection(int start, int end)
    {
        int count = ScalarCount;
        int clampedStart = Math.Clamp(Math.Min(start, end), 0, count);
        int clampedEnd = Math.Clamp(Math.Max(start, end), 0, count);
        caret = clampedEnd;
        selectionAnchor = clampedStart;
    }

    /// <summary>移动光标；select 为真时保留锚点形成选区。</summary>
    public void MoveCaret(int delta, bool select)
    {
        int count = ScalarCount;
        int next = Math.Clamp(caret + delta, 0, count);
        caret = next;
        if (!select) selectionAnchor = next;
    }

    /// <summary>把光标移到指定标量位置；select 为真时保留锚点。</summary>
    public void SetCaret(int position, bool select)
    {
        int next = Math.Clamp(position, 0, ScalarCount);
        caret = next;
        if (!select) selectionAnchor = next;
    }

    /// <summary>用给定文本替换当前选区；返回是否真的改了文本。</summary>
    public bool ReplaceSelection(string value)
    {
        if (ReadOnly) return false;
        string normalized = Normalize(value);
        return ApplyEdit(normalized);
    }

    /// <summary>Backspace：有选区时删选区，否则删光标前一个标量。</summary>
    public bool DeleteBackward()
    {
        if (ReadOnly) return false;
        if (HasSelection()) return ApplyEdit(string.Empty);

        int start = GetSelectionStart();
        if (start <= 0) return false;
        return ApplyEdit(string.Empty, start - 1, caret);
    }

    /// <summary>Delete：有选区时删选区，否则删光标后一个标量。</summary>
    public bool DeleteForward()
    {
        if (ReadOnly) return false;
        if (HasSelection()) return ApplyEdit(string.Empty);

        int end = GetSelectionEnd();
        if (end >= ScalarCount) return false;
        return ApplyEdit(string.Empty, GetSelectionStart(), end + 1);
    }

    /// <summary>全选。</summary>
    public void SelectAll() => SetSelection(0, ScalarCount);

    /// <summary>取当前选区的文本；没有选区时返回空串。</summary>
    public string GetSelectedText()
    {
        if (!HasSelection()) return string.Empty;
        return Substring(GetSelectionStart(), GetSelectionEnd() - GetSelectionStart());
    }

    /// <summary>开始输入法组合；同一个令牌重复开始无副作用。</summary>
    public void BeginComposition(ulong token)
    {
        if (composing && compositionToken == token) return;
        composing = true;
        compositionToken = token;
        compositionText = string.Empty;
        compositionCaret = 0;
    }

    /// <summary>更新组合内容；令牌不符或没有组合时忽略。</summary>
    public void UpdateComposition(ulong token, string value, int compositionCaretValue)
    {
        if (!composing || compositionToken != token) return;
        compositionText = Normalize(value);
        compositionCaret = Math.Clamp(compositionCaretValue, 0, CountScalars(compositionText));
    }

    /// <summary>
    /// 提交组合：只有这时才替换选区一次。成功后令牌作废，重复提交被忽略。
    /// 返回是否真的改了文本。
    /// </summary>
    public bool CommitComposition(ulong token, string value)
    {
        if (!composing || compositionToken != token) return false;

        composing = false;
        compositionToken = 0;
        compositionText = string.Empty;
        compositionCaret = 0;

        if (ReadOnly) return false;
        return ApplyEdit(Normalize(value));
    }

    /// <summary>取消组合：不改变实际文本。</summary>
    public void CancelComposition()
    {
        composing = false;
        compositionToken = 0;
        compositionText = string.Empty;
        compositionCaret = 0;
    }

    /// <summary>
    /// 归一化：非法代理项替换成 U+FFFD，CRLF/CR/LF/Tab 一律变成空格——
    /// 单行输入框里换行与制表符没有意义。
    /// </summary>
    public static string Normalize(string? value)
    {
        if (string.IsNullOrEmpty(value)) return string.Empty;

        string source = value!;
        StringBuilder builder = new(source.Length);
        for (int index = 0; index < source.Length; ++index)
        {
            char current = source[index];
            if (current == '\r')
            {
                if (index + 1 < source.Length && source[index + 1] == '\n') ++index;
                builder.Append(' ');
                continue;
            }
            if (current == '\n' || current == '\t')
            {
                builder.Append(' ');
                continue;
            }
            if (char.IsHighSurrogate(current) && index + 1 < source.Length && char.IsLowSurrogate(source[index + 1]))
            {
                builder.Append(current).Append(source[index + 1]);
                ++index;
                continue;
            }
            if (char.IsSurrogate(current))
            {
                builder.Append(char.ConvertFromUtf32((int)ReplacementScalar));
                continue;
            }
            builder.Append(current);
        }
        return builder.ToString();
    }

    /// <summary>按标量数量截断；maxScalars 为 0 表示不限制，绝不切断代理对。</summary>
    public static string Truncate(string value, int maxScalars)
    {
        if (maxScalars <= 0 || string.IsNullOrEmpty(value)) return value ?? string.Empty;

        //按标量边界截断：超出的那个标量必须整块丢掉，不能只切掉它的高位代理。
        int scalars = 0;
        for (int index = 0; index < value.Length;)
        {
            int scalarStart = index;
            index += char.IsHighSurrogate(value[index]) && index + 1 < value.Length && char.IsLowSurrogate(value[index + 1])
                ? 2
                : 1;
            ++scalars;
            if (scalars > maxScalars) return value.Substring(0, scalarStart);
        }
        return value;
    }

    /// <summary>统计标量数量。</summary>
    public static int CountScalars(string value)
    {
        if (string.IsNullOrEmpty(value)) return 0;

        int count = 0;
        for (int index = 0; index < value.Length; ++index)
        {
            //代理对算一个标量：跳过低位后仍然只加一次。
            if (char.IsHighSurrogate(value[index]) && index + 1 < value.Length && char.IsLowSurrogate(value[index + 1])) ++index;
            ++count;
        }
        return count;
    }

    /// <summary>标量下标换算成 UTF-16 下标；越界夹紧到字符串末尾。</summary>
    public static int ScalarToUtf16(string value, int scalarIndex)
    {
        if (string.IsNullOrEmpty(value) || scalarIndex <= 0) return 0;

        int scalars = 0;
        for (int index = 0; index < value.Length; ++index)
        {
            if (scalars == scalarIndex) return index;
            if (char.IsHighSurrogate(value[index]) && index + 1 < value.Length && char.IsLowSurrogate(value[index + 1])) ++index;
            ++scalars;
        }
        return value.Length;
    }

    /// <summary>按标量下标取子串；越界夹紧。</summary>
    public static string Substring(string value, int scalarStart, int scalarCount)
    {
        if (string.IsNullOrEmpty(value) || scalarCount <= 0) return string.Empty;
        int start = ScalarToUtf16(value, Math.Max(0, scalarStart));
        int end = ScalarToUtf16(value, Math.Max(0, scalarStart) + scalarCount);
        return value.Substring(start, end - start);
    }

    private string Substring(int scalarStart, int scalarCount) => Substring(text, scalarStart, scalarCount);

    //把编辑落到文本上：替换 [replaceStart, replaceEnd) 标量区间并移动光标。
    private bool ApplyEdit(string inserted, int replaceStart = -1, int replaceEnd = -1)
    {
        int start = replaceStart >= 0 ? replaceStart : GetSelectionStart();
        int end = replaceEnd >= 0 ? replaceEnd : GetSelectionEnd();

        int count = ScalarCount;
        start = Math.Clamp(start, 0, count);
        end = Math.Clamp(end, start, count);

        int insertedScalars = CountScalars(inserted);
        //按最大长度截断插入内容：替换区间的长度算作可用配额。
        if (MaxLength > 0)
        {
            int quota = MaxLength - (count - (end - start));
            if (quota <= 0)
            {
                //一个字符都放不下：只做删除。
                inserted = string.Empty;
                insertedScalars = 0;
            }
            else if (insertedScalars > quota)
            {
                inserted = Truncate(inserted, quota);
                insertedScalars = CountScalars(inserted);
            }
        }

        int utf16Start = ScalarToUtf16(text, start);
        int utf16End = ScalarToUtf16(text, end);
        string next = string.Concat(text.AsSpan(0, utf16Start), inserted, text.AsSpan(utf16End));
        if (next == text)
        {
            //文本没变也要把光标放到插入点之后。
            caret = start + insertedScalars;
            selectionAnchor = caret;
            return false;
        }

        text = next;
        caret = start + insertedScalars;
        selectionAnchor = caret;
        TextChanged?.Invoke(text);
        return true;
    }
}
