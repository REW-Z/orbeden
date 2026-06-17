using System;
using System.Text;

namespace Orbeden;

internal static unsafe class EnsBind
{
    private static delegate* unmanaged<EnsId, byte> FuncIsAlive;
    private static delegate* unmanaged<EnsId, byte*, int, int> FuncGetName;
    private static delegate* unmanaged<EnsId, byte*, int, void> FuncSetName;
    private static delegate* unmanaged<EnsId, byte> FuncHasSpaceComponent;
    private static delegate* unmanaged<EnsId, byte> FuncHasStaticMeshRenderer;
    private static bool initialized;

    //保存 C++ 传入的 Ens 函数表
    internal static void Initialize(IntPtr bindPointer)
    {
        if (bindPointer == IntPtr.Zero)
        {
            initialized = false;
            FuncIsAlive = null;
            FuncGetName = null;
            FuncSetName = null;
            FuncHasSpaceComponent = null;
            FuncHasStaticMeshRenderer = null;
            return;
        }

        void** functions = (void**)bindPointer;
        FuncIsAlive = (delegate* unmanaged<EnsId, byte>)functions[0];
        FuncGetName = (delegate* unmanaged<EnsId, byte*, int, int>)functions[1];
        FuncSetName = (delegate* unmanaged<EnsId, byte*, int, void>)functions[2];
        FuncHasSpaceComponent = (delegate* unmanaged<EnsId, byte>)functions[3];
        FuncHasStaticMeshRenderer = (delegate* unmanaged<EnsId, byte>)functions[4];
        initialized = true;
    }

    //判断 Ens 是否有效
    internal static bool IsAlive(EnsId ens)
    {
        return initialized && FuncIsAlive != null && FuncIsAlive(ens) != 0;
    }

    //读取 Ens 名称
    internal static string GetName(EnsId ens)
    {
        if (!initialized || FuncGetName == null) return string.Empty;

        int requiredBytes = FuncGetName(ens, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> bytes = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* pointer = bytes)
        {
            int actualBytes = FuncGetName(ens, pointer, requiredBytes);
            int length = Math.Clamp(actualBytes, 0, requiredBytes);
            return Encoding.UTF8.GetString(bytes[..length]);
        }
    }

    //写入 Ens 名称
    internal static void SetName(EnsId ens, string? name)
    {
        if (!initialized || FuncSetName == null) return;

        string value = name ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        if (byteCount <= 0)
        {
            FuncSetName(ens, null, 0);
            return;
        }

        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[byteCount] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);
        fixed (byte* pointer = bytes)
        {
            FuncSetName(ens, pointer, byteCount);
        }
    }

    //判断 Ens 是否拥有 SpaceComponent
    internal static bool HasSpaceComponent(EnsId ens)
    {
        return initialized && FuncHasSpaceComponent != null && FuncHasSpaceComponent(ens) != 0;
    }

    //判断 Ens 是否拥有 StaticMeshRenderer
    internal static bool HasStaticMeshRenderer(EnsId ens)
    {
        return initialized && FuncHasStaticMeshRenderer != null && FuncHasStaticMeshRenderer(ens) != 0;
    }
}
