using System;
using Orbeden;
using System.Runtime.InteropServices;
using System.Text;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorAssetNativeApi
{
    public IntPtr Context;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> CanModifyAssets;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, byte, int> RemapLiveReferences;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> OpenWorld;
    public delegate* unmanaged[Cdecl]<IntPtr, byte, byte*, int, int> GetWorldKey;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> SaveWorld;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> SetStartupWorld;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> CreateWorld;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, byte, byte> RemapWorldKeys;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetProjectError;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, byte> SavePrefab;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte, EnsId, EnsId, int> InstantiatePrefab;
    public delegate* unmanaged[Cdecl]<IntPtr, EnsId, byte*, int, int> CaptureEns;
    public delegate* unmanaged[Cdecl]<IntPtr, EnsId, byte> DestroyEnsTree;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> CreateEns;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte, int> ReimportAsset;
    public delegate* unmanaged[Cdecl]<IntPtr, int> ReimportAllAssets;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, int> LoadCachedAsset;
}
#pragma warning restore CS0649

/// <summary>Editor 资源操作使用的原生桥。</summary>
internal static unsafe class EditorAssetsNative
{
    private static EditorAssetNativeApi api;

    /// <summary>保存原生资源 API。</summary>
    internal static void Initialize(EditorAssetNativeApi value)
    {
        api = value;
    }

    /// <summary>从导入缓存加载单个对象及其引用依赖。</summary>
    internal static Orbeden.Object? LoadCachedAsset(string path, string key)
    {
        byte[] pathBytes = Encoding.UTF8.GetBytes(path), keyBytes = Encoding.UTF8.GetBytes(key);
        fixed (byte* source = pathBytes)
        fixed (byte* identity = keyBytes)
            return NativeBindingRuntime.Wrap<Orbeden.Object>(api.LoadCachedAsset(api.Context, source, pathBytes.Length, identity, keyBytes.Length));
    }
    //实例化预制体或恢复子树快照
    internal static Ens InstantiatePrefab(string text, bool snapshot, EnsId parent, EnsId before)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        fixed (byte* pointer = bytes)
            return NativeBindingRuntime.Wrap<Ens>(api.InstantiatePrefab(api.Context, pointer, bytes.Length,
                snapshot ? (byte)1 : (byte)0, parent, before)) ?? Ens.Null;
    }

    //捕获完整子树及其稳定身份
    internal static string CaptureEns(EnsId root)
    {
        int length = api.CaptureEns(api.Context, root, null, 0);
        byte[] bytes = new byte[length];
        fixed (byte* pointer = bytes) api.CaptureEns(api.Context, root, pointer, length);
        return Encoding.UTF8.GetString(bytes);
    }

    //销毁子树中的全部对象
    internal static bool DestroyEnsTree(EnsId root) => api.DestroyEnsTree(api.Context, root) != 0;

    //在当前 World 根下创建空 Ens
    internal static Ens CreateEns(string name)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(name);
        fixed (byte* pointer = bytes)
            return NativeBindingRuntime.Wrap<Ens>(api.CreateEns(api.Context, pointer, bytes.Length)) ?? Ens.Null;
    }

    //强制重新导入指定资源，返回处理的源文件数；目录要带 prefix 才会覆盖子路径
    internal static int ReimportAsset(string resourceKey, bool prefix)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(resourceKey);
        fixed (byte* pointer = bytes)
            return api.ReimportAsset(api.Context, pointer, bytes.Length, prefix ? (byte)1 : (byte)0);
    }

    //强制重新导入全部已加载资源，返回处理的源文件数
    internal static int ReimportAllAssets() => api.ReimportAllAssets(api.Context);

    //保存 Ens 子树为独立预制体
    internal static bool SavePrefab(string source, string key)
    {
        byte[] sourceBytes = Encoding.UTF8.GetBytes(source), keyBytes = Encoding.UTF8.GetBytes(key);
        fixed (byte* sourcePointer = sourceBytes)
        fixed (byte* keyPointer = keyBytes)
            return api.SavePrefab(api.Context, sourcePointer, sourceBytes.Length, keyPointer, keyBytes.Length) != 0;
    }

    //读取当前编辑或启动 World 的资源 Key
    internal static string GetWorldKey(bool startup)
    {
        int length = api.GetWorldKey(api.Context, startup ? (byte)1 : (byte)0, null, 0);
        byte[] bytes = new byte[length];
        fixed (byte* pointer = bytes) api.GetWorldKey(api.Context, startup ? (byte)1 : (byte)0, pointer, length);
        return Encoding.UTF8.GetString(bytes);
    }

    //读取项目操作失败原因
    internal static string GetProjectError()
    {
        int length = api.GetProjectError(api.Context, null, 0);
        byte[] bytes = new byte[length];
        fixed (byte* pointer = bytes) api.GetProjectError(api.Context, pointer, length);
        return Encoding.UTF8.GetString(bytes);
    }

    //保存当前编辑 World
    internal static bool SaveWorld() => api.SaveWorld(api.Context) != 0;

    //设置启动 World
    internal static bool SetStartupWorld(string key)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(key);
        fixed (byte* pointer = bytes) return api.SetStartupWorld(api.Context, pointer, bytes.Length) != 0;
    }

    //创建默认空 World 文件
    internal static bool CreateWorld(string key)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(key);
        fixed (byte* pointer = bytes) return api.CreateWorld(api.Context, pointer, bytes.Length) != 0;
    }

    //同步移动或删除后的 World 配置
    internal static bool RemapWorldKeys(string oldKey, string newKey, bool prefix)
    {
        byte[] oldBytes = Encoding.UTF8.GetBytes(oldKey), newBytes = Encoding.UTF8.GetBytes(newKey);
        fixed (byte* oldPointer = oldBytes)
        fixed (byte* newPointer = newBytes)
            return api.RemapWorldKeys(api.Context, oldPointer, oldBytes.Length, newPointer, newBytes.Length, prefix ? (byte)1 : (byte)0) != 0;
    }

    /// <summary>判断当前是否允许修改资源文件。</summary>
    internal static bool CanModifyAssets()
    {
        return api.CanModifyAssets != null && api.CanModifyAssets(api.Context) != 0;
    }

    /// <summary>批量重映射当前进程内存活的原生 Ref 字段。</summary>
    internal static int RemapLiveReferences(string oldKey, string newKey, bool prefix)
    {
        if (api.RemapLiveReferences == null || string.IsNullOrEmpty(oldKey)) return 0;

        byte[] oldBytes = Encoding.UTF8.GetBytes(oldKey);
        byte[] newBytes = Encoding.UTF8.GetBytes(newKey ?? string.Empty);
        fixed (byte* oldPointer = oldBytes)
        fixed (byte* newPointer = newBytes)
        {
            return api.RemapLiveReferences(api.Context,
                oldPointer,
                oldBytes.Length,
                newPointer,
                newBytes.Length,
                prefix ? (byte)1 : (byte)0);
        }
    }

    /// <summary>打开项目内的另一个场景，路径以项目根为基准。</summary>
    internal static bool OpenWorld(string relativeKey)
    {
        if (api.OpenWorld == null || string.IsNullOrEmpty(relativeKey)) return false;

        byte[] keyBytes = Encoding.UTF8.GetBytes(relativeKey);
        fixed (byte* keyPointer = keyBytes)
        {
            return api.OpenWorld(api.Context, keyPointer, keyBytes.Length) != 0;
        }
    }
}
