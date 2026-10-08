#pragma once

#include "Platform/Window.h"

//OpenGL 上下文和 GLAD 初始化
class OpenGLContext
{
private:
    bool initialized = false;

public:
    //初始化 OpenGL 函数入口
    bool Initialize(IWindow* window);

    //判断是否已经初始化
    bool IsInitialized() const;
};

