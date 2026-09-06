using System;
using System.Runtime.InteropServices;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

/// <summary>Editor 托管入口，由 C++ EditorSystem 调用。</summary>
public static class EditorRuntime
{
[StructLayout(LayoutKind.Sequential, Pack = 8)]
    private unsafe struct EditorManagedApi
    {
        public IntPtr EngineApi;
        public EditorGuiNativeApi Gui;
        public EditorApplicationNativeApi Application;
        public EditorGizmoApi Gizmo;
        public EditorPanelNativeApi Panels;
        public EditorAssetNativeApi Assets;
        public EditorComponentNativeApi Components;
    }

    /// <summary>初始化 Editor 托管桥接。</summary>
    [UnmanagedCallersOnly]
    public static unsafe byte Initialize(IntPtr editorApi)
    {
        try
        {
            ValidateNativeApiLayout();

            if (editorApi == IntPtr.Zero)
            {
                OrbedenCoreRuntime.InitializeEngineBindings(IntPtr.Zero);
                NativeEditorGUI.Initialize(default);
                EditorApplication.Initialize(default);
                Gizmos.Initialize(default);
                EditorAssetsNative.Initialize(default);
                EditorNativeComponents.Initialize(default);
                EditorGUI.SetObjectFieldAssetProvider(null);
                return 0;
            }

            EditorManagedApi api = *(EditorManagedApi*)editorApi;
            OrbedenCoreRuntime.InitializeEngineBindings(api.EngineApi);
            NativeEditorGUI.Initialize(api.Gui);
            EditorApplication.Initialize(api.Application);
            EditorApplication.ClearDirty();
            EditorPropertyHistory.Clear();
            Gizmos.Initialize(api.Gizmo);
            EditorAssetsNative.Initialize(api.Assets);
            EditorNativeComponents.Initialize(api.Components);
            EditorAssetCatalog.Instance.Refresh();
            EditorGUI.SetObjectFieldAssetProvider(EditorAssetCatalog.Instance);
            return EditorPanelRegistry.Initialize(api.Panels) ? (byte)1 : (byte)0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Editor managed initialization failed: {ex}");
            return 0;
        }
    }

    /// <summary>加载当前项目的用户游戏程序集。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void LoadGameAssembly(byte* assemblyPath, int assemblyPathLength)
    {
        EditorPropertyHistory.Clear();
        EditorPanelRegistry.LoadGameAssembly(ReadUtf8(assemblyPath, assemblyPathLength));
    }

    /// <summary>卸载当前用户游戏程序集引用。</summary>
    [UnmanagedCallersOnly]
    public static void UnloadGameAssembly()
    {
        EditorPropertyHistory.Clear();
        EditorPanelRegistry.UnloadGameAssembly();
    }

    /// <summary>保存托管 Editor 面板暂存的项目数据。</summary>
    [UnmanagedCallersOnly]
    public static byte SaveProjectState()
    {
        try { return EditorPanelRegistry.SavePendingChanges() ? (byte)1 : (byte)0; }
        catch (Exception ex) { Console.Error.WriteLine($"Editor managed save failed: {ex}"); return 0; }
    }

    /// <summary>撤销最近一次属性或组件事务。</summary>
    [UnmanagedCallersOnly]
    public static byte Undo() => EditorPropertyHistory.Undo() ? (byte)1 : (byte)0;

    /// <summary>重做最近一次属性或组件事务。</summary>
    [UnmanagedCallersOnly]
    public static byte Redo() => EditorPropertyHistory.Redo() ? (byte)1 : (byte)0;

    /// <summary>通知托管 Editor 原生 World 已成功保存。</summary>
    [UnmanagedCallersOnly]
    public static void WorldSaved() => EditorApplication.ClearWorldDirty();

    /// <summary>绘制指定 C# Editor Panel。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void DrawPanel(int handle,
        uint ensId,
        uint ensVersion,
        EnsId* selectedEns,
        int selectedEnsCount,
        byte* selectedStableIds,
        int selectedStableIdsLength,
        byte* stableId,
        int stableIdLength)
    {
        try
        {
            EnsId[] selection = selectedEns == null || selectedEnsCount <= 0
                ? []
                : new ReadOnlySpan<EnsId>(selectedEns, selectedEnsCount).ToArray();
            EnsId active = new(ensId, ensVersion);
            if (selection.Length == 0 && !active.IsNull) selection = [active];
            EditorPanelContext context = new(
                active,
                selection,
                ReadUtf8List(selectedStableIds, selectedStableIdsLength, selection.Length),
                ReadUtf8(stableId, stableIdLength));
            EditorPanelRegistry.DrawPanel(handle, context);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Editor Panel dispatch failed: {ex}");
        }
    }

    /// <summary>设置指定 C# Editor Panel 可见状态。</summary>
    [UnmanagedCallersOnly]
    public static void SetPanelVisible(int handle, byte visible)
    {
        try
        {
            EditorPanelRegistry.SetPanelVisible(handle, visible != 0);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Editor Panel visibility dispatch failed: {ex}");
        }
    }

    /// <summary>绘制 C# Scene Handles。</summary>
    [UnmanagedCallersOnly]
    public static void DrawSceneGizmos()
    {
        try
        {
            Gizmos.Line(new vector3(-1.5f, 0.05f, 0.0f), new vector3(1.5f, 0.05f, 0.0f), new color4(0.95f, 0.25f, 0.20f, 1.0f));
            Gizmos.Line(new vector3(0.0f, 0.05f, -1.5f), new vector3(0.0f, 0.05f, 1.5f), new color4(0.20f, 0.80f, 0.95f, 1.0f));
            Gizmos.Label(new vector3(0.0f, 1.35f, 0.0f), "C# Gizmo");
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Editor Scene Handles draw failed: {ex}");
        }
    }

    /// <summary>发布当前项目的 NativeAOT 库。</summary>
    [UnmanagedCallersOnly]
    public static unsafe byte PublishGameAot(byte* repositoryRoot,
        int repositoryRootLength,
        byte* projectRoot,
        int projectRootLength,
        byte* scriptProject,
        int scriptProjectLength,
        byte* configuration,
        int configurationLength,
        byte* targetPlatform,
        int targetPlatformLength,
        byte* errorBuffer,
        int errorBufferSize)
    {
        bool succeeded = PlayerBuildPipeline.Publish(
            ReadUtf8(repositoryRoot, repositoryRootLength),
            ReadUtf8(projectRoot, projectRootLength),
            ReadUtf8(scriptProject, scriptProjectLength),
            ReadUtf8(configuration, configurationLength),
            ReadUtf8(targetPlatform, targetPlatformLength),
            out string error);
        WriteUtf8(errorBuffer, errorBufferSize, error);
        return succeeded ? (byte)1 : (byte)0;
    }

    //从 C++ 传入的 UTF-8 指针读取字符串。
    private static unsafe string ReadUtf8(byte* text, int length)
    {
        if (text == null || length <= 0) return string.Empty;
        return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(text, length));
    }

    //读取以 NUL 分隔并与 Ens 选择列表对齐的 UTF-8 字符串。
    private static unsafe IReadOnlyList<string> ReadUtf8List(byte* value, int length, int expectedCount)
    {
        if (value == null || length <= 0 || expectedCount <= 0) return [];
        List<string> result = new(expectedCount);
        int start = 0;
        for (int index = 0; index < length && result.Count < expectedCount; ++index)
        {
            if (value[index] != 0) continue;
            result.Add(Encoding.UTF8.GetString(new ReadOnlySpan<byte>(value + start, index - start)));
            start = index + 1;
        }
        if (start < length && result.Count < expectedCount)
        {
            result.Add(Encoding.UTF8.GetString(new ReadOnlySpan<byte>(value + start, length - start)));
        }
        while (result.Count < expectedCount) result.Add(string.Empty);
        return result;
    }

    //把 UTF-8 文本写入 C++ 提供的缓冲区。
    private static unsafe void WriteUtf8(byte* buffer, int bufferSize, string text)
    {
        if (buffer == null || bufferSize <= 0) return;

        Span<byte> output = new(buffer, bufferSize);
        output.Clear();
        Encoding.UTF8.GetEncoder().Convert(text.AsSpan(), output[..^1], true, out _, out int bytesUsed, out _);
        output[bytesUsed] = 0;
    }

    //在读取 C++ Editor 函数表前验证托管 ABI 的固定尺寸。
    private static unsafe void ValidateNativeApiLayout()
    {
        ValidateFunctionTable<EditorGuiNativeApi>(nameof(EditorGuiNativeApi), 30);
        ValidateFunctionTable<EditorApplicationNativeApi>(nameof(EditorApplicationNativeApi), 3);
        ValidateFunctionTable<EditorGizmoApi>(nameof(EditorGizmoApi), 2);
        ValidateFunctionTable<EditorPanelNativeApi>(nameof(EditorPanelNativeApi), 2);
        ValidateFunctionTable<EditorAssetNativeApi>(nameof(EditorAssetNativeApi), 4);
        ValidateFunctionTable<EditorComponentNativeApi>(nameof(EditorComponentNativeApi), 19);
        ValidateFunctionTable<EditorManagedApi>(nameof(EditorManagedApi), 61);
    }

    //验证全由函数指针槽组成的函数表尺寸。
    private static unsafe void ValidateFunctionTable<T>(string name, int slotCount) where T : unmanaged
    {
        int expectedSize = checked(slotCount * IntPtr.Size);
        if (sizeof(T) != expectedSize)
            throw new TypeLoadException($"{name} ABI size mismatch: expected {expectedSize}, actual {sizeof(T)}.");
    }
}
