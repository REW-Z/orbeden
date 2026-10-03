#pragma once

#include <vector>

#include "Application.h"
#include "InputManager/InputEvent.h"
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
    HOME,
    END,
    //DELETE 在 windows.h 里是宏，这里退一格命名，语义就是 Delete 键。
    DEL,
    ESCAPE,

    UNMAPPED,
};

class InputManager : public IEngineSystem
{
public:
    //设置输入系统是否接收平台事件
    static void SetEnabled(bool value);

    //判断输入系统是否接收平台事件
    static bool IsEnabled();

    //清理本帧瞬时输入状态；跨帧占有与未消费的事件都不受影响
    static void BeginFrame();

    //写入按键状态，供平台回调调用
    static void SetKeyState(KeyEnum key, bool pressed);

    //写入原始键状态；与 KeyEnum 无关，供编辑器与输入模块查询
    static void SetRawKeyState(uint32 rawKey, bool pressed);

    //写入鼠标位置，供平台回调调用
    static void SetMousePosition(float32 x, float32 y);

    //写入一条有序事件；序号由输入系统分配，保留到达顺序
    static void PushEvent(InputEvent event);

    //读取尚未被消费的事件；消费之前跨帧保留
    static const std::vector<InputEvent>& GetFrameEvents();

    //标记事件已消费；被消费的按下会占有该键，直到对应的抬起
    static void MarkEventHandled(uint64 sequence);

    //读取原始键的持续按下状态；不受 UI 消费影响
    static bool RawKey(uint32 rawKey);

    //读取原始键的本帧按下状态
    static bool RawKeyDown(uint32 rawKey);

    //读取原始键的本帧抬起状态
    static bool RawKeyUp(uint32 rawKey);

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
