#include "InputManager/InputManager.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Log/Log.h"

namespace
{
    constexpr usize KeyCount = static_cast<usize>(KeyEnum::UNMAPPED) + 1;
    //单帧事件的上限；超出丢最旧的。
    constexpr usize MaxPendingEvents = 4096;

    //事件种类与设备；与 UIInputKind/UIInputDevice 的数值一致，这里不引用 UI 合同头文件。
    constexpr uint32 EventKindPointerDown = 1;
    constexpr uint32 EventKindPointerUp = 2;
    constexpr uint32 EventKindPointerCancel = 3;
    constexpr uint32 EventKindKeyDown = 5;
    constexpr uint32 EventKindKeyUp = 6;
    constexpr uint32 EventKindWindowFocusLost = 12;
    constexpr uint32 DeviceMouse = 0;
    constexpr uint32 DeviceKeyboard = 3;

    //UI 对某个键或指针按钮的占有：占有期间游戏查询看不到按下；
    //即使 UI 中途放手，物理键还按着也继续屏蔽到真正抬起。
    struct KeyOwnership
    {
        bool owned = false;
        bool blockedUntilRelease = false;
    };

    std::array<bool, KeyCount> keyStates{};
    std::array<bool, KeyCount> keyDownThisFrame{};
    std::array<bool, KeyCount> keyUpThisFrame{};
    std::array<KeyOwnership, KeyCount> keyOwnership{};
    //原始键按平台键码索引，与 KeyEnum 无关。
    std::unordered_map<uint32, bool> rawKeyStates;
    std::unordered_set<uint32> rawKeyDownThisFrame;
    std::unordered_set<uint32> rawKeyUpThisFrame;
    //本帧有序事件；帧尾丢弃，消费只控制游戏输入占有。
    std::vector<InputEvent> pendingEvents;
    uint64 nextEventSequence = 1;
    vector2 mousePos{};
    vector2 mouseMov{};
    bool inputEnabled = true;
    bool eventOverflowReported = false;

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
        keyOwnership.fill(KeyOwnership{});
        rawKeyStates.clear();
        rawKeyDownThisFrame.clear();
        rawKeyUpThisFrame.clear();
        mouseMov = vector2{};
    }

    //占有：按下被 UI 消费后，游戏不再看到这个键。
    void AcquireKey(KeyEnum key)
    {
        if (!IsValidKey(key)) return;
        KeyOwnership& ownership = keyOwnership[static_cast<usize>(key)];
        ownership.owned = true;
        ownership.blockedUntilRelease = true;
    }

    //放手：物理键仍按着时转为“屏蔽至抬起”，避免游戏看到没有按下来源的持续态。
    void ReleaseKey(KeyEnum key)
    {
        if (!IsValidKey(key)) return;
        usize index = static_cast<usize>(key);
        KeyOwnership& ownership = keyOwnership[index];
        ownership.owned = false;
        ownership.blockedUntilRelease = keyStates[index];
    }

    //占有或屏蔽中的键对游戏不可见。
    bool IsSuppressed(KeyEnum key)
    {
        usize index = static_cast<usize>(key);
        return keyOwnership[index].owned || keyOwnership[index].blockedUntilRelease;
    }
}

//设置输入系统是否接收平台事件
void InputManager::SetEnabled(bool value)
{
    inputEnabled = value;
    if (!inputEnabled)
    {
        ClearInputState();
        pendingEvents.clear();
        eventOverflowReported = false;
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
    //保留等待事件期间采集的事件与跨帧键占有
    rawKeyDownThisFrame.clear();
    rawKeyUpThisFrame.clear();
}

//丢弃本帧已交付的事件
void InputManager::EndFrame()
{
    pendingEvents.clear();
    eventOverflowReported = false;
}

//写入按键状态
void InputManager::SetKeyState(KeyEnum key, bool pressed)
{
    if (!inputEnabled) return;
    if (!IsValidKey(key)) return;

    usize index = static_cast<usize>(key);
    bool wasPressed = keyStates[index];
    keyStates[index] = pressed;
    //物理抬起是屏蔽的终点：之后这个键重新对游戏可见。
    if (!pressed) keyOwnership[index].blockedUntilRelease = false;

    if (pressed && !wasPressed)
    {
        keyDownThisFrame[index] = true;
    }
    else if (!pressed && wasPressed)
    {
        keyUpThisFrame[index] = true;
    }
}

//写入原始键状态
void InputManager::SetRawKeyState(uint32 rawKey, bool pressed)
{
    if (!inputEnabled) return;

    bool wasPressed = rawKeyStates[rawKey];
    rawKeyStates[rawKey] = pressed;
    if (pressed && !wasPressed)
    {
        rawKeyDownThisFrame.insert(rawKey);
    }
    else if (!pressed && wasPressed)
    {
        rawKeyUpThisFrame.insert(rawKey);
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

//写入一条有序事件
void InputManager::PushEvent(InputEvent event)
{
    if (!inputEnabled) return;

    if (pendingEvents.size() >= MaxPendingEvents)
    {
        //丢最旧的，新事件仍然保持到达顺序。
        pendingEvents.erase(pendingEvents.begin());
        if (!eventOverflowReported)
        {
            Log::Error("InputManager event queue overflowed; the oldest events were dropped.");
            eventOverflowReported = true;
        }
    }

    event.sequence = nextEventSequence++;
    if (nextEventSequence == 0) nextEventSequence = 1;
    pendingEvents.push_back(std::move(event));
}

//读取本帧尚未被消费的事件
const std::vector<InputEvent>& InputManager::GetFrameEvents()
{
    return pendingEvents;
}

//标记事件已消费
void InputManager::MarkEventHandled(uint64 sequence)
{
    auto found = std::find_if(pendingEvents.begin(), pendingEvents.end(),
        [sequence](const InputEvent& event) { return event.sequence == sequence; });
    if (found == pendingEvents.end()) return;

    //只有键盘与鼠标的 key 是 KeyEnum 值；手柄的 key 是另一套编号，不能当键占有。
    const bool keyedDevice = found->device == DeviceMouse || found->device == DeviceKeyboard;

    //消费按下即为占有；消费抬起或取消即放手；失焦时全部放手。
    switch (found->kind)
    {
    case EventKindKeyDown:
    case EventKindPointerDown:
        if (keyedDevice) AcquireKey(static_cast<KeyEnum>(found->key));
        break;
    case EventKindKeyUp:
    case EventKindPointerUp:
    case EventKindPointerCancel:
        if (keyedDevice) ReleaseKey(static_cast<KeyEnum>(found->key));
        break;
    case EventKindWindowFocusLost:
        for (usize index = 0; index < KeyCount; ++index) ReleaseKey(static_cast<KeyEnum>(index));
        break;
    default:
        break;
    }

    //事件已经交付完毕，从队列里摘掉。
    pendingEvents.erase(found);
}

//读取原始键的持续按下状态
bool InputManager::RawKey(uint32 rawKey)
{
    if (!inputEnabled) return false;
    auto found = rawKeyStates.find(rawKey);
    return found != rawKeyStates.end() && found->second;
}

//读取原始键的本帧按下状态
bool InputManager::RawKeyDown(uint32 rawKey)
{
    return inputEnabled && rawKeyDownThisFrame.count(rawKey) != 0;
}

//读取原始键的本帧抬起状态
bool InputManager::RawKeyUp(uint32 rawKey)
{
    return inputEnabled && rawKeyUpThisFrame.count(rawKey) != 0;
}

//读取持续按下状态
bool InputManager::Key(KeyEnum key)
{
    return inputEnabled && IsValidKey(key)
        && keyStates[static_cast<usize>(key)] && !IsSuppressed(key);
}

//读取本帧按下状态
bool InputManager::KeyDown(KeyEnum key)
{
    return inputEnabled && IsValidKey(key)
        && keyDownThisFrame[static_cast<usize>(key)] && !IsSuppressed(key);
}

//读取本帧抬起状态
bool InputManager::KeyUp(KeyEnum key)
{
    return inputEnabled && IsValidKey(key)
        && keyUpThisFrame[static_cast<usize>(key)] && !IsSuppressed(key);
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
