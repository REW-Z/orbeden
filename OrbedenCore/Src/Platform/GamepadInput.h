#pragma once

#include "Defines/types.h"

//手柄输入。只上报状态事件，不合成按键事件：轴与键各成一条 GamepadState，
//键值取 UIGamepadKey（AxisX/AxisY/Submit/Cancel），数值放在事件的 value 里。
//断连时生成全部释放，让上层取消导航重复。
namespace GamepadInput
{
    //同时跟踪的手柄数量上限。
    constexpr int32 MaxGamepads = 4;

    //轮询全部手柄并推入事件；每帧调用一次。
    void Poll();

    //清除全部跟踪状态；输入系统关闭时调用。
    void Reset();
}
