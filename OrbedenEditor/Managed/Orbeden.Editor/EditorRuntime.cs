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
        public EditorLogNativeApi Log;
        public EditorProfilerNativeApi Profiler;
    }

    //初始化失败原因的非托管副本，供原生侧读取。
    private static IntPtr initializationErrorPointer = IntPtr.Zero;
    private static int initializationErrorLength;

    /// <summary>初始化 Editor 托管桥接。</summary>
    [UnmanagedCallersOnly]
    public static unsafe byte Initialize(IntPtr editorApi)
    {
        try
        {
            ValidateNativeApiLayout();

            if (editorApi == IntPtr.Zero)
            {
                EditorAssetCache.Stop();
                OrbedenCoreRuntime.InitializeEngineBindings(IntPtr.Zero);
                NativeEditorGUI.Initialize(default);
                NativeEditorLog.Initialize(default);
                NativeEditorProfiler.Initialize(default);
                EditorConsole.Install();
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
            NativeEditorLog.Initialize(api.Log);
            NativeEditorProfiler.Initialize(api.Profiler);

            //面板注册期间的警告也要进 Console 面板，所以先装日志通道
            EditorConsole.Install();
            EditorTheme.ApplyCurrent();
            EditorApplication.Initialize(api.Application);
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
            SetInitializationError(ex.ToString());
            Console.Error.WriteLine($"Editor managed initialization failed: {ex}");
            return 0;
        }
    }

    /// <summary>返回上一次初始化失败的原因，由原生侧在 Initialize 返回 0 后取回写进日志。</summary>
    [UnmanagedCallersOnly]
    public static unsafe IntPtr GetInitializationError(int* length)
    {
        if (length != null) *length = initializationErrorLength;
        return initializationErrorPointer;
    }

    //托管侧没有日志通道，失败原因只能存在非托管内存里等原生侧来取。
    private static void SetInitializationError(string message)
    {
        if (initializationErrorPointer != IntPtr.Zero) Marshal.FreeHGlobal(initializationErrorPointer);

        byte[] bytes = Encoding.UTF8.GetBytes(message);
        initializationErrorPointer = Marshal.AllocHGlobal(bytes.Length);
        Marshal.Copy(bytes, 0, initializationErrorPointer, bytes.Length);
        initializationErrorLength = bytes.Length;
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
    public static byte Undo()
    {
        try { return EditorPropertyHistory.Undo() ? (byte)1 : (byte)0; }
        catch (Exception ex) { Console.Error.WriteLine($"Undo failed: {ex}"); return 0; }
    }

    /// <summary>重做最近一次属性或组件事务。</summary>
    [UnmanagedCallersOnly]
    public static byte Redo()
    {
        try { return EditorPropertyHistory.Redo() ? (byte)1 : (byte)0; }
        catch (Exception ex) { Console.Error.WriteLine($"Redo failed: {ex}"); return 0; }
    }

    /// <summary>请求当前聚焦的面板开始重命名选中项。</summary>
    [UnmanagedCallersOnly]
    public static void RequestRenameSelected()
    {
        try { EditorSelection.Rename(); }
        catch (Exception ex) { Console.Error.WriteLine($"Editor rename dispatch failed: {ex}"); }
    }

    /// <summary>请求当前聚焦的面板删除选中项。</summary>
    [UnmanagedCallersOnly]
    public static void RequestDeleteSelected()
    {
        try { EditorSelection.Delete(); }
        catch (Exception ex) { Console.Error.WriteLine($"Editor delete dispatch failed: {ex}"); }
    }

    /// <summary>请求当前聚焦的面板重新导入选中资源。</summary>
    [UnmanagedCallersOnly]
    public static void RequestReimportSelected()
    {
        try { EditorSelection.Reimport(); }
        catch (Exception ex) { Console.Error.WriteLine($"Editor reimport dispatch failed: {ex}"); }
    }

    /// <summary>请求重新导入全部已加载资源，不依赖当前选择。</summary>
    [UnmanagedCallersOnly]
    public static void RequestReimportAll()
    {
        try { EditorAssetsNative.ReimportAllAssets(); }
        catch (Exception ex) { Console.Error.WriteLine($"Editor reimport all failed: {ex}"); }
    }

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

    /// <summary>绘制 C# Scene Handles，并提交原生手柄产生的编辑。</summary>
    [UnmanagedCallersOnly]
    public static void DrawSceneGizmos()
    {
        try
        {
            //原生侧每帧调用一次，正好用来轮询已松手的手柄拖拽
            while (Gizmos.TakeEdit(out EditorGizmoEdit edit)) CommitGizmoEdit(edit);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Editor Scene Handles draw failed: {ex}");
        }
    }

    //把一次手柄拖拽记成一条撤销事务，撤销/重做直接写局部变换
    private static void CommitGizmoEdit(EditorGizmoEdit edit)
    {
        if (edit.Ens.IsNull) return;

        EnsId id = edit.Ens;
        vector3 startPosition = edit.StartPosition;
        quaternion startRotation = edit.StartRotation;
        vector3 startScale = edit.StartScale;
        vector3 endPosition = edit.EndPosition;
        quaternion endRotation = edit.EndRotation;
        vector3 endScale = edit.EndScale;

        string label = edit.Mode switch
        {
            1 => "Rotate Ens",
            2 => "Scale Ens",
            _ => "Move Ens",
        };
        EditorPropertyHistory.PushAction(label,
            () => RestoreGizmoTransform(id, startPosition, startRotation, startScale),
            () => RestoreGizmoTransform(id, endPosition, endRotation, endScale));
    }

    //按撤销记录写回一个 Ens 的局部变换
    private static void RestoreGizmoTransform(EnsId id, vector3 position, quaternion rotation, vector3 scale)
    {
        Ens ens = Ens.FromId(id);
        if (!ens.IsValid) return;

        ens.Transform.SetLocalPosition(position);
        ens.Transform.SetLocalRotation(rotation);
        ens.Transform.SetLocalScale(scale);
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
        ValidateFunctionTable<EditorGuiNativeApi>(nameof(EditorGuiNativeApi), 70);
        ValidateFunctionTable<EditorApplicationNativeApi>(nameof(EditorApplicationNativeApi), 10);
        ValidateFunctionTable<EditorGizmoApi>(nameof(EditorGizmoApi), 3);
        ValidateFunctionTable<EditorPanelNativeApi>(nameof(EditorPanelNativeApi), 2);
        ValidateFunctionTable<EditorAssetNativeApi>(nameof(EditorAssetNativeApi), 18);
        ValidateFunctionTable<EditorComponentNativeApi>(nameof(EditorComponentNativeApi), 24);
        ValidateFunctionTable<EditorLogNativeApi>(nameof(EditorLogNativeApi), 5);
        ValidateFunctionTable<EditorProfilerNativeApi>(nameof(EditorProfilerNativeApi), 8);
        ValidateFunctionTable<EditorManagedApi>(nameof(EditorManagedApi), 141);
        ValidateSize<EditorTextAbi>(nameof(EditorTextAbi), 16);
        ValidateSize<EditorValueAbi>(nameof(EditorValueAbi), 24);
        ValidateSize<EditorPropertyAbi>(nameof(EditorPropertyAbi), 64);
        ValidateSize<EditorComponentSnapshotAbi>(nameof(EditorComponentSnapshotAbi), 32);
        ValidateSize<EditorRectPrimitive>(nameof(EditorRectPrimitive), 36);
        ValidateSize<ProfileEvent>(nameof(ProfileEvent), 40);
        ValidateSize<ProfileFrameSummary>(nameof(ProfileFrameSummary), 88);
    }

    //验证全由函数指针槽组成的函数表尺寸。
    private static unsafe void ValidateFunctionTable<T>(string name, int slotCount) where T : unmanaged
    {
        int expectedSize = checked(slotCount * IntPtr.Size);
        if (sizeof(T) != expectedSize)
            throw new TypeLoadException($"{name} ABI size mismatch: expected {expectedSize}, actual {sizeof(T)}.");
    }

    //验证结构体的托管布局与原生侧一致。
    private static unsafe void ValidateSize<T>(string name, int expectedSize) where T : unmanaged
    {
        if (sizeof(T) != expectedSize)
            throw new TypeLoadException($"{name} ABI size mismatch: expected {expectedSize}, actual {sizeof(T)}.");
    }
}
