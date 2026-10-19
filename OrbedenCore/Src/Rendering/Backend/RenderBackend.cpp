#include "Rendering/Backend/RenderBackend.h"

#include "Rendering/Backend/OpenGLRenderBackend.h"
#include "Rendering/Backend/VulkanRenderBackend.h"
#include "Profiler/Profiler.h"

#include <stdexcept>

//按窗口图形 API 创建后端
std::unique_ptr<RenderBackend> RenderBackend::Create(WindowGraphicsApi api)
{
    switch (api)
    {
    case WindowGraphicsApi::OpenGL: return std::make_unique<OpenGLRenderBackend>();
    case WindowGraphicsApi::Vulkan: return std::make_unique<VulkanRenderBackend>();
    default: return nullptr;
    }
}

//读取启动参数中的图形 API 选项
bool RenderBackend::ReadGraphicsApi(std::span<const std::string_view> arguments, WindowGraphicsApi& api, std::string& error)
{
    for (usize index = 0; index < arguments.size(); ++index)
    {
        std::string_view value;
        if (arguments[index] == "--graphics-api")
        {
            if (++index == arguments.size()) { error = "--graphics-api requires opengl or vulkan."; return false; }
            value = arguments[index];
        }
        else if (arguments[index].starts_with("--graphics-api=")) value = arguments[index].substr(15);
        else continue;
        if (value == "opengl") api = WindowGraphicsApi::OpenGL;
        else if (value == "vulkan") api = WindowGraphicsApi::Vulkan;
        else { error = "Unknown graphics API: " + std::string(value); return false; }
    }
    return true;
}

//选择着色器与对应参数块
void RenderBackend::BindShaderProgram(GpuShaderProgramID id)
{
    if (activeParameterProgram != id.id)
    {
        activeParameters = id.IsValid() ? &shaderParameters[id.id] : nullptr;
        activeParameterProgram = id.id;
    }
    OnBindShaderProgram(id);
}

//获取当前着色器参数块
ShaderParameters& RenderBackend::GetParameters()
{
    if (!activeParameters) throw std::logic_error("Shader parameters require a bound program.");
    return *activeParameters;
}

//移除着色器参数缓存
void RenderBackend::ReleaseShaderParameters(GpuShaderProgramID id)
{
    auto found = shaderParameters.find(id.id);
    if (found == shaderParameters.end()) return;
    if (activeParameters == &found->second) { activeParameters = nullptr; activeParameterProgram = 0; }
    shaderParameters.erase(found);
}

//记录 GPU 完成等待耗时
void RenderBackend::ProfileGpuWait(const char* name)
{
    if (!Profiler::IsCapturing()) return;
    PROFILE(name);
    WaitForIdle();
}
