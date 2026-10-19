#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

#include "Editor/EditorSystem.h"
#include "Platform/GlfwWindow.h"
#include "Platform/WindowsTextInput.h"
#include "Rendering/RenderSystem.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/Native/InteropText.h"
#include "Runtime/Object/Ens.h"
#include "Runtime/Object/StaticMeshRenderer.h"

#include <filesystem>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    //记录 GPU 冒烟异常的原生调用栈
    LONG CALLBACK PrintExceptionStack(EXCEPTION_POINTERS* exception)
    {
        if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(process, nullptr, TRUE);
        CONTEXT context = *exception->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip; frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp; frame.AddrStack.Mode = AddrModeFlat;
        std::fprintf(stderr, "Access violation at %p\n", exception->ExceptionRecord->ExceptionAddress);
        for (uint32 index = 0; index < 30; ++index)
        {
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
            auto symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 displacement = 0;
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
                std::fprintf(stderr, "  %s + %llu\n", symbol->Name, displacement);
            else std::fprintf(stderr, "  0x%llx\n", frame.AddrPC.Offset);
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context,
                nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
        }
        std::fflush(stderr);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    //检查切换后的编辑器状态
    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    //绘制完整的编辑器帧
    void DrawFrames(Application& app, EditorSystem& editor, uint32 count)
    {
        for (uint32 index = 0; index < count; ++index)
        {
            app.GetWindow()->PollEvents();
            app.Tick(1.0f / 60.0f);
            editor.Update(app.GetWorld(), 1.0f / 60.0f);
            app.Render(1.0f / 60.0f);
            editor.RenderEditorGUI();
            app.Present();
        }
    }
}

//验证项目设置驱动的双向后端切换
int32 main(int32 argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    AddVectoredExceptionHandler(1, PrintExceptionStack);
    try
    {
        Require(argc == 3, "Expected Editor executable and temporary project directory.");
        std::filesystem::path projectRoot = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(projectRoot / "Content");
        std::filesystem::copy(std::filesystem::current_path() / "OrbedenEditor/Templates/Builtin", projectRoot / "Content/Builtin",
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
        std::filesystem::path projectFile = projectRoot / "BackendSmoke.oeproj";
        std::ofstream(projectRoot / "Content" / "main.world") << "<World>\n</World>\n";
        std::ofstream(projectFile) << "<OrbedenProject version=\"" << OrbedenProjectVersion << "\" startupWorld=\"main.world\">\n"
            "<EditorGuiConfig graphicsBackend=\"OpenGL\" font=\"builtin:vector\" fontSize=\"16\" />\n"
            "<EditorLayout><Panel id=\"editor_settings\" visible=\"true\" floating=\"true\" x=\"40\" y=\"40\" width=\"600\" height=\"450\" /></EditorLayout>\n"
            "</OrbedenProject>\n";

        auto window = std::make_unique<GlfwWindow>();
        WindowDesc desc;
        desc.graphicsApi = WindowGraphicsApi::OpenGL;
        desc.visible = false;
        Require(window->Create(desc), "Initial Editor window creation failed.");
        Application app(ScriptRuntimeMode::CLR);
        app.SetWindow(window.get());
        app.SetSimulationEnabled(false);
        Require(app.Initialize(), "Application initialization failed.");
        {
            EditorSystem editor(app, argv[1]);
            editor.RequestOpenProjectFile(InteropText::PathToUtf8(projectFile));
            DrawFrames(app, editor, 4);
            Require(editor.HasProject() && editor.GetProject().IsWorldLoaded(), "Smoke project did not load.");
            Ens* sentinel = app.GetWorld().CreateEns("Unsaved backend switch sentinel");
            auto renderer = sentinel->AddComponent<StaticMeshRenderer>();
            Mesh* mesh = Object::CreateInstance<Mesh>();
            mesh->vertices = { {-1.0f,-1.0f,0.0f}, {1.0f,-1.0f,0.0f}, {0.0f,1.0f,0.0f} };
            mesh->normals = { {0.0f,0.0f,1.0f}, {0.0f,0.0f,1.0f}, {0.0f,0.0f,1.0f} };
            mesh->texcoords = { {0.0f,0.0f}, {1.0f,0.0f}, {0.5f,1.0f} };
            mesh->indices = { 0, 1, 2 };
            mesh->subMeshes = { { "Smoke", 0, 3 } };
            mesh->MarkDirty();
            renderer->mesh = Ref<Mesh>(mesh);
            renderer->materials.push_back(Ref<Material>(ResourceManager::Load<Material>("Builtin/Materials/default.orbmat")));
            Require(renderer->mesh.Get() && renderer->materials[0].Get(), "Scene mesh and material did not load.");
            EnsId sentinelId = sentinel->GetId();
            editor.GetEditorScene().SelectEns(sentinelId);
            editor.GetEditorScene().SetRenderMode(EditorSceneRenderMode::ShadedWireframe);
            app.GetWorld().SetDirty();
            RenderSystem* renderSystem = app.GetSystem<RenderSystem>();
            RenderTargetID retainedTarget = renderSystem->CreateRenderTarget(64, 64);
            Require(retainedTarget.IsValid(), "Logical render target creation failed.");

            //重复双向切换并检查场景、目标和浮动面板
            for (WindowGraphicsApi api : { WindowGraphicsApi::Vulkan, WindowGraphicsApi::OpenGL,
                WindowGraphicsApi::Vulkan, WindowGraphicsApi::OpenGL })
            {
                EditorGuiConfigState config = editor.GetProject().GetEditorGuiConfig();
                config.graphicsApi = api;
                std::string error;
                Require(EditorProject::WriteEditorGuiConfig(InteropText::PathToUtf8(projectFile), config, error), error.c_str());
                editor.RequestEditorGuiConfig();
                DrawFrames(app, editor, 1);
                Require(editor.ApplyPendingGraphicsBackend(window), "Editor GUI restoration failed.");
                Require(window->GetGraphicsApi() == api, "Requested backend did not become active.");
                Require(WindowsTextInputDetail::Instance().IsAttached(), "Text input hook was not rebound.");
                Require(app.GetWorld().GetEns(sentinelId) == sentinel, "Unsaved World objects changed.");
                Require(app.GetWorld().IsDirty(), "World dirty state was lost.");
                Require(editor.GetEditorScene().GetSelectedEns() == sentinelId, "Selection was lost.");
                Require(renderSystem->GetRenderTargetTexture(retainedTarget).IsValid(), "Logical render target was lost.");
                DrawFrames(app, editor, 5);
                const RenderBatchStats& stats = renderSystem->GetBatchStats();
                Require(stats.ordinaryDraws + stats.instancedDraws + stats.dynamicBatchDraws > 0, "Scene stopped drawing after backend switch.");
                Require(stats.failedUploads == 0 && stats.invalidResources == 0, "Scene GPU resources were not rebuilt.");
                std::cout << editor.GetGraphicsBackendStatus() << '\n';
            }

            //拒绝无图形 API 的窗口并继续绘制原后端
            GlfwWindow unsupported;
            desc.graphicsApi = WindowGraphicsApi::None;
            Require(unsupported.Create(desc), "Unsupported smoke window creation failed.");
            Require(!app.SetWindow(&unsupported), "Unsupported window was accepted.");
            Require(app.GetWindow() == window.get(), "Failed switch replaced the current window.");
            Require(renderSystem->GetRenderTargetTexture(retainedTarget).IsValid(), "Failed switch released logical targets.");
            DrawFrames(app, editor, 3);
            renderSystem->DeleteRenderTarget(retainedTarget);
        }
        app.Quit();
        std::ifstream saved(projectFile);
        std::string content((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        Require(content.find("graphicsBackend=\"OpenGL\"") != std::string::npos, "Saved backend preference was lost.");
        Require(content.find("id=\"editor_settings\" visible=\"true\" floating=\"true\"") != std::string::npos,
            "Floating settings panel was not retained.");
        std::cout << "PASS: Editor backend switching, unsaved World, selection, targets, floating settings and persistence\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
