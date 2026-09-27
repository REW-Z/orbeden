#include "FileSystem/PathDefines.h"
#include "Platform/GlfwWindow.h"
#include "Rendering/Backend/OpenGLRenderBackend.h"
#include "Rendering/ColorSpace.h"
#include "Rendering/FullscreenQuad.h"
#include "Rendering/GpuResourceManager.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/AssetPipeline.h"
#include "Runtime/CookedAssetSerializer.h"
#include "Runtime/Reflection.h"
#include "Runtime/WorldSerializer.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <gl/GL.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

//检查实际资源与 GPU 执行结果
void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

//验证环境资源导入、持久化、Shader 编译与反射采样
int main() try
{
    const std::string content = std::filesystem::absolute("OrbedenEditor/Templates").generic_string();
    const std::string skyKey = "Builtin/Skyboxes/soft_daylight.orbsky";
    const std::string output = "Log/EnvironmentReflection";
    std::filesystem::create_directories(output);
    PathDefines::SetContentRoot(content);
    Reflection::RegisterGeneratedReflection();
    AssetCollection imported = AssetPipeline::ImportSource(skyKey);
    Require(imported.Succeeded() && imported.mainKeys.size() == 1, "Skybox import failed");
    auto* skybox = ResourceManager::Load<Skybox>(skyKey);
    Require(skybox && skybox->right.Get() && skybox->right.Get()->width == 128, "Missing builtin skybox faces");
    Require(imported.sourceFiles.size() == 7, "Skybox face dependencies are incomplete");

    //验证独立反射资源的场景保存与重载
    {
        World world;
        World::SetCurrentWorld(&world);
        world.renderSettings.reflectionEnvironment.Set(skybox);
        world.renderSettings.reflectionIntensity = 0.375f;
        Require(WorldSerializer::SaveXml(world, output + "/reflection.world"), "World save failed");
        world.Clear();
        Require(WorldSerializer::LoadXml(world, output + "/reflection.world"), "World load failed");
        Require(world.renderSettings.reflectionEnvironment.Get() == skybox
            && world.renderSettings.reflectionIntensity == 0.375f && !world.renderSettings.skyboxEnabled, "Reflection settings lost");
    }

    //验证天空盒和六面纹理的打包重载
    List<std::string> keys;
    Ref<Texture2D>* faces[] = { &skybox->right, &skybox->left, &skybox->top, &skybox->bottom, &skybox->front, &skybox->back };
    std::string error;
    for (Ref<Texture2D>* face : faces)
    {
        const std::string key = face->GetInstanceId().GetPath();
        Require(CookedAssetSerializer::Write(output + "/" + CookedAssetSerializer::GetBlobFileName(key), face->Get(), key, {}, error), "Face cook failed");
        keys.push_back(key);
    }
    Require(CookedAssetSerializer::Write(output + "/" + CookedAssetSerializer::GetBlobFileName(skyKey), skybox, skyKey, keys, error), "Skybox cook failed");
    keys.push_back(skyKey);
    Require(CookedAssetSerializer::WriteIndex(output + "/cooked.index", keys, error), "Cooked index failed");
    ResourceManager::Shutdown();
    PathDefines::SetContentRoot(std::filesystem::absolute(output).generic_string());
    skybox = ResourceManager::Load<Skybox>(skyKey);
    Require(skybox && skybox->top.Get() && skybox->top.Get()->pixels.size() == 128 * 128 * 4, "Cooked skybox load failed");
    ResourceManager::Shutdown();
    {
        std::ofstream malformed(output + "/invalid.orbsky");
        malformed << "right \"invalid.orbsky\"\n";
    }
    Require(!AssetPipeline::ImportSource("invalid.orbsky").Succeeded(), "Recursive non-image face was accepted");
    PathDefines::SetContentRoot(content);
    skybox = ResourceManager::Load<Skybox>(skyKey);

    //创建隐藏的真实 OpenGL 上下文
    GlfwWindow window;
    WindowDesc windowDesc;
    windowDesc.width = 32;
    windowDesc.height = 32;
    windowDesc.visible = false;
    windowDesc.graphicsApi = WindowGraphicsApi::OpenGL;
    Require(window.Create(windowDesc), "Hidden window failed");
    OpenGLRenderBackend backend;
    Require(backend.Initialize(&window), "OpenGL initialization failed");
    GpuResourceManager resources;
    resources.Initialize(&backend);
    auto environment = resources.GetEnvironmentReflection(skybox, 1.0f);
    Require(environment.texture.IsValid() && environment.maxLod == 7.0f, "Reflection mip chain failed");
    Require(resources.GetSkybox(skybox).id == environment.texture.id, "Skybox GPU cache was not shared");
    Require(!resources.GetEnvironmentReflection(skybox, 0.0f).texture.IsValid(), "Zero intensity did not disable reflection");
    Require(!resources.GetEnvironmentReflection(nullptr, 1.0f).texture.IsValid(), "Missing source did not disable reflection");

    //编译全部接入环境反射的材质 Shader
    for (const char* name : { "pbs_metallic", "blinn_phong", "transparent" })
    {
        auto* shader = ResourceManager::Load<Shader>(std::string("Builtin/Shaders/") + name + ".orbshader");
        Require(shader && resources.GetShader(shader), "Material shader compilation failed");
        for (const auto& slot : shader->floatSlots)
            Require(slot.name.find("u_Environment") != 0, "Pipeline uniform leaked into material slots");
    }

    //创建白色环境并验证实际片元反射响应
    List<uint8> pixels(8 * 8 * 4, 255);
    GpuCubeTextureDesc cubeDesc;
    cubeDesc.width = cubeDesc.height = 8;
    cubeDesc.channels = 4;
    cubeDesc.srgb = true;
    cubeDesc.generateMipmaps = true;
    for (auto& face : cubeDesc.faces) face = pixels.data();
    GpuCubeTextureID white = backend.CreateCubeTexture(cubeDesc);
    std::ifstream include(content + "/Builtin/environment_reflection.orbinc");
    std::string fragment = "#version 430 core\n" + std::string(std::istreambuf_iterator<char>(include), {});
    fragment += R"(
uniform float u_TestNdotV;
uniform float u_TestRoughness;
uniform float u_TestF0;
out vec4 FragColor;
void main() {
    vec3 viewDir = vec3(sqrt(1.0 - u_TestNdotV * u_TestNdotV), u_TestNdotV, 0.0);
    FragColor = vec4(EvaluateEnvironmentReflection(vec3(0, 1, 0), viewDir, vec3(u_TestF0), u_TestRoughness, 1.0), 1.0);
})";
    GpuShaderProgramDesc programDesc;
    programDesc.vertexSource = "#version 430 core\nlayout(location=0) in vec3 a_Position;\nvoid main(){gl_Position=vec4(a_Position.xy,0,1);}";
    programDesc.fragmentSource = fragment.c_str();
    GpuShaderProgramID program = backend.CreateShaderProgram(programDesc);
    Require(program.IsValid(), "Reflection test shader failed");
    FullscreenQuad quad;
    quad.Initialize(&backend);
    Require(quad.EnsureReady(), "Fullscreen quad failed");
    GpuRenderTargetDesc targetDesc;
    targetDesc.width = targetDesc.height = 8;
    targetDesc.format = GpuRenderTargetFormat::RGBA16F;
    GpuRenderTargetID target = backend.CreateRenderTarget(targetDesc);
    RenderPassDesc pass;
    pass.width = pass.height = 8;
    pass.renderTarget = target;
    backend.BeginPass(pass);
    backend.SetDepthTest(false);
    backend.SetBlend(false);
    backend.SetCullMode(CullMode::None);
    backend.BindShaderProgram(program);
    backend.BindVertexInput(quad.GetVertexInput());
    backend.BindCubeTexture(0, white);
    backend.SetUniformInt("u_EnvironmentTexture", 0);
    backend.SetUniformFloat("u_EnvironmentMaxLod", 3.0f);
    auto sample = [&](float32 nDotV, float32 roughness, float32 intensity, float32 f0)
    {
        backend.SetUniformFloat("u_TestNdotV", nDotV);
        backend.SetUniformFloat("u_TestRoughness", roughness);
        backend.SetUniformFloat("u_TestF0", f0);
        backend.SetUniformFloat("u_EnvironmentIntensity", intensity);
        backend.DrawIndexed(0, 6);
        float32 value[4] = {};
        glReadPixels(4, 4, 1, 1, GL_RGBA, GL_FLOAT, value);
        Require(glGetError() == GL_NO_ERROR, "OpenGL reflection sampling error");
        return value[0];
    };
    float32 facing = sample(1.0f, 0.0f, 1.0f, 0.04f);
    float32 grazing = sample(0.1f, 0.0f, 1.0f, 0.04f);
    Require(std::abs(facing - 0.04f) < 0.001f && grazing > 0.6f, "Fresnel response failed");
    Require(sample(0.1f, 1.0f, 1.0f, 0.04f) < grazing, "Roughness response failed");
    Require(std::abs(sample(1.0f, 0.0f, 0.5f, 0.04f) - facing * 0.5f) < 0.001f, "Reflection intensity is not linear");
    Require(sample(0.1f, 0.0f, 0.0f, 0.04f) == 0.0f, "Reflection disable failed");
    Require(sample(1.0f, 0.0f, 1.0f, 1.0f) > 0.99f, "Metal response failed");

    //验证普通 mip 在 sRGB 环境中的线性平均
    for (usize index = 0; index < pixels.size(); index += 4)
    {
        uint8 value = ((index / 4) % 8 + (index / 4) / 8) % 2 == 0 ? 0 : 255;
        pixels[index] = pixels[index + 1] = pixels[index + 2] = value;
    }
    GpuCubeTextureID checker = backend.CreateCubeTexture(cubeDesc);
    backend.BindCubeTexture(0, checker);
    Require(std::abs(sample(1.0f, 1.0f, 1.0f, 1.0f) - 0.5f) < 0.02f, "sRGB mip averaging is not linear");
    backend.DeleteCubeTexture(checker);

    //释放测试 GPU 资源与导入对象
    backend.EndPass();
    backend.BindShaderProgram({});
    backend.BindVertexInput({});
    backend.DeleteShaderProgram(program);
    backend.DeleteRenderTarget(target);
    backend.DeleteCubeTexture(white);
    quad.Initialize(nullptr);
    resources.Shutdown();
    ResourceManager::Shutdown();
    backend.Shutdown();
    window.Destroy();
    PathDefines::Clear();
    std::cout << "PASS: skybox import/cook, invalid source rejection, world settings, shader compilation, linear mip filtering, Fresnel, roughness, strength and disable\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
