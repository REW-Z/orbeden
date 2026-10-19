#include "Rendering/ShaderCompiler.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Include/Types.h>
#include <SPIRV/GlslangToSpv.h>


namespace
{
    //初始化进程共享编译器
    bool InitializeCompiler()
    {
        static const bool initialized = glslang::InitializeProcess();
        return initialized;
    }

    //配置 GLSL 目标与自动参数布局
    bool ParseShader(glslang::TShader& shader, const char* source, bool vertex, std::string& error)
    {
        if (!source || !*source) { error = "Shader source is empty."; return false; }
        std::string text(source);
        std::string preamble = "#define ORBEDEN_VULKAN 1\n#define gl_VertexID gl_VertexIndex\n#define gl_InstanceID gl_InstanceIndex\n";
        if (vertex)
        {
            preamble += "#define main OrbedenVertexMain\n";
            text += "\n#undef main\nvoid main(){OrbedenVertexMain();gl_Position.z=0.5*(gl_Position.z+gl_Position.w);}\n";
        }
        else if (shader.getStage() == EShLangFragment)
        {
            preamble += R"(
uniform vec3 u_OrbedenFramebufferInfo;
vec4 OrbedenGetFragCoord()
{
    vec4 position = gl_FragCoord;
    if (u_OrbedenFramebufferInfo.y > 0.5) position.y = u_OrbedenFramebufferInfo.x - position.y;
    return position;
}
#define gl_FragCoord OrbedenGetFragCoord()
#define dFdy(v) (dFdy(v) * (u_OrbedenFramebufferInfo.y > 0.5 ? -1.0 : 1.0))
#define dFdyFine(v) (dFdyFine(v) * (u_OrbedenFramebufferInfo.y > 0.5 ? -1.0 : 1.0))
#define dFdyCoarse(v) (dFdyCoarse(v) * (u_OrbedenFramebufferInfo.y > 0.5 ? -1.0 : 1.0))
)";
        }
        shader.setPreamble(preamble.c_str());
        const char* sources[] = { text.c_str() };
        shader.setStrings(sources, 1);
        shader.setOverrideVersion(450);
        shader.setEnvInput(glslang::EShSourceGlsl, shader.getStage(), glslang::EShClientVulkan, 450);
        shader.setEnvInputVulkanRulesRelaxed();
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
        shader.setAutoMapBindings(true);
        shader.setAutoMapLocations(true);
        shader.setGlobalUniformBlockName("OrbedenParameters");
        shader.setGlobalUniformSet(0);
        shader.setGlobalUniformBinding(0);
        shader.setShiftBinding(glslang::EResSampler, 1);
        shader.setShiftBinding(glslang::EResTexture, 1);
        constexpr EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
        if (shader.parse(GetDefaultResources(), 450, false, messages)) return true;
        error = shader.getInfoLog();
        error += shader.getInfoDebugLog();
        return false;
    }

    //链接阶段并提取数值与纹理布局
    bool LinkShader(glslang::TProgram& program, CompiledShader& output, std::string& error)
    {
        constexpr EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
        if (!program.link(messages) || !program.mapIO() || !program.buildReflection(EShReflectionSeparateBuffers))
        {
            error = program.getInfoLog();
            error += program.getInfoDebugLog();
            return false;
        }
        for (int32 index = 0; index < program.getNumUniformBlocks(); ++index)
        {
            const auto& block = program.getUniformBlock(index);
            if (block.name != "OrbedenParameters" || block.getBinding() != 0)
            {
                error = "Explicit uniform blocks require an engine parameter layout: " + block.name;
                return false;
            }
            output.parameterSize = std::max(output.parameterSize, static_cast<uint32>(block.size));
        }
        if (program.getNumBufferBlocks() != 0 && (!program.getIntermediate(EShLangCompute)
            || program.getNumBufferBlocks() != 1 || program.getBufferBlock(0).getBinding() != 31))
        {
            error = "Storage buffers require a backend resource layout.";
            return false;
        }
        for (int32 index = 0; index < program.getNumUniformVariables(); ++index)
        {
            const auto& reflected = program.getUniform(index);
            const glslang::TType& type = *reflected.getType();
            if (type.getBasicType() == glslang::EbtSampler)
            {
                if (type.isArray() || !type.getSampler().isCombined() || type.getSampler().arrayed || type.getSampler().ms || type.getSampler().shadow
                    || (type.getQualifier().hasSet() && type.getQualifier().layoutSet != 0)
                    || (type.getSampler().dim != glslang::Esd2D && type.getSampler().dim != glslang::EsdCube))
                {
                    error = "Unsupported shader sampler layout: " + reflected.name;
                    return false;
                }
                output.samplers.push_back({ reflected.name, static_cast<uint32>(reflected.getBinding()), type.getSampler().dim == glslang::EsdCube });
                continue;
            }
            ShaderParameterLayout parameter;
            parameter.name = reflected.name;
            parameter.offset = static_cast<uint32>(reflected.offset);
            parameter.count = static_cast<uint32>(std::max(type.isArray() ? type.getOuterArraySize() : reflected.size, 1));
            parameter.stride = static_cast<uint32>(std::max(reflected.arrayStride, 0));
            if (type.isMatrix() && type.getMatrixCols() == 4 && type.getMatrixRows() == 4) parameter.type = ShaderParameterType::Matrix4;
            else if (type.getBasicType() == glslang::EbtFloat && type.getVectorSize() == 4) parameter.type = ShaderParameterType::Color;
            else if (type.getBasicType() == glslang::EbtFloat && type.getVectorSize() == 3) parameter.type = ShaderParameterType::Vector3;
            else if (type.getBasicType() == glslang::EbtFloat && type.getVectorSize() == 1) parameter.type = ShaderParameterType::Float;
            else if ((type.getBasicType() == glslang::EbtInt || type.getBasicType() == glslang::EbtBool) && type.getVectorSize() == 1) parameter.type = ShaderParameterType::Int;
            else { error = "Unsupported shader parameter type: " + parameter.name; return false; }
            output.parameters.push_back(std::move(parameter));
        }
        glslang::SpvOptions options;
        options.disableOptimizer = true;
        if (auto intermediate = program.getIntermediate(EShLangVertex)) glslang::GlslangToSpv(*intermediate, output.vertex, &options);
        if (auto intermediate = program.getIntermediate(EShLangFragment)) glslang::GlslangToSpv(*intermediate, output.fragment, &options);
        if (auto intermediate = program.getIntermediate(EShLangCompute)) glslang::GlslangToSpv(*intermediate, output.compute, &options);
        return true;
    }
}

//编译 Vulkan 图形阶段并反射参数布局
bool ShaderCompiler::CompileGraphics(const GpuShaderProgramDesc& source, CompiledShader& output, std::string& error)
{
    output = {};
    error.clear();
    if (!InitializeCompiler()) { error = "GLSL compiler initialization failed."; return false; }
    glslang::TShader vertex(EShLangVertex), fragment(EShLangFragment);
    if (!ParseShader(vertex, source.vertexSource, true, error) || !ParseShader(fragment, source.fragmentSource, false, error)) return false;
    glslang::TProgram program;
    program.addShader(&vertex);
    program.addShader(&fragment);
    return LinkShader(program, output, error);
}

//编译 Vulkan 计算阶段并反射参数布局
bool ShaderCompiler::CompileCompute(const char* source, CompiledShader& output, std::string& error)
{
    output = {};
    error.clear();
    if (!InitializeCompiler()) { error = "GLSL compiler initialization failed."; return false; }
    glslang::TShader compute(EShLangCompute);
    if (!ParseShader(compute, source, false, error)) return false;
    glslang::TProgram program;
    program.addShader(&compute);
    return LinkShader(program, output, error);
}
