#include "Platform/GlfwWindow.h"
#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/ImGuiRenderer.h"
#include "Rendering/OutputPass.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Gui/UIShaderSources.h"
#include "ResourceManager/ResourceManager.h"
#include "FileSystem/PathDefines.h"
#include "Profiler/Profiler.h"
#include "Runtime/Native/InteropText.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/Object/Transform.h"

#include <imgui.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <chrono>

namespace
{
    //测量相同材质连续绘制的 CPU 提交耗时
    void RunSubmissionBenchmark(RenderBackend& backend)
    {
        const char* vertex = "#version 430 core\nlayout(location=0)in vec3 a_Position;uniform mat4 u_Model;void main(){gl_Position=u_Model*vec4(a_Position,1.0);}";
        const char* fragment = R"(#version 430 core
uniform vec4 u_Colors[16];
uniform mat4 u_ShadowMatrices[6];
uniform vec4 u_ShadowParams[6];
uniform sampler2D u_Texture;
out vec4 FragColor;
void main(){vec4 c=vec4(0.0);for(int i=0;i<16;++i)c+=u_Colors[i];for(int i=0;i<6;++i)c+=u_ShadowMatrices[i]*u_ShadowParams[i];FragColor=c*texture(u_Texture,vec2(0.5))*0.01;}
)";
        auto program = backend.CreateShaderProgram({ vertex, fragment });
        if (!program.IsValid()) throw std::runtime_error("Submission benchmark shader failed.");
        vector3 triangle[] = { {-1.0f,-1.0f,0.0f}, {3.0f,-1.0f,0.0f}, {-1.0f,3.0f,0.0f} };
        uint32 sequence[] = { 0, 1, 2 };
        auto vertices = backend.CreateVertexBuffer({ triangle, sizeof(triangle) });
        auto indices = backend.CreateIndexBuffer({ sequence, sizeof(sequence) });
        auto input = backend.CreateVertexInput({ vertices, indices, sizeof(vector3), GpuVertexLayout::Position });
        GpuRenderTargetDesc targetDesc; targetDesc.width = targetDesc.height = 16;
        auto target = backend.CreateRenderTarget(targetDesc);
        uint8 pixel[] = { 255, 255, 255, 255 };
        auto texture = backend.CreateTexture({ 1, 1, 4, pixel });
        backend.BindShaderProgram(program);
        List<std::string> colorNames;
        for (uint32 index = 0; index < 16; ++index) colorNames.push_back("u_Colors[" + std::to_string(index) + "]");
        for (uint32 index = 0; index < 6; ++index)
        {
            backend.GetParameters().SetMatrix4(("u_ShadowMatrices[" + std::to_string(index) + "]").c_str(), matrix4x4());
            backend.GetParameters().SetColor(("u_ShadowParams[" + std::to_string(index) + "]").c_str(), {0.1f,0.1f,0.1f,1.0f});
        }
        List<float64> times;
        for (uint32 frame = 0; frame < 60; ++frame)
        {
            backend.BeginFrame();
            RenderPassDesc pass; pass.renderTarget = target; pass.width = pass.height = 16;
            backend.BeginPass(pass);
            backend.GetPipelineState() = {};
            backend.GetPipelineState().depthTest = false;
            auto start = std::chrono::steady_clock::now();
            for (uint32 draw = 0; draw < 240; ++draw)
            {
                backend.BindShaderProgram(program);
                for (const std::string& name : colorNames)
                    backend.GetParameters().SetColor(name.c_str(), {0.2f,0.3f,0.4f,1.0f});
                matrix4x4 model; model.m[12] = static_cast<float32>(draw % 20) * 0.001f;
                backend.GetParameters().SetMatrix4("u_Model", model);
                backend.GetParameters().SetInt("u_Texture", 0);
                backend.BindTexture(0, texture);
                backend.BindVertexInput(input);
                backend.DrawIndexed(0, 3);
            }
            float64 elapsed = std::chrono::duration<float64, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (frame >= 10) times.push_back(elapsed);
            backend.EndPass(); backend.EndFrame(); backend.Present(); backend.WaitForIdle();
        }
        std::sort(times.begin(), times.end());
        std::cout << "240-draw CPU submission milliseconds: median=" << times[times.size()/2]
            << " p90=" << times[times.size()*9/10] << " samples=" << times.size() << '\n';
        backend.DeleteRenderTarget(target); backend.DeleteTexture(texture);
        backend.DeleteVertexInput(input); backend.DeleteVertexBuffer(vertices); backend.DeleteIndexBuffer(indices); backend.DeleteShaderProgram(program);
    }

    constexpr const char* Vertex = R"(#version 430 core
layout(location=0) in vec3 a_Position;
uniform mat4 u_Model;
out vec2 v_Uv;
void main(){v_Uv=vec2(0.5);gl_Position=u_Model*vec4(a_Position,1.0);}
)";
    constexpr const char* Fragment = R"(#version 430 core
in vec2 v_Uv;
uniform vec4 u_Color;
uniform sampler2D u_Texture;
out vec4 FragColor;
void main(){vec4 sampleColor=texture(u_Texture,v_Uv);if(sampleColor.r*u_Color.a<0.5)discard;FragColor=sampleColor*u_Color;}
)";

    //检查基础渲染结果
    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    //检查深度像素数值
    void CheckDepth(RenderBackend& backend, GpuRenderTargetID target, int32 x, int32 y, float32 expected)
    {
        float32 depth = -1.0f;
        Require(backend.ReadDepthPixel(target, x, y, depth), "Depth readback failed.");
        if (std::abs(depth - expected) > 0.002f)
            throw std::runtime_error("Depth mismatch at " + std::to_string(x) + "," + std::to_string(y) + ": expected=" + std::to_string(expected) + " actual=" + std::to_string(depth));
    }

    //测量真实管线的同材质场景提交耗时
    void RunSceneBenchmark(GlfwWindow& window, bool capture)
    {
        PathDefines::SetContentRoot(InteropText::PathToUtf8(std::filesystem::current_path() / "OrbedenEditor/Templates"));
        Application app(ScriptRuntimeMode::CLR);
        app.SetWindow(&window); app.SetSimulationEnabled(false);
        Require(app.Initialize(), "Scene benchmark initialization failed.");
        auto camera = app.GetWorld().CreateEns("Benchmark camera");
        camera->AddComponent<Camera>();
        camera->GetComponent<Transform>()->SetLocalPosition({0.0f,0.0f,5.0f});
        auto mesh = Object::CreateInstance<Mesh>();
        mesh->vertices = { {-1.0f,-1.0f,0.0f}, {1.0f,-1.0f,0.0f}, {0.0f,1.0f,0.0f} };
        mesh->normals = { {0.0f,0.0f,1.0f}, {0.0f,0.0f,1.0f}, {0.0f,0.0f,1.0f} };
        mesh->texcoords = { {0.0f,0.0f}, {1.0f,0.0f}, {0.5f,1.0f} };
        mesh->indices = { 0, 1, 2 }; mesh->subMeshes = { { "Benchmark", 0, 3 } }; mesh->MarkDirty();
        Material* material = ResourceManager::Load<Material>("Builtin/Materials/default.orbmat");
        Require(material != nullptr, "Scene benchmark material failed.");
        for (uint32 index = 0; index < 240; ++index)
        {
            Ens* ens = app.GetWorld().CreateEns("Benchmark mesh");
            ens->GetComponent<Transform>()->SetLocalPosition({static_cast<float32>(index) * 0.0001f,0.0f,0.0f});
            auto renderer = ens->AddComponent<StaticMeshRenderer>();
            renderer->mesh = Ref<Mesh>(mesh); renderer->materials.push_back(Ref<Material>(material));
            renderer->drawStrategy = DrawStrategy::Individual;
        }
        RenderSystem* renderer = app.GetSystem<RenderSystem>();
        List<float64> times;
        Profiler::SetCapturing(capture);
        for (uint32 frame = 0; frame < 60; ++frame)
        {
            window.PollEvents(); app.Tick(1.0f/60.0f);
            auto start = std::chrono::steady_clock::now();
            app.Render(1.0f/60.0f);
            float64 elapsed = std::chrono::duration<float64,std::milli>(std::chrono::steady_clock::now()-start).count();
            if (frame >= 10) times.push_back(elapsed);
            Require(renderer->GetBatchStats().ordinaryDraws == 240, "Scene benchmark draw count changed.");
            app.Present(); renderer->GetBackend().WaitForIdle();
            Profiler::EndFrame();
        }
        Profiler::SetCapturing(false);
        if (capture)
        {
            List<ProfileFrameSummary> frames(60);
            int32 count = Profiler::CopyFrameSummaries(frames.data(), static_cast<int32>(frames.size()));
            Require(count == 60, "Capture benchmark frame count changed.");
            for (int32 index = 0; index < count; ++index)
                Require(frames[index].droppedEventCount == 0, "Capture benchmark dropped profile events.");
        }
        std::sort(times.begin(),times.end());
        std::cout << "240-object ForwardPipeline capture=" << (capture ? "on" : "off") << " milliseconds: median=" << times[times.size()/2]
            << " p90=" << times[times.size()*9/10] << " samples=" << times.size() << '\n';
        app.Quit();
    }

    //编译真实内置资源及全部几何变体
    void CompileBuiltins(RenderBackend& backend, const std::filesystem::path& content)
    {
        PathDefines::SetContentRoot(InteropText::PathToUtf8(content));
        GpuResourceManager resources;
        resources.Initialize(&backend);
        uint32 count = 0;
        for (const auto& file : std::filesystem::recursive_directory_iterator(content))
        {
            if (file.path().extension() != ".orbshader") continue;
            std::string key = InteropText::PathToUtf8(file.path().lexically_relative(content));
            Shader* shader = ResourceManager::Load<Shader>(key);
            Require(shader != nullptr, ("Shader import failed: " + key).c_str());
            const GpuShader* gpu = resources.GetShader(shader);
            Require(gpu && gpu->IsValid(), ("Shader variants failed: " + key).c_str());
            ++count;
        }
        resources.Shutdown();
        ResourceManager::Shutdown();
        std::cout << "Builtin shaders and variants: " << count << " passed\n";
    }

    //执行同一套跨后端绘制与呈现
    void RunSmoke(RenderBackend& backend, GlfwWindow& window)
    {
        auto surfaceProgram = backend.CreateShaderProgram({ UIShaderSources::SurfaceVertexSource, UIShaderSources::SurfaceFragmentSource });
        auto coverageProgram = backend.CreateShaderProgram({ UIShaderSources::CoverageVertexSource, UIShaderSources::CoverageFragmentSource });
        Require(surfaceProgram.IsValid() && coverageProgram.IsValid(), "Runtime UI fallback shaders failed.");
        backend.DeleteShaderProgram(surfaceProgram); backend.DeleteShaderProgram(coverageProgram);
        GpuShaderProgramID program = backend.CreateShaderProgram({ Vertex, Fragment });
        Require(program.IsValid(), "Basic shader compilation failed.");
        vector3 positions[] = { {-0.35f,-0.6f,0.0f}, {0.35f,-0.6f,0.0f}, {0.0f,0.6f,0.0f} };
        uint32 sequence[] = { 0,1,2 };
        auto vertices = backend.CreateVertexBuffer({ positions, sizeof(positions), GpuBufferUsage::Stream });
        auto indices = backend.CreateIndexBuffer({ sequence, sizeof(sequence), GpuBufferUsage::Stream });
        auto input = backend.CreateVertexInput({ vertices, indices, sizeof(vector3), GpuVertexLayout::Position });
        uint8 white[] = { 255,255,255,255 }, black[] = { 0,0,0,255 };
        auto whiteTexture = backend.CreateTexture({ 1,1,4,white });
        auto blackTexture = backend.CreateTexture({ 1,1,4,black });
        auto depth = backend.CreateDepthTexture({ 128,128 });
        GpuRenderTargetDesc targetDesc; targetDesc.width = targetDesc.height = 128; targetDesc.depthTexture = depth; targetDesc.format = GpuRenderTargetFormat::RGBA16F;
        auto target = backend.CreateRenderTarget(targetDesc);
        auto copyDepth = backend.CreateDepthTexture({ 128,128 }); targetDesc.depthTexture = copyDepth;
        auto copyTarget = backend.CreateRenderTarget(targetDesc);
        Require(input.IsValid() && target.IsValid() && copyTarget.IsValid(), "Basic resource allocation failed.");
        GpuDepthDistributionID distribution = backend.CreateDepthDistribution();
        Require(distribution.IsValid(), "Depth compute resources failed.");

        for (uint32 frame = 0; frame < 5; ++frame)
        {
            backend.BeginFrame();
            RenderPassDesc pass; pass.width = pass.height = 128; pass.renderTarget = target;
            backend.BeginPass(pass);
            if (frame == 0)
            {
                GpuRenderTargetDesc temporaryDesc;
                temporaryDesc.width = temporaryDesc.height = 8; temporaryDesc.colorOnly = true;
                auto temporary = backend.CreateRenderTarget(temporaryDesc);
                Require(temporary.IsValid(), "Resource creation inside a pass failed.");
                backend.DeleteRenderTarget(temporary);
            }
            backend.GetPipelineState() = {};
            backend.GetPipelineState().cull = CullMode::Back;
            backend.BindShaderProgram(program); backend.BindVertexInput(input);
            backend.GetParameters().SetInt("u_Texture", 0);
            backend.GetParameters().SetColor("u_Color", {1.0f,0.2f,0.1f,1.0f});
            backend.BindTexture(0, whiteTexture);
            backend.GetParameters().SetMatrix4("u_Model", RenderMath::Translation({-0.45f,0.0f,-0.5f}));
            backend.DrawIndexed(0, 3);
            if (frame % 2 == 0)
            {
                for (vector3& position : positions) position.z = 0.2f;
                Require(backend.UploadVertexBuffer(vertices, positions, sizeof(positions), sizeof(positions)), "Stream update failed.");
            }
            backend.GetParameters().SetMatrix4("u_Model", RenderMath::Translation({0.45f,0.0f,0.5f}));
            backend.DrawIndexed(0, 3);
            if (frame % 2 == 0)
            {
                for (vector3& position : positions) position.z = 0.0f;
                Require(backend.UploadVertexBuffer(vertices, positions, sizeof(positions), sizeof(positions)), "Stream restore failed.");
            }
            backend.BindTexture(0, blackTexture);
            backend.GetParameters().SetMatrix4("u_Model", RenderMath::Translation({0.0f,0.0f,-0.9f}));
            backend.DrawIndexed(0, 3);
            backend.EndPass();
            CheckDepth(backend, target, 35, 60, 0.25f);
            CheckDepth(backend, target, 93, 60, frame % 2 == 0 ? 0.85f : 0.75f);
            CheckDepth(backend, target, 64, 60, 1.0f);
            GpuRenderTargetCopyDesc copy; copy.sourceRenderTarget = target; copy.destinationRenderTarget = copyTarget; copy.width = copy.height = 128;
            Require(backend.CopyRenderTarget(copy), "Color/depth snapshot copy failed.");
            CheckDepth(backend, copyTarget, 35, 60, 0.25f);
            GpuDepthDistribution previous;
            backend.TryReadDepthDistribution(distribution, previous);
            backend.SubmitDepthDistribution(distribution, copyDepth, matrix4x4(), 0.1f, 10.0f);
            OutputPass output; output.Initialize(&backend);
            OutputPass::Parameters parameters;
            parameters.sourceTexture = backend.GetRenderTargetColorTexture(target);
            parameters.width = window.GetFramebufferWidth(); parameters.height = window.GetFramebufferHeight();
            Require(output.Render(parameters), "Display output pass failed.");
            output.Shutdown();
            backend.EndFrame(); backend.Present();
            window.PollEvents();
        }
        backend.WaitForIdle();
        GpuDepthDistribution statistics;
        Require(backend.TryReadDepthDistribution(distribution, statistics), "Asynchronous compute did not complete.");
        uint64 samples = 0; for (uint32 count : statistics.bins) samples += count;
        Require(samples > 0, "Depth compute returned an empty histogram.");
        std::cout << "Draw parameters, culling, textures, HDR/depth copies, compute and presentation passed\n";

        //检查采样槽、图像写入与等待后的绑定恢复
        backend.BeginFrame();
        RenderPassDesc samplerPass; samplerPass.renderTarget = target; samplerPass.width = samplerPass.height = 128;
        backend.BeginPass(samplerPass); backend.GetPipelineState() = {};
        backend.BindShaderProgram(program); backend.BindVertexInput(input);
        backend.GetParameters().SetColor("u_Color", {1.0f,1.0f,1.0f,1.0f});
        backend.BindTexture(0, whiteTexture); backend.BindTexture(1, blackTexture);
        backend.GetParameters().SetInt("u_Texture", 0);
        backend.GetParameters().SetMatrix4("u_Model", RenderMath::Translation({-0.45f,0.0f,-0.5f}));
        backend.DrawIndexed(0,3);
        backend.GetParameters().SetInt("u_Texture", 1);
        backend.GetParameters().SetMatrix4("u_Model", RenderMath::Translation({0.45f,0.0f,0.5f}));
        backend.DrawIndexed(0,3); backend.EndPass();
        CheckDepth(backend,target,35,60,0.25f); CheckDepth(backend,target,93,60,1.0f);
        backend.WaitForIdle(); backend.WaitForIdle();
        samplerPass.clearMode = ClearMode::None;
        backend.BeginPass(samplerPass);
        backend.GetParameters().SetInt("u_Texture", 0);
        backend.DrawIndexed(0,3); backend.EndPass();
        CheckDepth(backend,target,93,60,0.75f);
        backend.BeginPass(samplerPass);
        Require(backend.UploadTextureRegion(whiteTexture,0,0,1,1,4,black,4), "Sampled texture update failed.");
        backend.GetParameters().SetMatrix4("u_Model", RenderMath::Translation({0.0f,0.0f,-0.9f}));
        backend.DrawIndexed(0,3); backend.EndPass();
        CheckDepth(backend,target,64,60,1.0f);
        backend.BeginPass(samplerPass);
        Require(backend.UploadTextureRegion(whiteTexture,0,0,1,1,4,white,4), "Sampled texture restore failed.");
        backend.DrawIndexed(0,3); backend.EndPass();
        CheckDepth(backend,target,64,60,0.05f);
        backend.EndFrame(); backend.Present();
        std::cout << "Sampler changes, texture writes and binding recovery after repeated waits passed\n";

        //检查离屏与窗口的片元坐标约定
        const char* coordinateFragment = "#version 430 core\nuniform float u_Threshold;out vec4 FragColor;void main(){if(gl_FragCoord.y<u_Threshold)discard;FragColor=vec4(1.0);}";
        auto coordinateProgram = backend.CreateShaderProgram({ Vertex, coordinateFragment });
        Require(coordinateProgram.IsValid(), "Fragment coordinate shader failed.");
        for (bool screen : { false, true })
        {
            RenderPassDesc pass; pass.renderTarget = screen ? GpuRenderTargetID{} : target;
            pass.width = screen ? window.GetFramebufferWidth() : 128;
            pass.height = screen ? window.GetFramebufferHeight() : 128;
            backend.BeginPass(pass); backend.GetPipelineState() = {};
            backend.BindShaderProgram(coordinateProgram); backend.BindVertexInput(input);
            backend.GetParameters().SetMatrix4("u_Model", matrix4x4());
            backend.GetParameters().SetFloat("u_Threshold", static_cast<float32>(pass.height) * 0.5f);
            backend.DrawIndexed(0,3); backend.EndPass();
            CheckDepth(backend, pass.renderTarget, pass.width / 2, pass.height * 3 / 5, 0.5f);
            CheckDepth(backend, pass.renderTarget, pass.width / 2, pass.height * 2 / 5, 1.0f);
        }
        backend.EndFrame(); backend.Present(); backend.DeleteShaderProgram(coordinateProgram);
        std::cout << "Canonical fragment coordinates on offscreen and presentation targets passed\n";

        //检查数组首元素别名与参数快照
        const char* arrayFragment = "#version 430 core\nuniform vec4 u_Colors[2];out vec4 FragColor;void main(){if(u_Colors[0].a<0.5)discard;FragColor=u_Colors[0]+u_Colors[1];}";
        auto arrayProgram = backend.CreateShaderProgram({ Vertex, arrayFragment });
        Require(arrayProgram.IsValid(), "Array parameter shader failed.");
        for (uint32 frame = 0; frame < 4; ++frame)
        {
            backend.BeginFrame();
            RenderPassDesc pass; pass.renderTarget = target; pass.width = pass.height = 128;
            backend.BeginPass(pass); backend.GetPipelineState() = {};
            backend.BindShaderProgram(arrayProgram); backend.BindVertexInput(input);
            backend.GetParameters().SetColor("u_Colors", {1.0f,1.0f,1.0f,1.0f});
            backend.GetParameters().SetColor("u_Colors[1]", {0.0f,0.0f,0.0f,0.0f});
            matrix4x4 model; model.m[12] = -0.45f; model.m[14] = -0.5f;
            backend.GetParameters().SetMatrix4("u_Model", model); backend.DrawIndexed(0,3);
            backend.GetParameters().SetColor("u_Colors[0]", {1.0f,1.0f,1.0f,0.0f});
            model.m[12] = 0.45f; model.m[14] = 0.5f;
            backend.GetParameters().SetMatrix4("u_Model", model); backend.DrawIndexed(0,3);
            backend.EndPass();
            CheckDepth(backend,target,35,60,0.25f); CheckDepth(backend,target,93,60,1.0f);
            pass.clearMode = ClearMode::None; backend.BeginPass(pass);
            backend.GetParameters().SetColor("u_Colors", {1.0f,1.0f,1.0f,1.0f});
            backend.DrawIndexed(0,3); backend.EndPass();
            CheckDepth(backend,target,93,60,0.75f);
            backend.EndFrame(); backend.Present();
        }
        backend.DeleteShaderProgram(arrayProgram);
        std::cout << "Array aliases and immutable parameter snapshots across frame reuse passed\n";

        //检查实例输入与跨帧保留的流式数据
        const char* instanceVertex = "#version 430 core\nlayout(location=0) in vec3 a_Position;layout(location=5) in mat4 a_Model;out vec2 v_Uv;void main(){v_Uv=vec2(0.5);gl_Position=a_Model*vec4(a_Position,1.0);}";
        auto instanceProgram = backend.CreateShaderProgram({ instanceVertex, Fragment });
        auto instanceInput = backend.CreateVertexInput({ vertices, indices, sizeof(vector3), GpuVertexLayout::InstancedMesh });
        float32 instances[2][36] = {};
        for (uint32 index = 0; index < 2; ++index)
        {
            instances[index][0] = instances[index][5] = instances[index][10] = instances[index][15] = 1.0f;
            instances[index][12] = index ? 0.45f : -0.45f;
            instances[index][14] = index ? 0.5f : -0.5f;
        }
        auto instanceBuffer = backend.CreateVertexBuffer({ instances, sizeof(instances), GpuBufferUsage::Stream });
        Require(instanceProgram.IsValid() && instanceInput.IsValid() && instanceBuffer.IsValid(), "Instancing resources failed.");
        for (uint32 frame = 0; frame < 5; ++frame)
        {
            backend.BeginFrame();
            RenderPassDesc pass; pass.renderTarget = target; pass.width = pass.height = 128;
            backend.BeginPass(pass); backend.GetPipelineState() = {};
            backend.BindShaderProgram(instanceProgram); backend.BindVertexInput(instanceInput);
            backend.GetParameters().SetInt("u_Texture",0); backend.GetParameters().SetColor("u_Color",{1.0f,1.0f,1.0f,1.0f}); backend.BindTexture(0,whiteTexture);
            Require(backend.BindInstanceBuffer(instanceBuffer,0), "Instance buffer binding failed.");
            backend.DrawIndexedInstanced(0,3,2); backend.EndPass();
            CheckDepth(backend,target,35,60,0.25f); CheckDepth(backend,target,93,60,0.75f);
            backend.EndFrame(); backend.Present();
        }
        backend.DeleteVertexInput(instanceInput); backend.DeleteVertexBuffer(instanceBuffer); backend.DeleteShaderProgram(instanceProgram);
        std::cout << "Instanced geometry and stream buffer retention across frame reuse passed\n";

        //绘制共享设备的附加窗口与动态字体
        GlfwWindow additional;
        WindowDesc additionalDesc; additionalDesc.width = 256; additionalDesc.height = 128; additionalDesc.visible = false;
        additionalDesc.graphicsApi = window.GetGraphicsApi(); additionalDesc.sharedResourcesWindow = &window;
        Require(additional.Create(additionalDesc), "Additional window creation failed.");
        GpuSurfaceID surface = backend.CreateSurface(&additional);
        Require(surface.IsValid(), "Additional surface creation failed.");
        ImGuiContext* context = ImGui::CreateContext();
        ImGuiRenderer gui;
        Require(gui.Initialize(backend), "Shared ImGui renderer initialization failed.");
        ImGuiIO& io = ImGui::GetIO(); io.DisplaySize = ImVec2(256,128); io.DeltaTime = 1.0f/60.0f; io.IniFilename = nullptr;
        for (uint32 frame = 0; frame < 4; ++frame)
        {
            backend.BeginFrame(); backend.SetSurface(surface);
            gui.BeginFrame(); ImGui::NewFrame(); ImGui::Begin("Render smoke"); ImGui::Text("OpenGL / Vulkan"); ImGui::Button("Shared renderer"); ImGui::End(); ImGui::Render();
            gui.Render(ImGui::GetDrawData()); backend.SetSurface({}); backend.EndFrame(); backend.Present();
        }
        gui.Shutdown(); ImGui::DestroyContext(context); backend.DeleteSurface(surface); additional.Destroy();
        std::cout << "Additional surface and ImGui font texture updates passed\n";
        backend.DeleteDepthDistribution(distribution);
        backend.DeleteRenderTarget(copyTarget); backend.DeleteDepthTexture(copyDepth);
        backend.DeleteRenderTarget(target); backend.DeleteDepthTexture(depth);
        backend.DeleteTexture(whiteTexture); backend.DeleteTexture(blackTexture);
        backend.DeleteVertexInput(input); backend.DeleteVertexBuffer(vertices); backend.DeleteIndexBuffer(indices); backend.DeleteShaderProgram(program);
    }
}

//运行指定后端的基础渲染冒烟
int32 main(int32 argc, char** argv)
{
    try
    {
        WindowDesc desc; desc.visible = false; desc.width = 320; desc.height = 240; desc.graphicsApi = WindowGraphicsApi::OpenGL;
        List<std::string_view> arguments; for (int32 index = 1; index < argc; ++index) arguments.push_back(argv[index]);
        std::string error; Require(RenderBackend::ReadGraphicsApi(arguments, desc.graphicsApi, error), error.c_str());
        GlfwWindow window; Require(window.Create(desc), "Smoke window creation failed.");
        if (std::find(arguments.begin(), arguments.end(), "--scene-benchmark") != arguments.end())
        {
            RunSceneBenchmark(window, std::find(arguments.begin(), arguments.end(), "--profile-capture") != arguments.end());
            return 0;
        }
        auto backend = RenderBackend::Create(desc.graphicsApi); Require(backend && backend->Initialize(&window), "Backend initialization failed.");
        if (std::find(arguments.begin(), arguments.end(), "--submission-benchmark") != arguments.end())
        {
            RunSubmissionBenchmark(*backend);
            backend->Shutdown();
            return 0;
        }
        RunSmoke(*backend, window);
        if (argc > 1)
        {
            std::filesystem::path content = std::filesystem::current_path() / "OrbedenEditor" / "Templates";
            Require(std::filesystem::is_directory(content), "Run smoke from the repository root.");
            CompileBuiltins(*backend, content);
        }
        backend->Shutdown();
        std::cout << "PASS\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
