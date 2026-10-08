using System.Runtime.InteropServices;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorInputNativeApi
{
    public delegate* unmanaged[Cdecl]<int, byte> IsKeyDown;
    public delegate* unmanaged[Cdecl]<int, byte> IsKeyPressed;
}
#pragma warning restore CS0649

/// <summary>
/// 编辑器键盘状态。场景手柄要在拖动中途响应 Escape 这类按键，
/// 而 ImGui 的条目状态只管鼠标，因此单独开一条只读通道。
/// </summary>
public static unsafe class EditorInput
{
    internal static void Initialize(EditorInputNativeApi value)
    {
        api = value;
        initialized = api.IsKeyDown != null;
    }

    internal static void Shutdown()
    {
        api = default;
        initialized = false;
    }

    /// <summary>按键是否按住。</summary>
    public static bool IsKeyDown(EditorKey key) =>
        initialized && api.IsKeyDown != null && api.IsKeyDown((int)key) != 0;

    /// <summary>按键这一帧是否刚按下。</summary>
    public static bool IsKeyPressed(EditorKey key) =>
        initialized && api.IsKeyPressed != null && api.IsKeyPressed((int)key) != 0;

    private static EditorInputNativeApi api;
    private static bool initialized;
}

/// <summary>
/// 编辑器用得到的按键。数值就是 ImGuiKey 的取值，只能按 ImGui 的定义追加，不能重排。
/// </summary>
public enum EditorKey
{
    /// <summary>Delete。</summary>
    Delete = 522,

    /// <summary>Escape。</summary>
    Escape = 526,

    /// <summary>左 Ctrl。</summary>
    LeftCtrl = 527,

    /// <summary>左 Shift。</summary>
    LeftShift = 528,

    /// <summary>左 Alt。</summary>
    LeftAlt = 529,
}
