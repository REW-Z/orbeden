#include "Editor/ExampleWorldGenerator.h"

#include "Application.h"
#include "Log/Log.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Skybox.h"
#include "Runtime/Object/Texture2D.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/World.h"

#include <cstddef>
#include <filesystem>
#include <fstream>

namespace
{
    constexpr const char* CubeMeshKey = "Resource/Mesh/cube.obj//Mesh/Main";
    constexpr const char* GroundMeshKey = "Resource/Mesh/ground.obj//Mesh/Main";
    constexpr const char* ExampleShaderKey = "Resource/Shader/blinn_phong_shadow";
    constexpr const char* SkyTextureKey = "Resource/Texture/sky_blue.png";

    constexpr const char* ProjectFileText =
        "<OrbedenProject version=\"1\" name=\"ExampleProject\" startupWorld=\"World/example_world.world\" resourceRoot=\"Resource\" scriptRoot=\"Script\" managedRoot=\"Managed\" />\n";

    constexpr const char* CubeObjText = R"ORB(mtllib ../Material/cube.mtl
o ExampleCube
v -1.0 -1.0 1.0
v 1.0 -1.0 1.0
v 1.0 1.0 1.0
v -1.0 1.0 1.0
v 1.0 -1.0 -1.0
v -1.0 -1.0 -1.0
v -1.0 1.0 -1.0
v 1.0 1.0 -1.0
v -1.0 -1.0 -1.0
v -1.0 -1.0 1.0
v -1.0 1.0 1.0
v -1.0 1.0 -1.0
v 1.0 -1.0 1.0
v 1.0 -1.0 -1.0
v 1.0 1.0 -1.0
v 1.0 1.0 1.0
v -1.0 1.0 1.0
v 1.0 1.0 1.0
v 1.0 1.0 -1.0
v -1.0 1.0 -1.0
v -1.0 -1.0 -1.0
v 1.0 -1.0 -1.0
v 1.0 -1.0 1.0
v -1.0 -1.0 1.0
vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0
vn 0.0 0.0 1.0
vn 0.0 0.0 -1.0
vn -1.0 0.0 0.0
vn 1.0 0.0 0.0
vn 0.0 1.0 0.0
vn 0.0 -1.0 0.0
usemtl CubeMaterial
f 1/1/1 2/2/1 3/3/1 4/4/1
f 5/1/2 6/2/2 7/3/2 8/4/2
f 9/1/3 10/2/3 11/3/3 12/4/3
f 13/1/4 14/2/4 15/3/4 16/4/4
f 17/1/5 18/2/5 19/3/5 20/4/5
f 21/1/6 22/2/6 23/3/6 24/4/6
)ORB";

    constexpr const char* GroundObjText = R"ORB(mtllib ../Material/ground.mtl
o ExampleGround
v -8.0 0.0 -8.0
v 8.0 0.0 -8.0
v 8.0 0.0 8.0
v -8.0 0.0 8.0
vt 0.0 0.0
vt 4.0 0.0
vt 4.0 4.0
vt 0.0 4.0
vn 0.0 1.0 0.0
usemtl GroundMaterial
f 1/1/1 2/2/1 3/3/1 4/4/1
)ORB";

    constexpr const char* CubeMtlText = R"ORB(newmtl CubeMaterial
Ka 0.08 0.06 0.05
Kd 0.86 0.42 0.20
Ks 0.55 0.48 0.42
Ns 48.0
shader Resource/Shader/blinn_phong_shadow
)ORB";

    constexpr const char* GroundMtlText = R"ORB(newmtl GroundMaterial
Ka 0.06 0.08 0.06
Kd 0.38 0.52 0.36
Ks 0.12 0.16 0.12
Ns 18.0
shader Resource/Shader/blinn_phong_shadow
)ORB";

    constexpr const char* BlinnPhongVertexShaderText = R"ORB(#version 430 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Tangent;

uniform mat4 u_Model;
uniform mat4 u_ViewProjection;
uniform mat4 u_LightViewProjection;

out vec3 v_WorldPosition;
out vec3 v_Normal;
out vec2 v_TexCoord;
out vec4 v_LightSpacePosition;

void main()
{
    vec4 worldPosition = u_Model * vec4(a_Position, 1.0);
    v_WorldPosition = worldPosition.xyz;
    v_Normal = mat3(u_Model) * a_Normal;
    v_TexCoord = a_TexCoord;
    v_LightSpacePosition = u_LightViewProjection * worldPosition;
    gl_Position = u_ViewProjection * worldPosition;
}
)ORB";

    constexpr const char* BlinnPhongFragmentShaderText = R"ORB(#version 430 core

in vec3 v_WorldPosition;
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec4 v_LightSpacePosition;

uniform vec3 u_CameraPosition;
uniform vec3 u_AmbientColor;
uniform vec3 u_DiffuseColor;
uniform vec3 u_SpecularColor;
uniform float u_Shininess;
uniform vec3 u_LightDirection;
uniform vec3 u_LightColor;
uniform float u_LightIntensity;
uniform bool u_HasDiffuseTexture;
uniform sampler2D u_DiffuseTexture;
uniform sampler2D u_ShadowMap;
uniform bool u_UseShadowMap;
uniform bool u_ReceiveShadows;
uniform float u_ShadowBias;
uniform float u_ShadowStrength;

out vec4 FragColor;

float SampleShadow()
{
    if (!u_UseShadowMap || !u_ReceiveShadows)
    {
        return 0.0;
    }

    vec3 projected = v_LightSpacePosition.xyz / v_LightSpacePosition.w;
    projected = projected * 0.5 + 0.5;
    if (projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0 || projected.z > 1.0)
    {
        return 0.0;
    }

    float closestDepth = texture(u_ShadowMap, projected.xy).r;
    float currentDepth = projected.z;
    return currentDepth - u_ShadowBias > closestDepth ? u_ShadowStrength : 0.0;
}

void main()
{
    vec3 albedo = u_DiffuseColor;
    if (u_HasDiffuseTexture)
    {
        albedo *= texture(u_DiffuseTexture, v_TexCoord).rgb;
    }

    vec3 normal = normalize(v_Normal);
    vec3 lightDir = normalize(-u_LightDirection);
    vec3 viewDir = normalize(u_CameraPosition - v_WorldPosition);
    vec3 halfDir = normalize(lightDir + viewDir);

    float diffuseTerm = max(dot(normal, lightDir), 0.0);
    float specularPower = max(u_Shininess, 1.0);
    float specularTerm = pow(max(dot(normal, halfDir), 0.0), specularPower);
    float shadow = SampleShadow();

    vec3 ambient = u_AmbientColor * albedo;
    vec3 direct = (diffuseTerm * albedo + specularTerm * u_SpecularColor) * u_LightColor * u_LightIntensity;
    vec3 color = ambient + direct * (1.0 - shadow);
    FragColor = vec4(color, 1.0);
}
)ORB";

    constexpr const char* ShadowDepthVertexShaderText = R"ORB(#version 430 core

layout(location = 0) in vec3 a_Position;

uniform mat4 u_Model;
uniform mat4 u_LightViewProjection;

void main()
{
    gl_Position = u_LightViewProjection * u_Model * vec4(a_Position, 1.0);
}
)ORB";

    constexpr const char* ShadowDepthFragmentShaderText = R"ORB(#version 430 core

void main()
{
}
)ORB";

    constexpr const char* SkyboxVertexShaderText = R"ORB(#version 430 core

layout(location = 0) in vec3 a_Position;

uniform mat4 u_ViewProjection;

out vec3 v_TexCoord;

void main()
{
    v_TexCoord = a_Position;
    vec4 position = u_ViewProjection * vec4(a_Position, 1.0);
    gl_Position = position.xyww;
}
)ORB";

    constexpr const char* SkyboxFragmentShaderText = R"ORB(#version 430 core

in vec3 v_TexCoord;

uniform samplerCube u_SkyboxTexture;

out vec4 FragColor;

void main()
{
    FragColor = texture(u_SkyboxTexture, v_TexCoord);
}
)ORB";

    constexpr const char* ExampleGameProjectText = R"ORB(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <AssemblyName>ExampleGame</AssemblyName>
    <RootNamespace>ExampleGame</RootNamespace>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
    <AppendRuntimeIdentifierToOutputPath>false</AppendRuntimeIdentifierToOutputPath>
  </PropertyGroup>

  <ItemGroup>
    <ProjectReference Include="..\..\OrbedenCore\Managed\Orbeden.Runtime\Orbeden.Runtime.csproj" PrivateAssets="all" Private="false" />
  </ItemGroup>

  <Target Name="RemoveRuntimeCopy" AfterTargets="Build">
    <Delete Files="$(OutputPath)Orbeden.Runtime.dll;$(OutputPath)Orbeden.Runtime.pdb;$(OutputPath)Orbeden.Runtime.deps.json" />
  </Target>
</Project>
)ORB";

    constexpr const char* DirectoryBuildPropsText = R"ORB(<Project>
  <PropertyGroup>
    <OutputPath>$(MSBuildThisFileDirectory)..\Managed\</OutputPath>
    <BaseIntermediateOutputPath>$(MSBuildThisFileDirectory)..\Managed\obj\</BaseIntermediateOutputPath>
    <MSBuildProjectExtensionsPath>$(BaseIntermediateOutputPath)</MSBuildProjectExtensionsPath>
  </PropertyGroup>
</Project>
)ORB";

    constexpr const char* GuiOverlayText = R"ORB(using System.Runtime.InteropServices;
using Orbeden;

namespace ExampleGame;

/// <summary>示例项目的运行时 GUI。</summary>
public static class GuiOverlay
{
    private static int clickCount;

    /// <summary>绘制示例运行时 GUI。</summary>
    [UnmanagedCallersOnly]
    public static void OnGui()
    {
        bool visible = GUI.BeginPanel("C# Runtime GUI");
        try
        {
            if (!visible) return;

            GUI.Label("Hello from Orbeden.Runtime");
            if (GUI.Button("Click from C#"))
            {
                clickCount++;
            }

            GUI.Label($"Clicks: {clickCount}");
        }
        finally
        {
            GUI.EndPanel();
        }
    }
}
)ORB";

    constexpr const char* SampleBehaviourText = R"ORB(using Orbeden;

namespace ExampleGame;

/// <summary>示例托管脚本行为。</summary>
public sealed class SampleBehaviour : ScriptBehaviour
{
    private vector3 startPosition;
    private float totalTime;
    private float elapsedTime;
    private int reportCount;

    /// <summary>脚本启动时调用。</summary>
    protected override void OnStart()
    {
        startPosition = Ens.Space.localPosition;
        Console.WriteLine($"SampleBehaviour start: Ens({EnsId.id}, {EnsId.version})");
    }

    /// <summary>脚本每帧更新时调用。</summary>
    protected override void OnUpdate(float deltaTime)
    {
        totalTime += deltaTime;
        SpaceComponent space = Ens.Space;
        vector3 position = startPosition;
        position.y += MathF.Sin(totalTime) * 0.25f;
        space.localPosition = position;

        StaticMeshRenderer? renderer = Ens.GetComponent<StaticMeshRenderer>();
        if (renderer != null)
        {
            renderer.castShadows = true;
            renderer.receiveShadows = true;
        }

        elapsedTime += deltaTime;
        if (elapsedTime < 2.0f) return;

        elapsedTime = 0.0f;
        reportCount++;
        Console.WriteLine($"SampleBehaviour update report: {reportCount}");
    }

    /// <summary>脚本结束时调用。</summary>
    protected override void OnEnd()
    {
        Console.WriteLine("SampleBehaviour end");
    }
}
)ORB";

    constexpr const char* ScriptGitIgnoreText = "bin/\nobj/\n";
    constexpr const char* ManagedGitIgnoreText = "*\n!.gitignore\n";

    constexpr unsigned char SkyBluePngBytes[] =
    {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D,
        0x24, 0x00, 0x00, 0x00, 0x01, 0x73, 0x52, 0x47,
        0x42, 0x00, 0xAE, 0xCE, 0x1C, 0xE9, 0x00, 0x00,
        0x00, 0x04, 0x67, 0x41, 0x4D, 0x41, 0x00, 0x00,
        0xB1, 0x8F, 0x0B, 0xFC, 0x61, 0x05, 0x00, 0x00,
        0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00,
        0x0E, 0xC3, 0x00, 0x00, 0x0E, 0xC3, 0x01, 0xC7,
        0x6F, 0xA8, 0x64, 0x00, 0x00, 0x00, 0x11, 0x49,
        0x44, 0x41, 0x54, 0x18, 0x57, 0x63, 0x58, 0x76,
        0xE5, 0xCB, 0x7F, 0x10, 0x66, 0x80, 0x31, 0x00,
        0x79, 0x10, 0x0D, 0xB5, 0x44, 0x69, 0xC0, 0x0F,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
        0xAE, 0x42, 0x60, 0x82,
    };

    std::string NormalizePath(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
    }

    bool WriteTextFile(const std::filesystem::path& path, const char* text)
    {
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output)
        {
            Log::Error(("Example project text file generate failed: " + NormalizePath(path)).c_str());
            return false;
        }

        output << text;
        return true;
    }

    bool WriteBinaryFile(const std::filesystem::path& path, const unsigned char* bytes, std::size_t size)
    {
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output)
        {
            Log::Error(("Example project binary file generate failed: " + NormalizePath(path)).c_str());
            return false;
        }

        output.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(size));
        return true;
    }

    void AssignShaderToMeshMaterials(Mesh* mesh, Shader* shader)
    {
        if (!mesh || !shader) return;

        for (SubMesh& subMesh : mesh->subMeshes)
        {
            Material* material = subMesh.material.Get();
            if (!material) continue;

            material->shader.Set(shader);
        }
    }

}

//判断项目是否使用内置示例 World 生成器
bool ExampleWorldGenerator::IsExampleProject(const std::string& projectName)
{
    return projectName == "ExampleProject";
}

//生成示例项目的资源、脚本和 World 文件
bool ExampleWorldGenerator::GenerateProjectFiles(const std::string& projectRoot)
{
    std::filesystem::path root(projectRoot);

    //写入项目描述和资源目录。
    bool succeeded = true;
    succeeded = WriteTextFile(root / "ExampleProject.oeproj", ProjectFileText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Mesh/cube.obj", CubeObjText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Mesh/ground.obj", GroundObjText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Material/cube.mtl", CubeMtlText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Material/ground.mtl", GroundMtlText) && succeeded;

    //写入示例和引擎内置 Shader。
    succeeded = WriteTextFile(root / "Resource/Shader/blinn_phong_shadow.vert.glsl", BlinnPhongVertexShaderText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Shader/blinn_phong_shadow.frag.glsl", BlinnPhongFragmentShaderText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Shader/shadow_depth.vert.glsl", ShadowDepthVertexShaderText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Shader/shadow_depth.frag.glsl", ShadowDepthFragmentShaderText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Shader/skybox.vert.glsl", SkyboxVertexShaderText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Shader/skybox.frag.glsl", SkyboxFragmentShaderText) && succeeded;
    succeeded = WriteBinaryFile(root / "Resource/Texture/sky_blue.png", SkyBluePngBytes, sizeof(SkyBluePngBytes)) && succeeded;

    //写入 C# 示例脚本工程。
    succeeded = WriteTextFile(root / "Script/ExampleGame.csproj", ExampleGameProjectText) && succeeded;
    succeeded = WriteTextFile(root / "Script/Directory.Build.props", DirectoryBuildPropsText) && succeeded;
    succeeded = WriteTextFile(root / "Script/GuiOverlay.cs", GuiOverlayText) && succeeded;
    succeeded = WriteTextFile(root / "Script/SampleBehaviour.cs", SampleBehaviourText) && succeeded;
    succeeded = WriteTextFile(root / "Script/.gitignore", ScriptGitIgnoreText) && succeeded;
    succeeded = WriteTextFile(root / "Managed/.gitignore", ManagedGitIgnoreText) && succeeded;

    //写入示例 World。
    succeeded = GenerateWorldFile(projectRoot, "World/example_world.world") && succeeded;
    if (succeeded)
    {
        Log::Info(("Example project generated: " + NormalizePath(root)).c_str());
    }

    return succeeded;
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
        "                <Field name=\"mesh\" type=\"Ref&lt;Mesh&gt;\" value=\"Resource/Mesh/cube.obj//Mesh/Main\" />\n"
        "                <Field name=\"drawLayer\" type=\"uint32\" value=\"1\" />\n"
        "                <Field name=\"drawQueue\" type=\"DrawQueue\" value=\"0\" />\n"
        "                <Field name=\"castShadows\" type=\"bool\" value=\"true\" />\n"
        "                <Field name=\"receiveShadows\" type=\"bool\" value=\"true\" />\n"
        "            </Component>\n"
        "            <Component type=\"ScriptsComponent\">\n"
        "                <Script enabled=\"true\" assembly=\"ExampleGame\" type=\"ExampleGame.SampleBehaviour\" />\n"
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
        "                <Field name=\"mesh\" type=\"Ref&lt;Mesh&gt;\" value=\"Resource/Mesh/ground.obj//Mesh/Main\" />\n"
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
        "                <Field name=\"color\" type=\"color\" value=\"1 0.96 0.86\" />\n"
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
    Shader* shader = ResourceManager::Load<Shader>(ExampleShaderKey);
    Texture2D* skyTexture = ResourceManager::Load<Texture2D>(SkyTextureKey);
    if (!cubeMesh || !groundMesh || !shader || !skyTexture)
    {
        Log::Error("Example world runtime environment failed: required resources are missing.");
        return;
    }

    AssignShaderToMeshMaterials(cubeMesh, shader);
    AssignShaderToMeshMaterials(groundMesh, shader);

    World& world = app.GetWorld();
    Skybox* skybox = world.renderSettings.skybox.Get();
    if (!skybox)
    {
        skybox = Object::CreateInstance<Skybox>();
    }
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
