#include "FileSystem/PathDefines.h"
#include "Physics/HeightFieldComponent.h"
#include "Physics/PhysicsReflection.h"
#include "Platform/GlfwWindow.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/Reflection.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/WorldSerializer.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/Object/Texture2D.h"
#include "Runtime/Object/TransformComponent.h"
#include "FlightController.h"
#define GLAD_API_CALL_EXPORT
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdio>
#include <vector>
extern "C" void OrbedenGameNative_RegisterReflection();

/// <summary>用真实 OpenGL 管线绘制模板并保存 RGB 图像供人工检查。</summary>
int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    Reflection::RegisterGeneratedReflection();
    PhysicsReflection::Register();
    OrbedenGameNative_RegisterReflection();
    PathDefines::SetContentRoot(argv[1]);
    World world;
    World::SetCurrentWorld(&world);
    if (!WorldSerializer::LoadXml(world, PathDefines::GetContentFilePath("World/main.world"))) return 2;
    GlfwWindow window;
    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "Flight terrain rendering verification";
    desc.graphicsApi = WindowGraphicsApi::OpenGL;
    if (!window.Create(desc)) return 3;
    glfwHideWindow(window.GetGlfwWindow());
    RenderSystem renderer;
    if (!renderer.Initialize(&window)) return 4;
    renderer.SetFpsLabelVisible(false);
    //抬高验证相机，展示跑道旁的地形起伏及噪声颜色。
    world.ForEachComponent<Camera>([](Camera* camera)
    {
        camera->GetEns()->Transform()->SetLocalPosition({ 0, 30, 45 });
        camera->GetEns()->Transform()->SetLocalRotation({ -0.258819f, 0, 0, 0.965926f });
    });
    for (int i = 0; i < 3; ++i) renderer.Render(world, 1.0f / 60);
    bool noiseBound = false;
    world.ForEachComponent<HeightFieldComponent>([&](HeightFieldComponent* terrain)
    {
        Mesh* mesh = terrain->GetEns()->GetComponent<StaticMeshRenderer>()->mesh.Get();
        Material* material = mesh && !mesh->subMeshes.empty() ? mesh->subMeshes[0].material.Get() : nullptr;
        Texture2D* noise = material ? material->GetTexture("u_DiffuseTexture") : nullptr;
        noiseBound = noise && noise->width == 256 && noise->height == 256;
        if (noiseBound) std::printf("noise: %dx%d RGBA, bound to terrain diffuse material\n", noise->width, noise->height);
    });
    const int width = window.GetFramebufferWidth();
    const int height = window.GetFramebufferHeight();
    std::vector<unsigned char> pixels(width * height * 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    GLenum error = glGetError();
    FILE* file = nullptr;
    fopen_s(&file, argv[2], "wb");
    if (!file) return 5;
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int y = height - 1; y >= 0; --y) std::fwrite(pixels.data() + y * width * 3, 1, width * 3, file);
    std::fclose(file);
    renderer.OnShutdown();
    window.Destroy();
    world.Clear();
    World::SetCurrentWorld(nullptr);
    ResourceManager::Shutdown();
    std::printf("OpenGL error: %u; screenshot: %s\n", error, argv[2]);
    return noiseBound && error == GL_NO_ERROR ? 0 : 6;
}
