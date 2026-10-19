#pragma once

#include "Rendering/Backend/RenderBackend.h"

//编译后的数值参数布局
struct ShaderParameterLayout
{
    std::string name;
    ShaderParameterType type = ShaderParameterType::Float;
    uint32 offset = 0;
    uint32 count = 1;
    uint32 stride = 0;
};

//编译后的采样资源绑定
struct ShaderSamplerLayout
{
    std::string name;
    uint32 binding = 0;
    bool cube = false;
};

//跨阶段链接后的 SPIR-V 与参数布局
struct CompiledShader
{
    List<uint32> vertex;
    List<uint32> fragment;
    List<uint32> compute;
    List<ShaderParameterLayout> parameters;
    List<ShaderSamplerLayout> samplers;
    uint32 parameterSize = 0;
};

//共享 GLSL 编译与布局反射入口
class ShaderCompiler
{
public:
    //编译 Vulkan 图形阶段并反射参数布局
    static bool CompileGraphics(const GpuShaderProgramDesc& source, CompiledShader& output, std::string& error);
    //编译 Vulkan 计算阶段并反射参数布局
    static bool CompileCompute(const char* source, CompiledShader& output, std::string& error);
};
