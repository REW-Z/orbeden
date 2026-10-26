using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>类型化原生调用或字段读写失败，携带原始互操作状态。</summary>
public sealed class NativeCallException : InvalidOperationException
{
    /// <summary>原生调用返回的失败状态。</summary>
    public InteropStatus Status { get; }

    internal NativeCallException(InteropStatus status) : base($"Native call failed: {status}.") => Status = status;
}

/// <summary>按值传递的 UTF-8 视图；输入指针须保持有效，输出应在下次字符串调用前复制。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public readonly unsafe struct NativeUtf8
{
    public readonly byte* Data;
    public readonly int Length;
    private readonly uint reserved;

    /// <summary>引用一段有效的 UTF-8 数据。</summary>
    public NativeUtf8(byte* data, int length)
    {
        Data = data;
        Length = length;
        reserved = 0;
    }

    /// <summary>复制视图中的文本。</summary>
    public override string ToString()
    {
        if (Length < 0 || Length > 0 && Data == null) throw new InvalidOperationException("Invalid native UTF-8 view.");
        return Length == 0 ? string.Empty : InteropText.DecodeUtf8(Data, Length);
    }
}

/// <summary>StringId 的 UTF-8 视图；与普通字符串使用不同的绑定签名。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public readonly unsafe struct NativeStringId
{
    public readonly NativeUtf8 Text;

    /// <summary>引用 StringId 的 UTF-8 数据。</summary>
    public NativeStringId(byte* data, int length) => Text = new(data, length);

    /// <summary>复制 StringId 文本。</summary>
    public override string ToString() => Text.ToString();
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct NativeTypedMethodsApi
{
    internal delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, uint, uint*, int, MemberHandle*, InteropStatus> Resolve;
    internal delegate* unmanaged[Cdecl, SuppressGCTransition]<ComponentHandle, MemberHandle, void**, void**, InteropStatus> Prepare;
    internal delegate* unmanaged[Cdecl, SuppressGCTransition]<void> Complete;
}

internal readonly unsafe struct NativeMethodBinding
{
    private readonly ComponentHandle component;
    private readonly MemberHandle member;

    internal NativeMethodBinding(ComponentHandle component, MemberHandle member)
    {
        this.component = component;
        this.member = member;
    }

    //先进入稳定的 Core，成功后才取得游戏函数地址
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal InteropStatus TryEnter(out NativeCallFrame call)
    {
        call = default;
        NativeTypedMethodsApi api = NativeMethodRuntime.Api;
        if (api.Prepare == null || api.Complete == null) return InteropStatus.StaleHandle;
        void* instance = null;
        void* entry = null;
        InteropStatus status = api.Prepare(component, member, &instance, &entry);
        if (status == InteropStatus.Ok) call = new(instance, entry, api.Complete);
        return status;
    }
}

internal unsafe ref struct NativeCallFrame
{
    internal readonly void* Instance;
    internal readonly void* Entry;
    private delegate* unmanaged[Cdecl, SuppressGCTransition]<void> complete;

    internal NativeCallFrame(void* instance, void* entry, delegate* unmanaged[Cdecl, SuppressGCTransition]<void> complete)
    {
        Instance = instance;
        Entry = entry;
        this.complete = complete;
    }

    //将失败状态转为托管异常
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void ThrowIfFailed(InteropStatus status)
    {
        if (status != InteropStatus.Ok) throw new NativeCallException(status);
    }

    //即使运行库在回调内断开，也通过本次保存的 Core 入口退出
    internal void Dispose()
    {
        if (complete == null) return;
        complete();
        complete = null;
    }
}

internal static class NativeValueType<T> where T : unmanaged
{
    internal static readonly InteropValueKind Kind = GetKind();

    //只允许 ABI 已明确的值类型
    private static InteropValueKind GetKind()
    {
        if (typeof(T) == typeof(bool)) return InteropValueKind.Bool;
        if (typeof(T) == typeof(int)) return InteropValueKind.Int32;
        if (typeof(T) == typeof(uint)) return InteropValueKind.UInt32;
        if (typeof(T) == typeof(ulong)) return InteropValueKind.UInt64;
        if (typeof(T) == typeof(float)) return InteropValueKind.Float32;
        if (typeof(T) == typeof(vector2)) return InteropValueKind.Vector2;
        if (typeof(T) == typeof(vector3)) return InteropValueKind.Vector3;
        if (typeof(T) == typeof(color)) return InteropValueKind.Color;
        if (typeof(T) == typeof(quaternion)) return InteropValueKind.Quaternion;
        if (typeof(T) == typeof(EnsId)) return InteropValueKind.EnsId;
        if (typeof(T) == typeof(NativeUtf8)) return InteropValueKind.String;
        if (typeof(T) == typeof(NativeStringId)) return InteropValueKind.StringId;
        return (InteropValueKind)uint.MaxValue;
    }
}

internal static unsafe class NativeMethodRuntime
{
    internal static NativeTypedMethodsApi Api;
    internal static void Initialize(NativeTypedMethodsApi api) => Api = api;

    //冷路径校验签名，热路径只保存组件和成员句柄
    internal static InteropStatus Bind(ComponentHandle component, string name, InteropValueKind returnKind,
        ReadOnlySpan<InteropValueKind> parameters, out NativeMethodBinding binding)
    {
        binding = default;
        if (component.Domain != ComponentDomain.Native) return InteropStatus.UnsupportedType;
        if (Api.Resolve == null) return InteropStatus.StaleHandle;
        if (string.IsNullOrEmpty(name)) return InteropStatus.InvalidArgument;
        if ((uint)returnKind == uint.MaxValue) return InteropStatus.UnsupportedType;
        foreach (InteropValueKind parameter in parameters)
            if ((uint)parameter == uint.MaxValue) return InteropStatus.UnsupportedType;
        byte[] bytes = InteropText.EncodeUtf8(name);
        MemberHandle member;
        fixed (byte* pointer = bytes)
        fixed (uint* kinds = MemoryMarshal.Cast<InteropValueKind, uint>(parameters))
        {
            InteropStatus status = Api.Resolve(component, pointer, bytes.Length, (uint)returnKind, kinds, parameters.Length, &member);
            if (status == InteropStatus.Ok) binding = new(component, member);
            return status;
        }
    }
}
