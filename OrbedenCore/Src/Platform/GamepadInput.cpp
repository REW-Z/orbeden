#include "Platform/GamepadInput.h"

#include <cmath>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "InputManager/InputEvent.h"
#include "InputManager/InputManager.h"

namespace
{
    //事件种类与设备；与 UIInputKind/UIInputDevice 的数值一致。
    constexpr uint32 EventKindGamepadState = 13;
    constexpr uint32 DeviceGamepad = 2;

    //手柄的键；与 UIGamepadKey 一致。
    constexpr uint32 GamepadAxisX = 0;
    constexpr uint32 GamepadAxisY = 1;
    constexpr uint32 GamepadSubmit = 2;
    constexpr uint32 GamepadCancel = 3;

    //轴的变化阈值：小于它视为没动，避免摇杆噪声每帧产生事件。
    constexpr float32 AxisEpsilon = 0.001f;
    //死区内的值一律上报成零，方向导航由此得到干净的按下/释放边沿。
    constexpr float32 AxisDeadZone = 0.2f;

    //一路手柄的上一次状态。
    struct GamepadState
    {
        bool present = false;
        float32 axisX = 0.0f;
        float32 axisY = 0.0f;
        bool submit = false;
        bool cancel = false;
    };

    GamepadState previous[GamepadInput::MaxGamepads];

    //把死区内的值压成零，保留方向符号。
    float32 ApplyDeadZone(float32 value)
    {
        if (std::fabs(value) < AxisDeadZone) return 0.0f;
        return value;
    }

    //D-pad 有按下时按满量程参与轴值，方向导航与摇杆共用两个轴。
    float32 CombineAxis(float32 stick, bool negative, bool positive)
    {
        if (negative && !positive) return -1.0f;
        if (positive && !negative) return 1.0f;
        return stick;
    }

    void PushState(uint32 key, float32 value)
    {
        InputEvent event;
        event.kind = EventKindGamepadState;
        event.device = DeviceGamepad;
        event.key = key;
        event.value = value;
        InputManager::PushEvent(std::move(event));
    }

    //键盘与轴各自比较变化量后上报；没有变化就不产生事件。
    void ReportChanges(const GamepadState& current, const GamepadState& last)
    {
        if (std::fabs(current.axisX - last.axisX) > AxisEpsilon) PushState(GamepadAxisX, current.axisX);
        if (std::fabs(current.axisY - last.axisY) > AxisEpsilon) PushState(GamepadAxisY, current.axisY);
        //按键按值上报：按下为 1、释放为 0，上层按边沿判定。
        if (current.submit != last.submit) PushState(GamepadSubmit, current.submit ? 1.0f : 0.0f);
        if (current.cancel != last.cancel) PushState(GamepadCancel, current.cancel ? 1.0f : 0.0f);
    }
}

void GamepadInput::Poll()
{
    for (int32 index = 0; index < MaxGamepads; ++index)
    {
        GamepadState& last = previous[index];
        int32 joystick = GLFW_JOYSTICK_1 + index;
        if (!glfwJoystickPresent(joystick) || !glfwJoystickIsGamepad(joystick))
        {
            //断连：把还按着的轴与键全部释放，上层据此取消导航重复。
            if (last.present)
            {
                GamepadState released;
                released.present = true;
                ReportChanges(released, last);
                last = GamepadState{};
            }
            continue;
        }

        GLFWgamepadstate state{};
        if (!glfwGetGamepadState(joystick, &state))
        {
            continue;
        }

        GamepadState current;
        current.present = true;
        current.axisX = CombineAxis(ApplyDeadZone(state.axes[GLFW_GAMEPAD_AXIS_LEFT_X]),
            state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] == GLFW_PRESS,
            state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] == GLFW_PRESS);
        //屏幕坐标 Y 向上，手柄摇杆向上为正，两者方向一致，直接取用。
        current.axisY = CombineAxis(ApplyDeadZone(state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]),
            state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] == GLFW_PRESS,
            state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP] == GLFW_PRESS);
        current.submit = state.buttons[GLFW_GAMEPAD_BUTTON_A] == GLFW_PRESS;
        current.cancel = state.buttons[GLFW_GAMEPAD_BUTTON_B] == GLFW_PRESS;

        //首次出现时把全部状态上报一遍，上层不必区分“刚接入”与“没变化”。
        ReportChanges(current, last.present ? last : GamepadState{});
        last = current;
    }
}

void GamepadInput::Reset()
{
    for (GamepadState& state : previous) state = GamepadState{};
}
