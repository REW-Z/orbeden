using System;
using System.Runtime.InteropServices;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct OrbedenEngineNativeApi
{
    public uint AbiVersion;
    public uint StructSize;
    public WorldBindApi World;
    public PathDefinesBindApi PathDefines;
    public EnsBindApi Ens;
    public ObjectBindApi Object;
    public ObjectExtensionBindApi ObjectExtension;
    public NativeBindingsApi Bindings;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct OrbedenNativeApi
{
    public uint AbiVersion;
    public uint StructSize;
    public RuntimeGuiApi Gui;
    public WorldBindApi World;
    public PathDefinesBindApi PathDefines;
    public EnsBindApi Ens;
    public ObjectBindApi Object;
    public RuntimeGuiExtensionApi GuiExtension;
    public RuntimeGuiAdvancedApi GuiAdvanced;
    public ObjectExtensionBindApi ObjectExtension;
    public NativeBindingsApi Bindings;
    public NativeScriptInteropApi ScriptInterop;
    public ScriptBindApi Script;
    public RuntimeGuiDrawApi GuiDraw;
}
#pragma warning restore CS0649

/// <summary>AOT 运行时原生 API 初始化入口。</summary>
public static unsafe class OrbedenCoreRuntime
{
    private static bool nativeAbiValidated;

    /// <summary>初始化引擎原生 API。</summary>
    public static void Initialize(IntPtr nativeApi)
    {
        ValidateNativeApiLayout();

        if (nativeApi == IntPtr.Zero)
        {
            InitializeEngineBindings(default(OrbedenNativeApi));
            ScriptInteropDispatch.Initialize(default);
            Script.InitializeNativeApi(default);
            GUI.InitializeNativeApi(default, default, default);
            GUI.InitializeDrawApi(default);
            return;
        }

        ValidateNativeApiHeader(nativeApi, sizeof(OrbedenNativeApi));
        OrbedenNativeApi api = *(OrbedenNativeApi*)nativeApi;
        InitializeEngineBindings(api);
        ScriptInteropDispatch.Initialize(api.ScriptInterop);
        Script.InitializeNativeApi(api.Script);
        GUI.InitializeNativeApi(api.Gui, api.GuiExtension, api.GuiAdvanced);
        GUI.InitializeDrawApi(api.GuiDraw);
    }

    /// <summary>初始化引擎对象和组件绑定。</summary>
    public static void InitializeEngineBindings(IntPtr nativeApi)
    {
        ValidateNativeApiLayout();

        if (nativeApi == IntPtr.Zero)
        {
            Ens.InitializeWorldNativeApi(default);
            PathDefines.InitializeNativeApi(default);
            Ens.InitializeEnsNativeApi(default);
            Object.InitializeNativeApi(default, default);
            NativeBindingRuntime.Initialize(default);
            return;
        }

        ValidateNativeApiHeader(nativeApi, sizeof(OrbedenEngineNativeApi));
        OrbedenEngineNativeApi api = *(OrbedenEngineNativeApi*)nativeApi;
        InitializeEngineBindings(api);
    }

    /// <summary>初始化 Editor 使用的引擎对象和组件绑定。</summary>
    private static void InitializeEngineBindings(OrbedenEngineNativeApi api)
    {
        Ens.InitializeWorldNativeApi(api.World);
        PathDefines.InitializeNativeApi(api.PathDefines);
        Ens.InitializeEnsNativeApi(api.Ens);
        Object.InitializeNativeApi(api.Object, api.ObjectExtension);
        NativeBindingRuntime.Initialize(api.Bindings);
    }

    /// <summary>初始化 Game 使用的引擎对象和组件绑定。</summary>
    private static void InitializeEngineBindings(OrbedenNativeApi api)
    {
        Ens.InitializeWorldNativeApi(api.World);
        PathDefines.InitializeNativeApi(api.PathDefines);
        Ens.InitializeEnsNativeApi(api.Ens);
        Object.InitializeNativeApi(api.Object, api.ObjectExtension);
        NativeBindingRuntime.Initialize(api.Bindings);
    }

    /// <summary>读取函数表前拒绝旧 ABI 或不同尺寸的引擎模块。</summary>
    private static void ValidateNativeApiHeader(IntPtr pointer, int expectedSize)
    {
        uint* header = (uint*)pointer;
        if (header[0] != 2 || header[1] != expectedSize)
            throw new TypeLoadException("Native API version or layout mismatch. Rebuild Core, Editor and game modules together.");
    }

    //在读取 C++ 函数表前验证托管 ABI 的固定尺寸。
    private static void ValidateNativeApiLayout()
    {
        if (nativeAbiValidated) return;

        ValidateSize<NativeBindingSlice>(nameof(NativeBindingSlice), IntPtr.Size == 8 ? 16 : 8);
        ValidateSize<NativeBindingBuffer>(nameof(NativeBindingBuffer), IntPtr.Size == 8 ? 16 : 8);
        ValidateSize<EnsId>(nameof(EnsId), 8);
        ValidateSize<vector2>(nameof(vector2), 8);
        ValidateSize<vector3>(nameof(vector3), 12);
        ValidateSize<quaternion>(nameof(quaternion), 16);
        ValidateSize<color>(nameof(color), 16);

        ValidateFunctionTable<WorldBindApi>(nameof(WorldBindApi), 4);
        ValidateFunctionTable<PathDefinesBindApi>(nameof(PathDefinesBindApi), 2);
        ValidateFunctionTable<EnsBindApi>(nameof(EnsBindApi), 6);
        ValidateFunctionTable<ObjectBindApi>(nameof(ObjectBindApi), 6);
        ValidateFunctionTable<NativeBindingsApi>(nameof(NativeBindingsApi), 10);
        ValidateFunctionTable<ObjectExtensionBindApi>(nameof(ObjectExtensionBindApi), 1);
        ValidateFunctionTable<RuntimeGuiApi>(nameof(RuntimeGuiApi), 11);
        ValidateFunctionTable<RuntimeGuiExtensionApi>(nameof(RuntimeGuiExtensionApi), 4);
        ValidateFunctionTable<RuntimeGuiAdvancedApi>(nameof(RuntimeGuiAdvancedApi), 17);
        ValidateFunctionTable<RuntimeGuiDrawApi>(nameof(RuntimeGuiDrawApi), 15);
        ValidateFunctionTable<NativeScriptInteropApi>(nameof(NativeScriptInteropApi), 9);
        ValidateFunctionTable<ManagedScriptInteropApi>(nameof(ManagedScriptInteropApi), 11);
        ValidateFunctionTable<ScriptBindApi>(nameof(ScriptBindApi), 16);
        ValidateSize<OrbedenEngineNativeApi>(nameof(OrbedenEngineNativeApi), 8 + 29 * IntPtr.Size);
        ValidateSize<OrbedenNativeApi>(nameof(OrbedenNativeApi), 8 + 101 * IntPtr.Size);

        nativeAbiValidated = true;
    }

    //验证一个可跨语言按值复制的 ABI 结构尺寸。
    private static void ValidateSize<T>(string name, int expectedSize) where T : unmanaged
    {
        if (sizeof(T) != expectedSize)
            throw new TypeLoadException($"{name} ABI size mismatch: expected {expectedSize}, actual {sizeof(T)}.");
    }

    //验证全由函数指针槽组成的函数表尺寸。
    private static void ValidateFunctionTable<T>(string name, int slotCount) where T : unmanaged
    {
        ValidateSize<T>(name, checked(slotCount * IntPtr.Size));
    }
}
