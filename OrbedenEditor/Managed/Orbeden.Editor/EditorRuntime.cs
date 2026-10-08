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
        public EditorAssetReimportNativeApi Reimport;
        public EditorInputNativeApi Input;
        public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> ExportProfilerFrameText;
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
                EditorAssetReimportNative.Initialize(default);
                EditorInput.Initialize(default);
                EditorNativeComponents.Initialize(default);
                EditorGUI.SetObjectFieldAssetProvider(null);
                return 0;
            }

            EditorManagedApi api = *(EditorManagedApi*)editorApi;
            OrbedenCoreRuntime.InitializeEngineBindings(api.EngineApi);
            NativeEditorGUI.Initialize(api.Gui);
            NativeEditorLog.Initialize(api.Log);
            NativeEditorProfiler.Initialize(api.Profiler, api.ExportProfilerFrameText);

            //面板注册期间的警告也要进 Console 面板，所以先装日志通道
            EditorConsole.Install();
            EditorTheme.ApplyCurrent();
            EditorApplication.Initialize(api.Application);
            EditorPropertyHistory.Clear();
            Gizmos.Initialize(api.Gizmo);
            EditorAssetsNative.Initialize(api.Assets);
            EditorAssetReimportNative.Initialize(api.Reimport);
            EditorInput.Initialize(api.Input);
            EditorNativeComponents.Initialize(api.Components);
            EditorAssetCatalog.Instance.Refresh();
            EditorGUI.SetObjectFieldAssetProvider(EditorAssetCatalog.Instance);
            //内置组件编辑器在初始化成功后就可用，不依赖游戏程序集
            CustomEditorRegistry.RegisterBuiltins();
            //UI 编辑器属于引擎，初始化时登记常驻项；重复初始化各自幂等
            UIEditorRegistration.Register();
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
        string path = ReadUtf8(assemblyPath, assemblyPathLength);
        if (path.Length == 0) return;
        //这是编辑器显式的“装载这个程序集”入口：先释放旧会话，同路径重编译后也要拿到新代码。
        ManagedAssemblySession.Unload();
        //会话先建立，面板再刷新：两者必须看到同一份 Type 实例。
        ManagedAssemblySession.Load(path, typeof(OrbedenEditor.ComponentEditor).Assembly);
        EditorPanelRegistry.LoadGameAssembly(path);
    }

    /// <summary>接管脚本域已经装载的游戏程序集：不换会话，只让编辑器侧看到同一份 Type。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void AttachGameAssembly(byte* assemblyPath, int assemblyPathLength)
    {
        string path = ReadUtf8(assemblyPath, assemblyPathLength);
        if (path.Length == 0) return;
        //进入 Play 时影子程序集已由脚本域建立会话并登记了包装：这里再走一次卸载重装，
        //Play 域里已构造的实例会持有被释放程序集的类型，组件查找会静默失配。
        EditorPropertyHistory.Clear();
        EditorPanelRegistry.LoadGameAssembly(path);
    }

    /// <summary>卸载当前用户游戏程序集引用。</summary>
    [UnmanagedCallersOnly]
    public static void UnloadGameAssembly()
    {
        EditorPropertyHistory.Clear();
        //先广播给面板清自己的缓存，再释放会话；会话卸载回调负责编辑器静态表。
        EditorPanelRegistry.UnloadGameAssembly();
        ManagedAssemblySession.Unload();
    }

    /// <summary>保存托管 Editor 面板暂存的项目数据。</summary>
    [UnmanagedCallersOnly]
    public static byte SaveProjectState()
    {
        try
        {
            if (!EditorPanelRegistry.SavePendingChanges()) return 0;
            ScriptRuntimeRegistry.FlushHostFields();
            return 1;
        }
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
        //必须带上设置表：不传会让颜色空间、网格缩放、上轴这些导入设置退回语义推断
        try { EditorAssetsNative.ReimportAllAssets(EditorAssetCache.EncodeAllSettings()); }
        catch (Exception ex) { Console.Error.WriteLine($"Editor reimport all failed: {ex}"); }
    }

    /// <summary>导出源资源的导入设置表供 Player 打包使用。</summary>
    [UnmanagedCallersOnly]
    public static unsafe int ReadAssetImportSettings(byte* output, int capacity)
    {
        try
        {
            byte[] bytes = Encoding.UTF8.GetBytes(EditorAssetCache.EncodeAllSettings());
            if (output != null && capacity >= bytes.Length) bytes.CopyTo(new Span<byte>(output, capacity));
            return bytes.Length;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Editor import settings export failed: {ex}");
            return -1;
        }
    }

    /// <summary>开始一次后台脚本构建；原生已完成工程准备与过期判断。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void RequestScriptBuild(byte* scriptProject, int scriptProjectLength, byte reimport, byte outdated)
    {
        try { EditorRefresh.Start(ReadUtf8(scriptProject, scriptProjectLength), reimport != 0, outdated != 0); }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Editor script build dispatch failed: {ex}");
            EditorRefresh.Abort(ex.Message);
        }
    }

    /// <summary>绘制后台任务的进度浮层。原生在顶层窗口上下文每帧调用一次。</summary>
    [UnmanagedCallersOnly]
    public static void DrawProgressOverlay()
    {
        try { EditorProgress.DrawModal(); }
        catch (Exception ex) { Console.Error.WriteLine($"Editor progress overlay draw failed: {ex}"); }
    }

    /// <summary>
    /// 编辑态脚本域初始化。入口点必须由编辑器托管栈提供：CLR 宿主按路径绑定脚本入口会拿到
    /// 同身份的第二份 OrbedenCore.CSharp，脚本运行时的静态状态会与编辑器分家。
    /// </summary>
    [UnmanagedCallersOnly]
    public static void InitializeScriptRuntime(IntPtr nativeApi, uint executionMode) =>
        GameScriptRuntime.Initialize(nativeApi, executionMode);

    /// <summary>装载游戏程序集；Play 与会话共用的脚本域入口。装载随行的游戏 Editor 扩展程序集
    /// 需要解析编辑器合同，这里传入编辑器自身这一份，与编辑态装载入口保持一致。</summary>
    [UnmanagedCallersOnly]
    public static unsafe byte LoadScriptAssembly(byte* assemblyPath, int assemblyPathLength)
    {
        if (assemblyPath == null || assemblyPathLength <= 0) return 0;
        string path = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(assemblyPath, assemblyPathLength));
        return GameScriptRuntime.LoadAssembly(path, typeof(OrbedenEditor.ComponentEditor).Assembly) ? (byte)1 : (byte)0;
    }

    /// <summary>脚本域帧阶段入口；全部转发到编辑器这一份 GameScriptRuntime。</summary>
    [UnmanagedCallersOnly]
    public static void UpdateScriptRuntime(float deltaTime) => GameScriptRuntime.Update(deltaTime);

    [UnmanagedCallersOnly]
    public static void FixedUpdateScriptRuntime(float fixedDeltaTime) => GameScriptRuntime.FixedUpdate(fixedDeltaTime);

    [UnmanagedCallersOnly]
    public static void LateUpdateScriptRuntime(float deltaTime) => GameScriptRuntime.LateUpdate(deltaTime);

    [UnmanagedCallersOnly]
    public static void DrawGuiScriptRuntime() => GameScriptRuntime.DrawGUI();

    [UnmanagedCallersOnly]
    public static void EnsWorldActiveChangedScriptRuntime(EnsId ens, byte worldActive) =>
        GameScriptRuntime.OnEnsWorldActiveChanged(ens, worldActive != 0);

    [UnmanagedCallersOnly]
    public static void EnsDestroyedScriptRuntime(EnsId ens) => GameScriptRuntime.OnEnsDestroyed(ens);

    [UnmanagedCallersOnly]
    public static void ProcessInputScriptRuntime(float deltaTime) => GameScriptRuntime.ProcessInput(deltaTime);

    [UnmanagedCallersOnly]
    public static void PrepareRenderScriptRuntime(float deltaTime) => GameScriptRuntime.PrepareRender(deltaTime);

    /// <summary>编辑态脚本域关闭；与 InitializeScriptRuntime 同属一份程序集。</summary>
    [UnmanagedCallersOnly]
    public static void ShutdownScriptRuntime() => GameScriptRuntime.Shutdown();

    /// <summary>请求当前聚焦的面板复制选中项。</summary>
    [UnmanagedCallersOnly]
    public static void RequestCopySelected()
    {
        try { EditorSelection.Copy(); }
        catch (Exception ex) { Console.Error.WriteLine($"Editor copy dispatch failed: {ex}"); }
    }

    /// <summary>请求当前聚焦的面板粘贴剪贴板内容。</summary>
    [UnmanagedCallersOnly]
    public static void RequestPasteSelected()
    {
        try { EditorSelection.Paste(); }
        catch (Exception ex) { Console.Error.WriteLine($"Editor paste dispatch failed: {ex}"); }
    }

    /// <summary>请求当前聚焦的面板切换选中项的激活状态。</summary>
    [UnmanagedCallersOnly]
    public static void RequestToggleActiveSelected()
    {
        try { EditorSelection.ToggleActive(); }
        catch (Exception ex) { Console.Error.WriteLine($"Editor toggle active dispatch failed: {ex}"); }
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
    public static void DrawSceneGizmos(byte commitOnly)
    {
        try
        {
            //原生侧每帧调用一次，正好用来轮询已松手的手柄拖拽
            while (Gizmos.TakeEdit(out EditorGizmoEdit edit)) CommitGizmoEdit(edit);
            if (commitOnly == 0)
            {
                EditorSceneExtensions.DrawOverlay();
                CustomEditorRegistry.DrawScene();
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Editor Scene Handles draw failed: {ex}");
        }
    }

    /// <summary>把场景射线交给源码包的拾取扩展。</summary>
    [UnmanagedCallersOnly]
    public static unsafe byte PickScene(vector3 origin, vector3 direction, float maximumDistance, EditorScenePickHit* output)
    {
        if (output == null) return 0;
        *output = default;
        EditorScenePickRay ray = new(origin, direction, maximumDistance);
        if (!EditorSceneExtensions.TryPick(ray, out EditorScenePickHit hit)) return 0;
        *output = hit;
        return 1;
    }

    /// <summary>绘制编辑器底部状态栏内容。</summary>
    [UnmanagedCallersOnly]
    public static void DrawStatusBar()
    {
        try
        {
            //后台任务在这里每帧推进一次；它只碰状态与原生调用，不做 ImGui 绘制，
            //因此放在状态栏的侧栏上下文里是安全的（进度浮层另走 DrawProgressOverlay）
            EditorRefresh.Pump();
            EditorStatusBar.Draw();
            EditorRecentProjects.TrackCurrentProject();
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Editor Status Bar draw failed: {ex}");
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
        ValidateFunctionTable<EditorGuiNativeApi>(nameof(EditorGuiNativeApi), 85);
        ValidateFunctionTable<EditorApplicationNativeApi>(nameof(EditorApplicationNativeApi), 16);
        ValidateFunctionTable<EditorGizmoApi>(nameof(EditorGizmoApi), 9);
        ValidateFunctionTable<EditorPanelNativeApi>(nameof(EditorPanelNativeApi), 2);
        ValidateFunctionTable<EditorAssetNativeApi>(nameof(EditorAssetNativeApi), 19);
        ValidateFunctionTable<EditorComponentNativeApi>(nameof(EditorComponentNativeApi), 25);
        ValidateFunctionTable<EditorLogNativeApi>(nameof(EditorLogNativeApi), 5);
        ValidateFunctionTable<EditorProfilerNativeApi>(nameof(EditorProfilerNativeApi), 8);
        ValidateFunctionTable<EditorAssetReimportNativeApi>(nameof(EditorAssetReimportNativeApi), 2);
        ValidateFunctionTable<EditorInputNativeApi>(nameof(EditorInputNativeApi), 2);
        ValidateFunctionTable<EditorManagedApi>(nameof(EditorManagedApi), 175);
        ValidateSize<EditorTextAbi>(nameof(EditorTextAbi), 16);
        ValidateSize<EditorValueAbi>(nameof(EditorValueAbi), 24);
        ValidateSize<EditorPropertyAbi>(nameof(EditorPropertyAbi), 80);
        //尺寸相同也可能字段次序不同，把新增字段的位置一并钉住
        if (Marshal.OffsetOf<EditorPropertyAbi>(nameof(EditorPropertyAbi.TypeName)).ToInt32() != 32
            || Marshal.OffsetOf<EditorPropertyAbi>(nameof(EditorPropertyAbi.Value)).ToInt32() != 48)
            throw new TypeLoadException("EditorPropertyAbi ABI layout mismatch.");
        ValidateSize<EditorComponentSnapshotAbi>(nameof(EditorComponentSnapshotAbi), 32);
        ValidateSize<EditorRectPrimitive>(nameof(EditorRectPrimitive), 36);
        ValidateSize<EditorScenePickHit>(nameof(EditorScenePickHit), 20);
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
