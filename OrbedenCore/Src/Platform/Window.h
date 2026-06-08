#pragma once

#include <string>

#include "Defines/types.h"

enum class WindowGraphicsApi
{
    None,
    OpenGL,
    Vulkan,
};

struct WindowDesc
{
    int32 width = 1280;
    int32 height = 720;
    std::string title = "Orbeden";
    WindowGraphicsApi graphicsApi = WindowGraphicsApi::None;
    bool resizable = true;
    bool vsync = false;
};

class IWindowResizeListener
{
public:
    virtual ~IWindowResizeListener() = default;

    //窗口 framebuffer 尺寸变化时调用
    virtual void OnWindowResize(int32 width, int32 height) = 0;
};

class IWindow
{
public:
    virtual ~IWindow() = default;

    //创建平台窗口
    virtual bool Create(const WindowDesc& desc) = 0;

    //销毁平台窗口
    virtual void Destroy() = 0;

    //处理平台事件
    virtual void PollEvents() = 0;

    //提交当前帧显示
    virtual void Present() = 0;

    //判断窗口是否请求关闭
    virtual bool ShouldClose() const = 0;

    //设置 resize 监听者
    virtual void SetResizeListener(IWindowResizeListener* listener) = 0;

    //获取窗口逻辑宽度
    virtual int32 GetWidth() const = 0;

    //获取窗口逻辑高度
    virtual int32 GetHeight() const = 0;

    //获取 framebuffer 宽度
    virtual int32 GetFramebufferWidth() const = 0;

    //获取 framebuffer 高度
    virtual int32 GetFramebufferHeight() const = 0;

    //获取窗口图形 API
    virtual WindowGraphicsApi GetGraphicsApi() const = 0;

    //获取平台原生窗口句柄
    virtual void* GetNativeHandle() const = 0;
};
