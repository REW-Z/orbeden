using System;
using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>已绑定的原生字段；按具体类型读写，不重复查找字段名称。</summary>
public readonly unsafe struct NativeField<T> where T : unmanaged
{
    private readonly NativeFieldBinding binding;

    internal NativeField(NativeFieldBinding binding) => this.binding = binding;

    /// <summary>校验组件与字段代次，不保留下一次读写的调用保护。</summary>
    public bool IsValid
    {
        get
        {
            if (binding.TryEnter(0, out NativeCallFrame call) != InteropStatus.Ok) return false;
            call.Dispose();
            return true;
        }
    }

    /// <summary>绑定时是否存在读取入口；不表示组件仍然存活。</summary>
    public bool CanRead => (binding.Access & 1) != 0;

    /// <summary>绑定时是否存在写入入口；不表示组件仍然存活。</summary>
    public bool CanWrite => (binding.Access & 2) != 0;

    /// <summary>读取或写入原生字段，保留当前绑定。</summary>
    public T Value
    {
        get => Get();
        set => Set(value);
    }

    /// <summary>隐式读取原生字段，不改变绑定。</summary>
    public static implicit operator T(NativeField<T> field) => field.Get();

    /// <summary>直接返回字段值；读取失败时抛出 NativeCallException。</summary>
    public T Get()
    {
        NativeCallFrame.ThrowIfFailed(binding.TryEnter(1, out NativeCallFrame call));
        try
        {
            T value = default;
            NativeCallFrame.ThrowIfFailed(((delegate* unmanaged[Cdecl]<void*, T*, InteropStatus>)call.Entry)(call.Instance, &value));
            return value;
        }
        finally { call.Dispose(); }
    }

    /// <summary>写入字段并保留 setter、变更通知及脏标记；失败时抛出 NativeCallException。</summary>
    public void Set(T value)
    {
        NativeCallFrame.ThrowIfFailed(binding.TryEnter(2, out NativeCallFrame call));
        try { NativeCallFrame.ThrowIfFailed(((delegate* unmanaged[Cdecl]<void*, T, InteropStatus>)call.Entry)(call.Instance, value)); }
        finally { call.Dispose(); }
    }
}

public sealed partial class ComponentProxy
{
    /// <summary>按名称和精确值类型绑定原生字段；后续通过 NativeField 直接读写。</summary>
    public InteropStatus TryBindField<T>(string name, out NativeField<T> field) where T : unmanaged
    {
        InteropStatus status = NativeFieldRuntime.Bind(handle, name, NativeValueType<T>.Kind, out NativeFieldBinding binding);
        field = status == InteropStatus.Ok ? new(binding) : default;
        return status;
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct NativeTypedFieldsApi
{
    internal delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, uint, MemberHandle*, uint*, InteropStatus> Resolve;
    internal delegate* unmanaged[Cdecl, SuppressGCTransition]<ComponentHandle, MemberHandle, uint, void**, void**, InteropStatus> Prepare;
    internal delegate* unmanaged[Cdecl, SuppressGCTransition]<void> Complete;
}

internal readonly unsafe struct NativeFieldBinding
{
    private readonly ComponentHandle component;
    private readonly MemberHandle member;
    internal readonly uint Access;

    internal NativeFieldBinding(ComponentHandle component, MemberHandle member, uint access)
    {
        this.component = component;
        this.member = member;
        Access = access;
    }

    //每次从稳定的 Core 取得当前有效的字段入口
    internal InteropStatus TryEnter(uint access, out NativeCallFrame call)
    {
        call = default;
        NativeTypedFieldsApi api = NativeFieldRuntime.Api;
        if (api.Prepare == null || api.Complete == null) return InteropStatus.StaleHandle;
        void* instance = null;
        void* entry = null;
        InteropStatus status = api.Prepare(component, member, access, &instance, &entry);
        if (status == InteropStatus.Ok) call = new(instance, entry, api.Complete);
        return status;
    }
}

internal static unsafe class NativeFieldRuntime
{
    internal static NativeTypedFieldsApi Api;
    internal static void Initialize(NativeTypedFieldsApi api) => Api = api;

    //只在绑定阶段编码名称和校验类型
    internal static InteropStatus Bind(ComponentHandle component, string name, InteropValueKind kind, out NativeFieldBinding binding)
    {
        binding = default;
        if (component.Domain != ComponentDomain.Native) return InteropStatus.UnsupportedType;
        if (Api.Resolve == null) return InteropStatus.StaleHandle;
        if (string.IsNullOrEmpty(name)) return InteropStatus.InvalidArgument;
        if ((uint)kind == uint.MaxValue) return InteropStatus.UnsupportedType;
        byte[] bytes = InteropText.EncodeUtf8(name);
        MemberHandle member;
        uint access;
        fixed (byte* pointer = bytes)
        {
            InteropStatus status = Api.Resolve(component, pointer, bytes.Length, (uint)kind, &member, &access);
            if (status == InteropStatus.Ok) binding = new(component, member, access);
            return status;
        }
    }
}
