using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

[StructLayout(LayoutKind.Sequential, Pack = 8)]
public readonly unsafe struct NativeBindingSlice(byte* data, int length)
{
    public readonly byte* Data = data;
    public readonly int Length = length;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
public readonly unsafe struct NativeBindingBuffer
{
    public readonly byte* Data;
    public readonly int Length;
    public ReadOnlySpan<byte> Span => Length >= 0 && (Length == 0 || Data != null)
        ? new ReadOnlySpan<byte>(Data, Length) : throw new InvalidDataException("Invalid native binding buffer.");
}

public enum NativeBindingStatus : uint { Ok, InvalidObject, InvalidArgument, InvocationFailed, TypeMismatch }

/// <summary>为生成的动态值编码提供明确所有权的缓冲区。</summary>
public sealed class NativeBindingWriter
{
    private readonly MemoryStream stream = new();
    public void Scalar<T>(T value) where T : unmanaged
    {
        Span<byte> bytes = stackalloc byte[System.Runtime.CompilerServices.Unsafe.SizeOf<T>()];
        MemoryMarshal.Write(bytes, in value); stream.Write(bytes);
    }
    public void Text(string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        byte[] bytes = Encoding.UTF8.GetBytes(value); Scalar(bytes.Length); stream.Write(bytes);
    }
    public byte[] ToArray() => stream.ToArray();
}

/// <summary>读取生成值快照，并检查所有长度与读取边界。</summary>
public ref struct NativeBindingReader(ReadOnlySpan<byte> data)
{
    private ReadOnlySpan<byte> remaining = data;
    public T Scalar<T>() where T : unmanaged
    {
        int size = System.Runtime.CompilerServices.Unsafe.SizeOf<T>();
        if (remaining.Length < size) throw new InvalidDataException("Truncated native binding value.");
        T result = MemoryMarshal.Read<T>(remaining); remaining = remaining[size..]; return result;
    }
    public int Count()
    {
        int length = Scalar<int>();
        if (length < 0 || length > remaining.Length) throw new InvalidDataException("Invalid native binding count.");
        return length;
    }
    public string Text()
    {
        int length = Count(); string result = Encoding.UTF8.GetString(remaining[..length]); remaining = remaining[length..]; return result;
    }
    public void Complete()
    {
        if (!remaining.IsEmpty) throw new InvalidDataException("Trailing native binding bytes.");
    }
}

/// <summary>标识生成的原生类型包装；不沿托管继承链隐式继承。</summary>
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class NativeBindingAttribute(string typeName) : Attribute
{
    public string TypeName { get; } = typeName;
}
