#pragma once

#include <string>

#include "Runtime/EngineTypes.h"

//一条有序输入事件。字段与 UIInputRecord 一一对应，由桥接层翻译过去；
//kind/device/key 的取值见 Runtime/Gui/RetainedGuiTypes.h 的同名枚举。
//这里不直接引用那份头文件：输入系统是引擎的通用设施，不该绑定在 UI 合同上。
struct InputEvent
{
    //到达序号；由输入系统分配，单调递增且保留到达顺序。
    uint64 sequence = 0;

    //来源窗口。
    uint32 windowId = 0;

    //指针标识；0 为鼠标，触摸从 1 开始。
    uint32 pointerId = 0;

    //事件种类，取 UIInputKind。
    uint32 kind = 0;

    //设备，取 UIInputDevice。
    uint32 device = 0;

    //按键或按钮标识，含义由设备决定。
    uint32 key = 0;

    //原始平台键码；KeyEnum 覆盖不到时按它判断。
    uint32 rawKey = 0;

    //修饰键位：Shift=1、Control=2、Alt=4。
    uint32 modifiers = 0;

    //窗口逻辑坐标。
    vector2 position;

    //本帧增量。
    vector2 delta;

    //数值；滚轮与手柄轴用它。
    float32 value = 0.0f;

    //文本提交的 UTF-8 内容。
    std::string text;

    //组合态光标所在的标量下标。
    uint32 caretScalar = 0;

    //平台时间戳，秒。
    double timestamp = 0.0;
};
