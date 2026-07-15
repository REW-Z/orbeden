using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

/// <summary>Editor 托管入口，由 C++ EditorSystem 调用。</summary>
public static class EditorRuntime
{
    private static int panelClickCount;

    [StructLayout(LayoutKind.Sequential)]
    private unsafe struct EditorManagedApi
    {
        public IntPtr NativeApi;
        public EditorGizmoApi Gizmo;
    }

    /// <summary>初始化 Editor 托管桥接。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void Initialize(IntPtr editorApi)
    {
        if (editorApi == IntPtr.Zero)
        {
            OrbedenCoreRuntime.Initialize(IntPtr.Zero);
            Gizmos.Initialize(default);
            return;
        }

        EditorManagedApi api = *(EditorManagedApi*)editorApi;
        OrbedenCoreRuntime.Initialize(api.NativeApi);
        Gizmos.Initialize(api.Gizmo);
    }

    /// <summary>加载当前项目的用户游戏程序集。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void LoadGameAssembly(byte* assemblyPath, int assemblyPathLength, byte* sidecarPath, int sidecarPathLength)
    {
        EditorGameDomain.LoadGameAssembly(ReadUtf8(assemblyPath, assemblyPathLength), ReadUtf8(sidecarPath, sidecarPathLength));
    }

    /// <summary>卸载当前用户游戏程序集引用。</summary>
    [UnmanagedCallersOnly]
    public static void UnloadGameAssembly()
    {
        EditorGameDomain.UnloadGameAssembly();
    }

    /// <summary>绘制当前选中 Ens 的托管 Inspector。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void DrawInspector(uint ensId, uint ensVersion, byte* stableId, int stableIdLength)
    {
        EditorGameDomain.DrawInspector(new EnsId(ensId, ensVersion), ReadUtf8(stableId, stableIdLength));
    }

    /// <summary>绘制当前选中 Ens 的托管 Inspector 内容。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void DrawInspectorContent(uint ensId, uint ensVersion, byte* stableId, int stableIdLength)
    {
        EditorGameDomain.DrawInspectorContent(new EnsId(ensId, ensVersion), ReadUtf8(stableId, stableIdLength));
    }

    /// <summary>绘制 C# Editor 面板。</summary>
    [UnmanagedCallersOnly]
    public static void DrawPanels()
    {
        bool visible = GUI.BeginPanel("C# Editor Panel");
        try
        {
            if (!visible) return;

            DrawEditorPanelContentInternal();
        }
        finally
        {
            GUI.EndPanel();
        }
    }

    /// <summary>绘制 C# Editor 面板内容。</summary>
    [UnmanagedCallersOnly]
    public static void DrawEditorPanelContent()
    {
        DrawEditorPanelContentInternal();
    }

    //绘制 C# Editor 面板内容。
    private static void DrawEditorPanelContentInternal()
    {
        GUI.Label("Managed editor panel");
        if (GUI.Button("Editor C# Button"))
        {
            panelClickCount++;
        }

        GUI.Label($"Clicks: {panelClickCount}");
    }

    /// <summary>绘制 C# SceneView Gizmos。</summary>
    [UnmanagedCallersOnly]
    public static void DrawSceneGizmos()
    {
        Gizmos.Line(new vector3(-1.5f, 0.05f, 0.0f), new vector3(1.5f, 0.05f, 0.0f), new color4(0.95f, 0.25f, 0.20f, 1.0f));
        Gizmos.Line(new vector3(0.0f, 0.05f, -1.5f), new vector3(0.0f, 0.05f, 1.5f), new color4(0.20f, 0.80f, 0.95f, 1.0f));
        Gizmos.Label(new vector3(0.0f, 1.35f, 0.0f), "C# Gizmo");
    }

    /// <summary>发布当前项目的 NativeAOT 静态库。</summary>
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
}
