#include "Platform/InputManager.h"

#include <array>

namespace
{
    constexpr usize KeyCount = static_cast<usize>(KeyEnum::UNMAPPED) + 1;

    std::array<bool, KeyCount> keyStates{};
    std::array<bool, KeyCount> keyDownThisFrame{};
    std::array<bool, KeyCount> keyUpThisFrame{};
    vector2 mousePos{};
    vector2 mouseMov{};
    bool inputEnabled = true;

    bool IsValidKey(KeyEnum key)
    {
        usize index = static_cast<usize>(key);
        return index < KeyCount && key != KeyEnum::UNMAPPED;
    }

    void ClearInputState()
    {
        keyStates.fill(false);
        keyDownThisFrame.fill(false);
        keyUpThisFrame.fill(false);
        mouseMov = vector2{};
    }
}

//设置输入系统是否接收平台事件
void InputManager::SetEnabled(bool value)
{
    inputEnabled = value;
    if (!inputEnabled)
    {
        ClearInputState();
    }
}

//判断输入系统是否接收平台事件
bool InputManager::IsEnabled()
{
    return inputEnabled;
}

//清理本帧瞬时输入状态
void InputManager::BeginFrame()
{
    mouseMov = vector2{};
    keyDownThisFrame.fill(false);
    keyUpThisFrame.fill(false);
}

//写入按键状态
void InputManager::SetKeyState(KeyEnum key, bool pressed)
{
    if (!inputEnabled) return;
    if (!IsValidKey(key)) return;

    usize index = static_cast<usize>(key);
    bool wasPressed = keyStates[index];
    keyStates[index] = pressed;

    if (pressed && !wasPressed)
    {
        keyDownThisFrame[index] = true;
    }
    else if (!pressed && wasPressed)
    {
        keyUpThisFrame[index] = true;
    }
}

//写入鼠标位置
void InputManager::SetMousePosition(float32 x, float32 y)
{
    if (!inputEnabled) return;
    mouseMov.x += x - mousePos.x;
    mouseMov.y += y - mousePos.y;
    mousePos.x = x;
    mousePos.y = y;
}

//读取持续按下状态
bool InputManager::Key(KeyEnum key)
{
    return inputEnabled && IsValidKey(key) && keyStates[static_cast<usize>(key)];
}

//读取本帧按下状态
bool InputManager::KeyDown(KeyEnum key)
{
    return inputEnabled && IsValidKey(key) && keyDownThisFrame[static_cast<usize>(key)];
}

//读取本帧抬起状态
bool InputManager::KeyUp(KeyEnum key)
{
    return inputEnabled && IsValidKey(key) && keyUpThisFrame[static_cast<usize>(key)];
}

//读取鼠标当前位置
vector2 InputManager::MousePos()
{
    return inputEnabled ? mousePos : vector2{};
}

//读取鼠标本帧移动量
vector2 InputManager::MouseMov()
{
    return inputEnabled ? mouseMov : vector2{};
}

//读取持续按下状态
bool Input::Key(KeyEnum key)
{
    return InputManager::Key(key);
}

//读取本帧按下状态
bool Input::KeyDown(KeyEnum key)
{
    return InputManager::KeyDown(key);
}

//读取本帧抬起状态
bool Input::KeyUp(KeyEnum key)
{
    return InputManager::KeyUp(key);
}

//读取鼠标当前位置
vector2 Input::MousePos()
{
    return InputManager::MousePos();
}

//读取鼠标本帧移动量
vector2 Input::MouseMov()
{
    return InputManager::MouseMov();
}
