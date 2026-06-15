#include "Editor/ExampleWorldGenerator.h"

#include "Application.h"
#include "Log/Log.h"
#include "Runtime/Object/MaterialShader.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Skybox.h"
#include "Runtime/Object/Texture2D.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/World.h"

#include <filesystem>
#include <fstream>

namespace
{
    constexpr const char* CubeMeshKey = "Resources/Examples/World/Meshes/cube.obj//Mesh/Main";
    constexpr const char* GroundMeshKey = "Resources/Examples/World/Meshes/ground.obj//Mesh/Main";
    constexpr const char* ExampleShaderKey = "Resources/Examples/World/Shaders/blinn_phong_shadow";
    constexpr const char* SkyTextureKey = "Resources/Examples/World/Textures/sky_blue.png";

    std::string NormalizePath(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
    }

    void AssignShaderToMeshMaterials(Mesh* mesh, MaterialShader* shader)
    {
        if (!mesh || !shader) return;

        for (SubMesh& subMesh : mesh->subMeshes)
        {
            Material* material = subMesh.material.Get();
            if (!material) continue;

            material->shader.Set(shader);
        }
    }

    template<typename T>
    T* GetOrCreateWorldObject(World& world, const std::string& id)
    {
        Object* found = Object::FindObject(StringId(id));
        if (found)
        {
            return found->Cast<T>();
        }

        return world.CreateObject<T>(id);
    }
}

//判断项目是否使用内置示例 World 生成器
bool ExampleWorldGenerator::IsExampleProject(const std::string& projectName)
{
    return projectName == "ExampleProject";
}

//生成示例 World 文件
bool ExampleWorldGenerator::GenerateWorldFile(const std::string& projectRoot, const std::string& startupWorld)
{
    std::filesystem::path worldPath = std::filesystem::path(projectRoot) / startupWorld;
    if (worldPath.has_parent_path())
    {
        std::filesystem::create_directories(worldPath.parent_path());
    }

    std::ofstream output(worldPath, std::ios::out | std::ios::trunc);
    if (!output)
    {
        Log::Error(("Example world generate failed: " + NormalizePath(worldPath)).c_str());
        return false;
    }

    output <<
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<World version=\"1\">\n"
        "    <Ens stableId=\"world://examples/world/root\" name=\"ExampleSceneRoot\">\n"
        "        <Component type=\"SpaceComponent\">\n"
        "            <Field name=\"localPosition\" type=\"vector3\" value=\"0 0 0\" />\n"
        "            <Field name=\"localRotation\" type=\"quaternion\" value=\"0 0 0 1\" />\n"
        "            <Field name=\"localScale\" type=\"vector3\" value=\"1 1 1\" />\n"
        "        </Component>\n"
        "        <Ens stableId=\"world://examples/world/cube\" name=\"ExampleCube\">\n"
        "            <Component type=\"SpaceComponent\">\n"
        "                <Field name=\"localPosition\" type=\"vector3\" value=\"0 1 0\" />\n"
        "                <Field name=\"localRotation\" type=\"quaternion\" value=\"0 0 0 1\" />\n"
        "                <Field name=\"localScale\" type=\"vector3\" value=\"1 1 1\" />\n"
        "            </Component>\n"
        "            <Component type=\"StaticMeshRenderer\">\n"
        "                <Field name=\"enabled\" type=\"bool\" value=\"true\" />\n"
        "                <Field name=\"mesh\" type=\"Ref&lt;Mesh&gt;\" value=\"Resources/Examples/World/Meshes/cube.obj//Mesh/Main\" />\n"
        "                <Field name=\"drawLayer\" type=\"uint32\" value=\"1\" />\n"
        "                <Field name=\"drawQueue\" type=\"DrawQueue\" value=\"0\" />\n"
        "                <Field name=\"castShadows\" type=\"bool\" value=\"true\" />\n"
        "                <Field name=\"receiveShadows\" type=\"bool\" value=\"true\" />\n"
        "            </Component>\n"
        "        </Ens>\n"
        "        <Ens stableId=\"world://examples/world/ground\" name=\"ExampleGround\">\n"
        "            <Component type=\"SpaceComponent\">\n"
        "                <Field name=\"localPosition\" type=\"vector3\" value=\"0 0 0\" />\n"
        "                <Field name=\"localRotation\" type=\"quaternion\" value=\"0 0 0 1\" />\n"
        "                <Field name=\"localScale\" type=\"vector3\" value=\"1 1 1\" />\n"
        "            </Component>\n"
        "            <Component type=\"StaticMeshRenderer\">\n"
        "                <Field name=\"enabled\" type=\"bool\" value=\"true\" />\n"
        "                <Field name=\"mesh\" type=\"Ref&lt;Mesh&gt;\" value=\"Resources/Examples/World/Meshes/ground.obj//Mesh/Main\" />\n"
        "                <Field name=\"drawLayer\" type=\"uint32\" value=\"1\" />\n"
        "                <Field name=\"drawQueue\" type=\"DrawQueue\" value=\"0\" />\n"
        "                <Field name=\"castShadows\" type=\"bool\" value=\"false\" />\n"
        "                <Field name=\"receiveShadows\" type=\"bool\" value=\"true\" />\n"
        "            </Component>\n"
        "        </Ens>\n"
        "        <Ens stableId=\"world://examples/world/directional_light\" name=\"ExampleDirectionalLight\">\n"
        "            <Component type=\"SpaceComponent\">\n"
        "                <Field name=\"localPosition\" type=\"vector3\" value=\"0 4 0\" />\n"
        "                <Field name=\"localRotation\" type=\"quaternion\" value=\"0 0 0 1\" />\n"
        "                <Field name=\"localScale\" type=\"vector3\" value=\"1 1 1\" />\n"
        "            </Component>\n"
        "            <Component type=\"DirectionalLight\">\n"
        "                <Field name=\"enabled\" type=\"bool\" value=\"true\" />\n"
        "                <Field name=\"direction\" type=\"vector3\" value=\"-0.45 -1 -0.35\" />\n"
        "                <Field name=\"color\" type=\"vector3\" value=\"1 0.96 0.86\" />\n"
        "                <Field name=\"intensity\" type=\"float32\" value=\"1.35\" />\n"
        "                <Field name=\"castShadows\" type=\"bool\" value=\"true\" />\n"
        "                <Field name=\"shadowBias\" type=\"float32\" value=\"0.004\" />\n"
        "                <Field name=\"shadowStrength\" type=\"float32\" value=\"0.45\" />\n"
        "                <Field name=\"shadowDistance\" type=\"float32\" value=\"24\" />\n"
        "            </Component>\n"
        "        </Ens>\n"
        "        <Ens stableId=\"world://examples/world/camera\" name=\"ExampleCamera\">\n"
        "            <Component type=\"SpaceComponent\">\n"
        "                <Field name=\"localPosition\" type=\"vector3\" value=\"5 3.2 7\" />\n"
        "                <Field name=\"localRotation\" type=\"quaternion\" value=\"-0.1819 0.2952 0.0574 0.9362\" />\n"
        "                <Field name=\"localScale\" type=\"vector3\" value=\"1 1 1\" />\n"
        "            </Component>\n"
        "            <Component type=\"Camera\">\n"
        "                <Field name=\"enabled\" type=\"bool\" value=\"true\" />\n"
        "                <Field name=\"fieldOfView\" type=\"float32\" value=\"60\" />\n"
        "                <Field name=\"nearPlane\" type=\"float32\" value=\"0.1\" />\n"
        "                <Field name=\"farPlane\" type=\"float32\" value=\"100\" />\n"
        "                <Field name=\"depth\" type=\"float32\" value=\"0\" />\n"
        "                <Field name=\"drawLayerMask\" type=\"uint32\" value=\"4294967295\" />\n"
        "                <Field name=\"clearMode\" type=\"ClearMode\" value=\"2\" />\n"
        "                <Field name=\"clearColor\" type=\"color4\" value=\"0.62 0.78 0.96 1\" />\n"
        "            </Component>\n"
        "        </Ens>\n"
        "    </Ens>\n"
        "</World>\n";

    Log::Info(("Example world generated: " + NormalizePath(worldPath)).c_str());
    return true;
}

//补齐示例场景的运行时渲染环境
void ExampleWorldGenerator::ApplyRuntimeEnvironment(Application& app)
{
    Mesh* cubeMesh = ResourceManager::Load<Mesh>(CubeMeshKey);
    Mesh* groundMesh = ResourceManager::Load<Mesh>(GroundMeshKey);
    MaterialShader* shader = ResourceManager::Load<MaterialShader>(ExampleShaderKey);
    Texture2D* skyTexture = ResourceManager::Load<Texture2D>(SkyTextureKey);
    if (!cubeMesh || !groundMesh || !shader || !skyTexture)
    {
        Log::Error("Example world runtime environment failed: required resources are missing.");
        return;
    }

    AssignShaderToMeshMaterials(cubeMesh, shader);
    AssignShaderToMeshMaterials(groundMesh, shader);

    World& world = app.GetWorld();
    Skybox* skybox = GetOrCreateWorldObject<Skybox>(world, "world://examples/world/skybox");
    if (!skybox) return;

    skybox->right.Set(skyTexture);
    skybox->left.Set(skyTexture);
    skybox->top.Set(skyTexture);
    skybox->bottom.Set(skyTexture);
    skybox->front.Set(skyTexture);
    skybox->back.Set(skyTexture);
    world.renderSettings.skybox.Set(skybox);
    world.renderSettings.skyboxEnabled = true;
    world.renderSettings.ambientColor = { 0.12f, 0.14f, 0.16f, 1.0f };
}
