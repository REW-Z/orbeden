using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace OrbedenCore.CSharp;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct EnsBindApi
{
    public delegate* unmanaged[Cdecl]<EnsId, byte> IsAlive;
    public delegate* unmanaged[Cdecl]<EnsId, byte*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<EnsId, byte*, int, void> SetName;
    public delegate* unmanaged[Cdecl]<EnsId, byte> HasSpaceComponent;
    public delegate* unmanaged[Cdecl]<EnsId, byte> HasStaticMeshRenderer;
    public delegate* unmanaged[Cdecl]<EnsId, byte> AddStaticMeshRenderer;
}
#pragma warning restore CS0649

internal static unsafe class EnsBind
{
    private static EnsBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 Ens 函数表
    internal static void Initialize(EnsBindApi value)
    {
        api = value;
        initialized = api.IsAlive != null;
    }

    //判断 Ens 是否有效
    internal static bool IsAlive(EnsId ens)
    {
        return initialized && api.IsAlive != null && api.IsAlive(ens) != 0;
    }

    //读取 Ens 名称
    internal static string GetName(EnsId ens)
    {
        if (!initialized || api.GetName == null) return string.Empty;

        int requiredBytes = api.GetName(ens, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> bytes = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* pointer = bytes)
        {
            int actualBytes = api.GetName(ens, pointer, requiredBytes);
            int length = Math.Clamp(actualBytes, 0, requiredBytes);
            return Encoding.UTF8.GetString(bytes[..length]);
        }
    }

    //写入 Ens 名称
    internal static void SetName(EnsId ens, string? name)
    {
        if (!initialized || api.SetName == null) return;

        string value = name ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        if (byteCount <= 0)
        {
            api.SetName(ens, null, 0);
            return;
        }

        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[byteCount] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);
        fixed (byte* pointer = bytes)
        {
            api.SetName(ens, pointer, byteCount);
        }
    }

    //判断 Ens 是否拥有 SpaceComponent
    internal static bool HasSpaceComponent(EnsId ens)
    {
        return initialized && api.HasSpaceComponent != null && api.HasSpaceComponent(ens) != 0;
    }

    //判断 Ens 是否拥有 StaticMeshRenderer
    internal static bool HasStaticMeshRenderer(EnsId ens)
    {
        return initialized && api.HasStaticMeshRenderer != null && api.HasStaticMeshRenderer(ens) != 0;
    }

    //添加 StaticMeshRenderer
    internal static bool AddStaticMeshRenderer(EnsId ens)
    {
        return initialized && api.AddStaticMeshRenderer != null && api.AddStaticMeshRenderer(ens) != 0;
    }
}
