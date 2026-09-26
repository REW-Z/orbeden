#include "Rendering/OutputPass.h"

#include "Log/Log.h"

namespace
{
    //AgX 基础对比曲线，拟合自 Troy Sobotka 的 AgX。
    //输入已归一化到 [0,1]；常量取自公开的极简 GLSL 实现，两组矩阵是精确互逆，
    //且在 GLSL 的列主序 mat3 约定下保白（列和均为 1）。改动任一组都会让色相偏移。
    const char* OutputFragmentSource = R"(#version 430 core
in vec2 v_Uv;
uniform sampler2D u_SourceTexture;
uniform float u_Exposure;
uniform int u_Mode;
out vec4 FragColor;

const mat3 AgxInsetMatrix = mat3(
    0.842479062253094, 0.0423282422610123, 0.0423756549057051,
    0.0784335999999992, 0.878468636469772, 0.0784336,
    0.0792237451477643, 0.0791661274605434, 0.879142973793104);
const mat3 AgxOutsetMatrix = mat3(
    1.19687900512017, -0.0528968517574562, -0.0529716355144438,
    -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
    -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
const float AgxMinEv = -12.47393;
const float AgxMaxEv = 4.026069;

/// <summary>AgX 对比曲线的六次多项式拟合。</summary>
vec3 AgxContrast(vec3 value)
{
    vec3 value2 = value * value;
    vec3 value4 = value2 * value2;
    return 15.5 * value4 * value2
        - 40.14 * value4 * value
        + 31.96 * value4
        - 6.868 * value2 * value
        + 0.4298 * value2
        + 0.1191 * value
        - 0.00232;
}

/// <summary>线性到 sRGB 编码，管线的最后一步。</summary>
vec3 LinearToSrgb(vec3 value)
{
    vec3 low = value * 12.92;
    vec3 high = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(value, vec3(0.0031308)));
}

void main()
{
    vec4 source = texture(u_SourceTexture, v_Uv);
    //Copy 模式给阴影调试调色板用，它的值是显示色，任何转换都会改变观感。
    if (u_Mode == 2)
    {
        FragColor = source;
        return;
    }

    vec3 color = max(source.rgb, vec3(0.0));
    if (u_Mode == 0)
    {
        color = AgxInsetMatrix * max(color * u_Exposure, vec3(0.0));
        color = clamp(log2(max(color, vec3(1e-10))), vec3(AgxMinEv), vec3(AgxMaxEv));
        color = (color - vec3(AgxMinEv)) / (AgxMaxEv - AgxMinEv);
        color = AgxContrast(color);
        color = AgxOutsetMatrix * color;
        //AgX 输出的是 2.2 显示编码，先还原成线性再交给 sRGB 编码。
        color = pow(max(color, vec3(0.0)), vec3(2.2));
    }

    //alpha 是覆盖率不是颜色，不参与曝光与色调映射。
    FragColor = vec4(LinearToSrgb(min(color, vec3(1.0))), source.a);
})";
}

//绑定后端并释放可能属于其它后端的旧资源
void OutputPass::Initialize(RenderBackend* renderBackend)
{
    if (backend == renderBackend) return;

    Shutdown();
    backend = renderBackend;
    quad.Initialize(renderBackend);
}

void OutputPass::Shutdown()
{
    if (backend && program.IsValid()) backend->DeleteShaderProgram(program);
    program = GpuShaderProgramID();
    //切到无后端状态，由 FullscreenQuad 用自己的引用释放顶点与索引缓冲
    quad.Initialize(nullptr);
    warned = false;
    backend = nullptr;
}

//按需创建 shader 与全屏四边形
bool OutputPass::EnsureResources()
{
    if (!backend) return false;

    if (!program.IsValid())
    {
        GpuShaderProgramDesc desc;
        desc.vertexSource = FullscreenQuadVertexSource;
        desc.fragmentSource = OutputFragmentSource;
        program = backend->CreateShaderProgram(desc);
    }

    const bool ready = program.IsValid() && quad.EnsureReady();
    if (!ready && !warned)
    {
        Log::Error("OutputPass setup failed: builtin display resources are unavailable.");
        warned = true;
    }
    return ready;
}

//按参数输出一次；失败时返回 false，由调用方决定是否跳过
bool OutputPass::Render(const Parameters& parameters)
{
    if (!parameters.sourceTexture.IsValid() || parameters.width <= 0 || parameters.height <= 0) return false;
    if (!EnsureResources()) return false;

    //显示目标的内容会被这次绘制整体覆盖，不能清屏——否则会清掉同帧其它相机的输出。
    RenderPassDesc passDesc;
    passDesc.renderTarget = parameters.destinationRenderTarget;
    passDesc.x = parameters.x;
    passDesc.y = parameters.y;
    passDesc.width = parameters.width;
    passDesc.height = parameters.height;
    passDesc.clearMode = ClearMode::None;
    backend->BeginPass(passDesc);

    backend->SetDepthTest(false);
    backend->SetDepthWrite(false);
    backend->SetBlend(false);
    backend->SetCullMode(CullMode::None);
    backend->BindShaderProgram(program);
    backend->SetUniformInt("u_SourceTexture", 0);
    backend->SetUniformFloat("u_Exposure", parameters.exposure);
    backend->SetUniformInt("u_Mode", static_cast<int32>(parameters.mode));
    backend->BindTexture(0, parameters.sourceTexture);
    backend->BindVertexInput(quad.GetVertexInput());
    backend->DrawIndexed(0, 6);
    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->EndPass();
    return true;
}
