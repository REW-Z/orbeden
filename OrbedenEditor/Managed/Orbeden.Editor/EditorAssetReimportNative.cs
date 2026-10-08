using System;
using System.Runtime.InteropServices;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorAssetReimportNativeApi
{
    public IntPtr Context;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetLoadedSources;
}
#pragma warning restore CS0649

/// <summary>条件重导使用的原生桥：只负责告诉托管侧"哪些源文件已被加载"。</summary>
internal static unsafe class EditorAssetReimportNative
{
    private static EditorAssetReimportNativeApi api;

    /// <summary>保存原生条件重导 API。</summary>
    internal static void Initialize(EditorAssetReimportNativeApi value)
    {
        api = value;
    }

    /// <summary>读取已加载资源涉及的源文件 Key；桥不可用或载荷畸形时返回 null。</summary>
    internal static List<string>? GetLoadedSources()
    {
        if (api.GetLoadedSources == null) return null;

        int length = api.GetLoadedSources(api.Context, null, 0);
        if (length <= 0) return [];
        byte[] bytes = new byte[length];
        fixed (byte* pointer = bytes)
        {
            //第二次调用期间资源表可能变化，长度不足时按实际写入量截断
            int written = api.GetLoadedSources(api.Context, pointer, length);
            if (written <= 0) return null;
            if (written < length) Array.Resize(ref bytes, written);
        }

        List<string> sources = [];
        foreach (string key in System.Text.Encoding.UTF8.GetString(bytes).Split('\0'))
        {
            if (key.Length != 0) sources.Add(key);
        }
        return sources;
    }
}
