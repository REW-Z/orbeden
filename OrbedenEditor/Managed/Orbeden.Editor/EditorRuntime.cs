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
    public static unsafe void LoadGameAssembly(byte* assemblyPath, int assemblyPathLength, byte* sidecarPath, int sidecarPathLength)
    {
        EditorPanelRegistry.LoadGameAssembly(
            ReadUtf8(assemblyPath, assemblyPathLength),
            ReadUtf8(sidecarPath, sidecarPathLength));
    }

    /// <summary>卸载当前用户游戏程序集引用。</summary>
    [UnmanagedCallersOnly]
    public static void UnloadGameAssembly()
    {
        EditorPanelRegistry.UnloadGameAssembly();
    }

    /// <summary>绘制指定 C# Editor Panel。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void DrawPanel(int handle, uint ensId, uint ensVersion, byte* stableId, int stableIdLength)
    {
        try
        {
            EditorPanelContext context = new(
                new EnsId(ensId, ensVersion),
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
        ValidateFunctionTable<EditorApplicationNativeApi>(nameof(EditorApplicationNativeApi), 2);
        ValidateFunctionTable<EditorGizmoApi>(nameof(EditorGizmoApi), 2);
        ValidateFunctionTable<EditorPanelNativeApi>(nameof(EditorPanelNativeApi), 2);
        ValidateFunctionTable<EditorAssetNativeApi>(nameof(EditorAssetNativeApi), 4);
        ValidateFunctionTable<EditorComponentNativeApi>(nameof(EditorComponentNativeApi), 13);
        ValidateFunctionTable<EditorManagedApi>(nameof(EditorManagedApi), 54);
    }

    //验证全由函数指针槽组成的函数表尺寸。
    private static unsafe void ValidateFunctionTable<T>(string name, int slotCount) where T : unmanaged
    {
        int expectedSize = checked(slotCount * IntPtr.Size);
        if (sizeof(T) != expectedSize)
            throw new TypeLoadException($"{name} ABI size mismatch: expected {expectedSize}, actual {sizeof(T)}.");
    }
}
