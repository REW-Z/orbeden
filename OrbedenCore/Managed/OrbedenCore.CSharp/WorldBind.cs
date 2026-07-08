using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct WorldBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, EnsId> CreateEns;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, EnsId> CreateEnsWithStableId;
    public delegate* unmanaged[Cdecl]<byte*, int, EnsId> FindEns;
    public delegate* unmanaged[Cdecl]<EnsId, byte> DestroyEns;
}
#pragma warning restore CS0649

internal static unsafe class WorldBind
{
    private static WorldBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 World 函数表
    internal static void Initialize(WorldBindApi value)
    {
        api = value;
        initialized = api.CreateEns != null;
    }

    //创建 Ens
    internal static EnsId CreateEns(string? name)
    {
        if (!initialized || api.CreateEns == null) return EnsId.Null;

        string value = name ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            return api.CreateEns(pointer, byteCount);
        }
    }

    //使用稳定 ID 创建 Ens
    internal static EnsId CreateEnsWithStableId(string? stableId, string? name)
    {
        if (!initialized || api.CreateEnsWithStableId == null) return EnsId.Null;

        string stableValue = stableId ?? string.Empty;
        string nameValue = name ?? string.Empty;
        int stableBytesCount = Encoding.UTF8.GetByteCount(stableValue);
        int nameBytesCount = Encoding.UTF8.GetByteCount(nameValue);
        Span<byte> stableBytes = stableBytesCount <= 1024 ? stackalloc byte[Math.Max(stableBytesCount, 1)] : new byte[stableBytesCount];
        Span<byte> nameBytes = nameBytesCount <= 1024 ? stackalloc byte[Math.Max(nameBytesCount, 1)] : new byte[nameBytesCount];
        Encoding.UTF8.GetBytes(stableValue.AsSpan(), stableBytes);
        Encoding.UTF8.GetBytes(nameValue.AsSpan(), nameBytes);

        fixed (byte* stablePointer = stableBytes)
        fixed (byte* namePointer = nameBytes)
        {
            return api.CreateEnsWithStableId(stablePointer, stableBytesCount, namePointer, nameBytesCount);
        }
    }

    //按稳定 ID 查找 Ens
    internal static EnsId FindEns(string? stableId)
    {
        if (!initialized || api.FindEns == null) return EnsId.Null;

        string value = stableId ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            return api.FindEns(pointer, byteCount);
        }
    }

    //销毁 Ens
    internal static bool DestroyEns(EnsId ens)
    {
        return initialized && api.DestroyEns != null && api.DestroyEns(ens) != 0;
    }
}
