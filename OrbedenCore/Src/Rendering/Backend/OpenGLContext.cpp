#include "Rendering/Backend/OpenGLContext.h"

#include "Log/Log.h"
#include "Platform/GlfwWindow.h"
#include <glad/gl.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace
{
    GLADapiproc LoadOpenGLProc(const char* name)
    {
        return reinterpret_cast<GLADapiproc>(glfwGetProcAddress(name));
    }
}

bool OpenGLContext::Initialize(IWindow* window)
{
    if (initialized) return true;

    GlfwWindow* glfwWindow = dynamic_cast<GlfwWindow*>(window);
    if (!glfwWindow || !glfwWindow->GetGlfwWindow())
    {
        Log::Error("OpenGLContext initialize failed: GLFW window is missing.");
        return false;
    }

    glfwMakeContextCurrent(glfwWindow->GetGlfwWindow());
    if (!gladLoadGL(LoadOpenGLProc))
    {
        Log::Error("OpenGLContext initialize failed: GLAD load failed.");
        return false;
    }

    initialized = true;
    return true;
}

bool OpenGLContext::IsInitialized() const
{
    return initialized;
}
