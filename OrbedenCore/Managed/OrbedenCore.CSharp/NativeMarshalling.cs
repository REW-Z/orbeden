using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>与原生 StringId 对应的文本标识。</summary>
public readonly record struct StringId
{
    private readonly string? text;

    /// <summary>读取标识文本。</summary>
    public string Text => text ?? string.Empty;

    /// <summary>创建文本标识。</summary>
    public StringId(string? text) => this.text = string.IsNullOrEmpty(text) ? null : text;

    /// <summary>读取标识文本。</summary>
    public override string ToString() => Text;
}

/// <summary>读取动态属性和事件中的普通托管值。</summary>
public static class NativeValues
{
    private static readonly Dictionary<Type, NativeValueKind> kinds = new()
    {
        [typeof(void)] = NativeValueKind.Empty,
        [typeof(bool)] = NativeValueKind.Bool,
        [typeof(int)] = NativeValueKind.Int32,
        [typeof(uint)] = NativeValueKind.UInt32,
        [typeof(ulong)] = NativeValueKind.UInt64,
        [typeof(float)] = NativeValueKind.Float32,
        [typeof(string)] = NativeValueKind.String,
        [typeof(StringId)] = NativeValueKind.StringId,
        [typeof(vector2)] = NativeValueKind.Vector2,
        [typeof(vector3)] = NativeValueKind.Vector3,
        [typeof(color)] = NativeValueKind.Color,
        [typeof(quaternion)] = NativeValueKind.Quaternion,
        [typeof(EnsId)] = NativeValueKind.EnsId,
        [typeof(nint)] = NativeValueKind.Pointer,
        [typeof(nuint)] = NativeValueKind.Pointer,
    };
    /// <summary>读取普通托管值的原生类型分类。</summary>
    public static NativeValueKind GetValueKind(this object? value) => value == null ? NativeValueKind.Object : value is Array ? NativeValueKind.Array : GetKind(value.GetType());
    /// <summary>按精确类型读取动态值。</summary>
    public static bool TryGet<T>(this object? value, out T result)
    {
        if (value is T typed)
        {
            result = typed;
            return true;
        }

        if (typeof(T) == typeof(string) && value is StringId text)
        {
            string copy = text.ToString();
            result = Unsafe.As<string, T>(ref copy);
            return true;
        }

        if (typeof(T) == typeof(int) && (value is Object || value == null))
        {
            int id = (value as Object)?.InstanceId ?? 0;
            result = Unsafe.As<int, T>(ref id);
            return true;
        }

        result = default!;
        return false;
    }

    /// <summary>检查普通值是否匹配动态属性的声明分类。</summary>
    public static bool Matches(object? value, NativeValueKind kind) => kind == NativeValueKind.Array ? value is int or Array : value.GetValueKind() == kind;
    //映射托管类型到原生签名
    internal static NativeValueKind GetKind(Type type)
    {
        if (type.IsEnum)
        {
            type = Enum.GetUnderlyingType(type);
        }

        if (kinds.TryGetValue(type, out NativeValueKind kind))
        {
            return kind;
        }

        if (typeof(Object).IsAssignableFrom(type))
        {
            return NativeValueKind.Object;
        }

        if (type.IsSZArray)
        {
            NativeValueKind element = GetKind(type.GetElementType()!);
            if ((uint)element is> 0 and < 16 && element != NativeValueKind.Array)
            {
                return (NativeValueKind)((uint)NativeValueKind.Array | (uint)element << 16);
            }
        }

        return (NativeValueKind)uint.MaxValue;
    }
}

//单次调用拥有输入编码及固定句柄
internal ref struct NativeMarshallingScope
{
    private List<GCHandle>? pins;
    internal readonly bool HasPins => pins != null;

    //把返回存储交给本次调用的完成入口
    internal List<GCHandle>? TakePins()
    {
        List<GCHandle>? result = pins;
        pins = null;
        return result;
    }

    //固定本次调用使用的托管存储
    internal unsafe void* Pin(Array value)
    {
        GCHandle handle = GCHandle.Alloc(value, GCHandleType.Pinned);
        try
        {
            (pins ??= []).Add(handle);
        }
        catch
        {
            handle.Free();
            throw;
        }

        return (void*)handle.AddrOfPinnedObject();
    }

    //调用结束后释放全部固定句柄
    public void Dispose()
    {
        if (pins == null)
        {
            return;
        }

        foreach (GCHandle pin in pins)
        {
            pin.Free();
        }

        pins = null;
    }
}

//类型化参数只保存自身的原生布局，不附加动态标签
internal static unsafe class NativeMarshaller<T>
{
    internal static readonly NativeValueKind Kind = NativeValues.GetKind(typeof(T));
    internal static readonly int Size = NativeMarshaller.SizeOf(typeof(T));
    //封送输入值，标量保持原始位表示
    internal static void Write(void* output, T value, ref NativeMarshallingScope scope)
    {
        if (!RuntimeHelpers.IsReferenceOrContainsReferences<T>())
        {
            Unsafe.WriteUnaligned(output, value);
            return;
        }

        NativeMarshaller.Write(output, typeof(T), value, ref scope);
    }

    //复制返回值，引用类型的结果不逃逸原生存储
    internal static T Read(void* input)
    {
        if (!RuntimeHelpers.IsReferenceOrContainsReferences<T>())
        {
            return Unsafe.ReadUnaligned<T>(input);
        }

        return (T)NativeMarshaller.Read(input, typeof(T))!;
    }
}

internal static unsafe class NativeMarshaller
{
    //读取原生布局尺寸
    internal static int SizeOf(Type type) => NativeValues.GetKind(type) switch
    {
        NativeValueKind.Bool => 1,
        NativeValueKind.Pointer => IntPtr.Size,
        NativeValueKind.Int32 or NativeValueKind.UInt32 or NativeValueKind.Float32 or NativeValueKind.Object => 4,
        NativeValueKind.UInt64 or NativeValueKind.EnsId or NativeValueKind.Vector2 => 8,
        NativeValueKind.Vector3 => 12,
        NativeValueKind.Color or NativeValueKind.Quaternion or NativeValueKind.String or NativeValueKind.StringId => 16,
        var kind when ((uint)kind & 0xffff) == (uint)NativeValueKind.Array => 16,
        _ => 0,
    };
    //把普通托管值写入约定的原生布局
    internal static void Write(void* output, Type type, object? value, ref NativeMarshallingScope scope)
    {
        if (type == typeof(string) || type == typeof(StringId))
        {
            byte[] bytes = InteropText.EncodeUtf8(value?.ToString() ?? string.Empty);
            *(NativeUtf8*)output = new((byte*)scope.Pin(bytes), bytes.Length);
        }
        else if (typeof(Object).IsAssignableFrom(type))
        {
            *(int*)output = (value as Object)?.InstanceId ?? 0;
        }
        else if (type.IsSZArray)
        {
            Array? array = (Array? )value;
            Type element = type.GetElementType()!;
            int stride = SizeOf(element);
            int count = array?.Length ?? 0;
            byte[] bytes = new byte[checked(stride * count)];
            byte* data = (byte*)scope.Pin(bytes);
            for (int i = 0; i < count; i++)
            {
                Write(data + i * stride, element, array!.GetValue(i), ref scope);
            }

            *(NativeUtf8*)output = new(data, count);
        }
        else
        {
            if (type.IsEnum)
            {
                value = Convert.ChangeType(value, Enum.GetUnderlyingType(type));
            }

            switch (value)
            {
                case bool item:
                    *(byte*)output = item ? (byte)1 : (byte)0;
                    break;
                case int item:
                    *(int*)output = item;
                    break;
                case uint item:
                    *(uint*)output = item;
                    break;
                case ulong item:
                    *(ulong*)output = item;
                    break;
                case nint item:
                    *(nint*)output = item;
                    break;
                case nuint item:
                    *(nuint*)output = item;
                    break;
                case float item:
                    *(float*)output = item;
                    break;
                case vector2 item:
                    *(vector2*)output = item;
                    break;
                case vector3 item:
                    *(vector3*)output = item;
                    break;
                case color item:
                    *(color*)output = item;
                    break;
                case quaternion item:
                    *(quaternion*)output = item;
                    break;
                case EnsId item:
                    *(EnsId*)output = item;
                    break;
                default:
                    throw new NativeCallException(InteropStatus.UnsupportedType);
            }
        }
    }

    //从原生布局复制普通托管值
    internal static object? Read(void* input, Type type)
    {
        if (type == typeof(string))
        {
            return ((NativeUtf8*)input)->ToString();
        }

        if (type == typeof(nint))
        {
            return *(nint*)input;
        }

        if (type == typeof(nuint))
        {
            return *(nuint*)input;
        }

        if (type == typeof(StringId))
        {
            return new StringId(((NativeUtf8*)input)->ToString());
        }

        if (typeof(Object).IsAssignableFrom(type))
        {
            int id = *(int*)input;
            Object? wrapped;
            try
            {
                wrapped = NativeBindingRuntime.Wrap(id, type);
            }
            catch (InvalidOperationException)
            {
                throw new NativeCallException(InteropStatus.TypeMismatch);
            }

            if (id != 0 && wrapped == null)
            {
                throw new NativeCallException(InteropStatus.UnsupportedType);
            }

            return wrapped;
        }

        if (type.IsSZArray)
        {
            NativeUtf8 view = *(NativeUtf8*)input;
            Type element = type.GetElementType()!;
            int stride = SizeOf(element);
            Array array = Array.CreateInstance(element, view.Length);
            for (int i = 0; i < view.Length; i++)
            {
                array.SetValue(Read(view.Data + i * stride, element), i);
            }

            return array;
        }

        Type storageType = type.IsEnum ? Enum.GetUnderlyingType(type) : type;
        object value = NativeValues.GetKind(storageType) switch
        {
            NativeValueKind.Bool => *(byte*)input != 0,
            NativeValueKind.Int32 => *(int*)input,
            NativeValueKind.UInt32 => *(uint*)input,
            NativeValueKind.UInt64 => *(ulong*)input,
            NativeValueKind.Float32 => *(float*)input,
            NativeValueKind.Vector2 => *(vector2*)input,
            NativeValueKind.Vector3 => *(vector3*)input,
            NativeValueKind.Color => *(color*)input,
            NativeValueKind.Quaternion => *(quaternion*)input,
            NativeValueKind.EnsId => *(EnsId*)input,
            _ => throw new NativeCallException(InteropStatus.UnsupportedType),
        };
        return type.IsEnum ? Enum.ToObject(type, value) : value;
    }
}
