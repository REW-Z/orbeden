#pragma once

#include "Application.h"
#include "Runtime/EngineTypes.h"

//沿用旧工程输入枚举，方便迁移已有玩法代码
enum KeyEnum
{
    MOUSEL,
    MOUSER,
    MOUSEMID,

    NUM1,
    NUM2,
    NUM3,
    NUM4,
    NUM5,
    NUM6,
    NUM7,
    NUM8,
    NUM9,
    NUM0,
    Q,
    W,
    E,
    R,
    T,
    Y,
    U,
    I,
    O,
    P,
    A,
    S,
    D,
    F,
    G,
    H,
    J,
    K,
    L,
    Z,
    X,
    C,
    V,
    B,
    N,
    M,
    SPACE,
    TAB,
    LSHIFT,
    LCTRL,
    LALT,
    BACKSPACE,
    ENTER,
    RSHIFT,
    RCTRL,
    RALT,
    UP,
    DOWN,
    LEFT,
    RIGHT,

    UNMAPPED,
};

class InputManager : public IEngineSystem
{
public:
    //进入新帧时清理瞬时输入状态
    void OnBeginFrame() override;

    //设置输入系统是否接收平台事件
    static void SetEnabled(bool value);

    //判断输入系统是否接收平台事件
    static bool IsEnabled();

    //清理本帧瞬时输入状态
    static void BeginFrame();

    //写入按键状态，供平台回调调用
    static void SetKeyState(KeyEnum key, bool pressed);

    //写入鼠标位置，供平台回调调用
    static void SetMousePosition(float32 x, float32 y);

    //读取持续按下状态
    static bool Key(KeyEnum key);

    //读取本帧按下状态
    static bool KeyDown(KeyEnum key);

    //读取本帧抬起状态
    static bool KeyUp(KeyEnum key);

    //读取鼠标当前位置
    static vector2 MousePos();

    //读取鼠标本帧移动量
    static vector2 MouseMov();
};

class Input
{
public:
    //读取持续按下状态
    static bool Key(KeyEnum key);

    //读取本帧按下状态
    static bool KeyDown(KeyEnum key);

    //读取本帧抬起状态
    static bool KeyUp(KeyEnum key);

    //读取鼠标当前位置
    static vector2 MousePos();

    //读取鼠标本帧移动量
    static vector2 MouseMov();
};
