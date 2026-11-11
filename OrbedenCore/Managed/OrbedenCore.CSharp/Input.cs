using System.Runtime.InteropServices;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct InputBindApi
{
    public delegate* unmanaged[Cdecl]<int, byte> Key;
    public delegate* unmanaged[Cdecl]<int, byte> KeyDown;
    public delegate* unmanaged[Cdecl]<int, byte> KeyUp;
    public delegate* unmanaged[Cdecl]<vector2*, void> MousePos;
    public delegate* unmanaged[Cdecl]<vector2*, void> MouseMov;
    public delegate* unmanaged[Cdecl]<float> MouseWheel;
}
#pragma warning restore CS0649

/// <summary>查询未被 UI 占有的游戏输入</summary>
public static unsafe class Input
{
    private static InputBindApi api;

    //保存原生输入函数表
    internal static void InitializeNativeApi(InputBindApi value) => api = value;

    /// <summary>读取按键或鼠标按钮的持续按下状态</summary>
    public static bool Key(KeyEnum key) => api.Key != null && api.Key((int)key) != 0;

    /// <summary>读取按键或鼠标按钮的本帧按下状态</summary>
    public static bool KeyDown(KeyEnum key) => api.KeyDown != null && api.KeyDown((int)key) != 0;

    /// <summary>读取按键或鼠标按钮的本帧抬起状态</summary>
    public static bool KeyUp(KeyEnum key) => api.KeyUp != null && api.KeyUp((int)key) != 0;

    /// <summary>读取鼠标在窗口中的逻辑坐标</summary>
    public static vector2 MousePos()
    {
        vector2 position = default;
        if (api.MousePos != null)
        {
            api.MousePos(&position);
        }
        return position;
    }

    /// <summary>读取鼠标本帧移动量</summary>
    public static vector2 MouseMov()
    {
        vector2 movement = default;
        if (api.MouseMov != null)
        {
            api.MouseMov(&movement);
        }
        return movement;
    }

    /// <summary>读取本帧未被 UI 消费的纵向滚轮刻度，向上为正；重复读取不消费</summary>
    public static float MouseWheel() => api.MouseWheel != null ? api.MouseWheel() : 0;
}
