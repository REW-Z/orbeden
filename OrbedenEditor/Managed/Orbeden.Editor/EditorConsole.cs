using System;
using System.IO;
using System.Text;

namespace OrbedenEditor;

/// <summary>编辑器日志级别，与原生 LogLevel 一一对应。</summary>
internal static class EditorLogLevel
{
    public const int Info = 0;
    public const int Warning = 1;
    public const int Error = 2;
}

/// <summary>编辑器日志通道：透传到操作系统控制台，同时写入原生保留窗口。</summary>
internal static class EditorConsole
{
    private static bool installed;
    private static LineForwardWriter? infoWriter;
    private static LineForwardWriter? warningWriter;
    private static LineForwardWriter? errorWriter;

    /// <summary>安装控制台转发，重复调用不生效。</summary>
    internal static void Install()
    {
        //二次安装会把上一个包装器当成原始 writer，导致每行输出两遍
        if (installed) return;
        installed = true;

        //控制台只有两个流却有三个级别：标准输出算信息，标准错误算警告，
        //错误级由 EditorConsole.Error 单独走一个包装器，避免整条流被降级成警告
        TextWriter standardOut = Console.Out;
        TextWriter standardError = Console.Error;

        infoWriter = new LineForwardWriter(standardOut, EditorLogLevel.Info);
        warningWriter = new LineForwardWriter(standardError, EditorLogLevel.Warning);
        errorWriter = new LineForwardWriter(standardError, EditorLogLevel.Error);

        Console.SetOut(infoWriter);
        Console.SetError(warningWriter);
    }

    /// <summary>写出信息日志。</summary>
    internal static void Info(string? message) => infoWriter?.WriteLine(message ?? string.Empty);

    /// <summary>写出警告日志。</summary>
    internal static void Warning(string? message) => warningWriter?.WriteLine(message ?? string.Empty);

    /// <summary>写出错误日志。</summary>
    internal static void Error(string? message) => errorWriter?.WriteLine(message ?? string.Empty);
}

/// <summary>把写入的文本透传给原始 writer，并按行送进原生日志保留窗口。</summary>
internal sealed class LineForwardWriter : TextWriter
{
    //单行上限，写入方一直不换行时强制成行，避免缓冲无界增长
    private const int PendingCapacity = 4096;

    private readonly TextWriter passthrough;
    private readonly int level;
    private readonly StringBuilder pending = new();
    private readonly object gate = new();

    //日志本身再触发写日志时直接放行，不再递归成行
    [ThreadStatic] private static bool insideForward;

    internal LineForwardWriter(TextWriter passthrough, int level)
    {
        this.passthrough = passthrough;
        this.level = level;
    }

    /// <summary>沿用原始 writer 的编码。</summary>
    public override Encoding Encoding => passthrough.Encoding;

    /// <summary>写入单个字符。</summary>
    public override void Write(char value)
    {
        passthrough.Write(value);
        Forward(value == '\n' ? "\n" : value.ToString());
    }

    /// <summary>写入字符串。</summary>
    public override void Write(string? value)
    {
        if (string.IsNullOrEmpty(value)) return;

        passthrough.Write(value);
        Forward(value);
    }

    /// <summary>写入字符串片段。</summary>
    public override void Write(char[] buffer, int index, int count)
    {
        if (buffer == null || count <= 0) return;

        passthrough.Write(buffer, index, count);
        Forward(new string(buffer, index, count));
    }

    /// <summary>写入一行。</summary>
    public override void WriteLine(string? value)
    {
        Write(value);
        Write(passthrough.NewLine);
    }

    /// <summary>刷出缓冲并透传。</summary>
    public override void Flush()
    {
        passthrough.Flush();

        lock (gate)
        {
            if (pending.Length > 0) EmitLine();
        }
    }

    //按换行切分并成行
    private void Forward(string value)
    {
        if (insideForward) return;
        insideForward = true;
        try
        {
            lock (gate)
            {
                int start = 0;
                while (start < value.Length)
                {
                    int newline = value.IndexOf('\n', start);
                    if (newline < 0)
                    {
                        pending.Append(value, start, value.Length - start);
                        if (pending.Length >= PendingCapacity) EmitLine();
                        return;
                    }

                    pending.Append(value, start, newline - start);
                    EmitLine();
                    start = newline + 1;
                }
            }
        }
        finally
        {
            insideForward = false;
        }
    }

    //把缓冲内容作为一条日志写入保留窗口
    private void EmitLine()
    {
        string line = pending.ToString();
        pending.Clear();

        //去掉 Windows 换行里留在行尾的回车
        if (line.Length > 0 && line[^1] == '\r') line = line[..^1];
        NativeEditorLog.Append(level, line);
    }
}
