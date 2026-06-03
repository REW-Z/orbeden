#pragma once

#include "Platform/Window.h"

struct GLFWwindow;

class GlfwWindow : public IWindow
{
private:
    GLFWwindow* window = nullptr;
    IWindowResizeListener* resizeListener = nullptr;
    WindowDesc desc{};
    int32 width = 0;
    int32 height = 0;
    int32 framebufferWidth = 0;
    int32 framebufferHeight = 0;

public:
    GlfwWindow() = default;
    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;

    //确保窗口销毁时释放 GLFW 资源
    ~GlfwWindow() override;

    //创建 GLFW 窗口
    bool Create(const WindowDesc& desc) override;

    //销毁 GLFW 窗口
    void Destroy() override;

    //处理 GLFW 事件
    void PollEvents() override;

    //提交当前帧显示
    void Present() override;

    //判断窗口是否请求关闭
    bool ShouldClose() const override;

    //设置 resize 监听者
    void SetResizeListener(IWindowResizeListener* listener) override;

    //获取窗口逻辑宽度
    int32 GetWidth() const override;

    //获取窗口逻辑高度
    int32 GetHeight() const override;

    //获取 framebuffer 宽度
    int32 GetFramebufferWidth() const override;

    //获取 framebuffer 高度
    int32 GetFramebufferHeight() const override;

    //获取平台原生窗口句柄
    void* GetNativeHandle() const override;

    //获取 GLFW 窗口指针，供后续渲染后端对接
    GLFWwindow* GetGlfwWindow() const;

    //接收 GLFW framebuffer resize 回调
    void HandleFramebufferResize(int32 newWidth, int32 newHeight);

private:
    //同步 GLFW 当前窗口尺寸
    void RefreshSize();
};
