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
    constexpr const char* ExampleShaderKey = "Resource/Shader/blinn_phong_shadow.orbshader";
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
shader Resource/Shader/blinn_phong_shadow.orbshader
)ORB";

    constexpr const char* GroundMtlText = R"ORB(newmtl GroundMaterial
Ka 0.06 0.08 0.06
Kd 0.38 0.52 0.36
Ks 0.12 0.16 0.12
Ns 18.0
shader Resource/Shader/blinn_phong_shadow.orbshader
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
uniform vec4 u_AmbientColor;
uniform vec4 u_DiffuseColor;
uniform vec4 u_SpecularColor;
uniform float u_Shininess;
uniform vec3 u_LightDirection;
uniform vec4 u_LightColor;
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
    vec3 albedo = u_DiffuseColor.rgb;
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

    vec3 ambient = u_AmbientColor.rgb * albedo;
    vec3 direct = (diffuseTerm * albedo + specularTerm * u_SpecularColor.rgb) * u_LightColor.rgb * u_LightIntensity;
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
    <InvariantGlobalization>true</InvariantGlobalization>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
    <AppendRuntimeIdentifierToOutputPath>false</AppendRuntimeIdentifierToOutputPath>
  </PropertyGroup>

  <ItemGroup>
    <Reference Include="OrbedenCore.CSharp">
      <HintPath>$(OrbedenSdkPath)Managed\OrbedenCore.CSharp\OrbedenCore.CSharp.dll</HintPath>
      <Private>false</Private>
    </Reference>
  </ItemGroup>
</Project>
)ORB";

    constexpr const char* DirectoryBuildPropsText = R"ORB(<Project>
  <PropertyGroup>
    <OutputPath>$(MSBuildThisFileDirectory)..\Managed\</OutputPath>
    <BaseIntermediateOutputPath>$(MSBuildThisFileDirectory)..\Managed\obj\</BaseIntermediateOutputPath>
    <MSBuildProjectExtensionsPath>$(BaseIntermediateOutputPath)</MSBuildProjectExtensionsPath>
    <OrbedenSdkPath Condition="'$(OrbedenSdkPath)' == ''">$(MSBuildThisFileDirectory)..\..\OrbedenEditor\Sdk\</OrbedenSdkPath>
  </PropertyGroup>
</Project>
)ORB";

    constexpr const char* GameModuleText = R"ORB(using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OrbedenCore.CSharp;

namespace ExampleGame;

/// <summary>ExampleGame AOT 模块入口。</summary>
public static class GameModule
{
    /// <summary>初始化游戏模块。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Initialize", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Initialize(IntPtr nativeApi)
    {
        OrbedenCoreRuntime.Initialize(nativeApi);
        MountedScriptRuntime.Initialize();
    }

    /// <summary>关闭游戏模块。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Shutdown", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Shutdown()
    {
        MountedScriptRuntime.Shutdown();
    }

    /// <summary>更新游戏模块。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Update", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Update(float deltaTime)
    {
        MountedScriptRuntime.Update(deltaTime);
    }

    /// <summary>绘制游戏模块 GUI。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_DrawGui", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_DrawGui()
    {
        GuiOverlay.Draw();
    }
}
)ORB";

    constexpr const char* MountedScriptRuntimeText = R"ORB(using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using OrbedenCore.CSharp;

namespace ExampleGame;

/// <summary>按 world 脚本挂载清单启动用户脚本。</summary>
internal static class MountedScriptRuntime
{
    private sealed class ScriptMount
    {
        public string StableId = string.Empty;
        public string Type = string.Empty;
        public Dictionary<string, string> Values = [];
    }

    private static readonly List<ScriptBehaviour> scripts = [];

    /// <summary>读取当前 world 的脚本挂载清单并启动脚本。</summary>
    public static void Initialize()
    {
        Shutdown();

        string sidecarPath = GetStartupWorldSidecarPath();
        if (string.IsNullOrEmpty(sidecarPath) || !File.Exists(sidecarPath))
        {
            Console.WriteLine("MountedScriptRuntime: world script sidecar was not found.");
            return;
        }

        foreach (ScriptMount mount in ReadScriptMounts(sidecarPath))
        {
            Ens ens = Ens.Find(mount.StableId);
            if (!ens.IsValid)
            {
                Console.WriteLine($"MountedScriptRuntime: missing Ens stableId '{mount.StableId}'.");
                continue;
            }

            ScriptBehaviour? script = CreateScript(mount, ens);
            if (script == null)
            {
                Console.WriteLine($"MountedScriptRuntime: unsupported script type '{mount.Type}'.");
                continue;
            }

            scripts.Add(script);
        }

        foreach (ScriptBehaviour script in scripts)
        {
            script.InvokeStart();
        }
    }

    /// <summary>更新已挂载脚本。</summary>
    public static void Update(float deltaTime)
    {
        foreach (ScriptBehaviour script in scripts)
        {
            if (script.Ens.IsValid)
            {
                script.InvokeUpdate(deltaTime);
            }
        }
    }

    /// <summary>关闭已挂载脚本。</summary>
    public static void Shutdown()
    {
        for (int index = scripts.Count - 1; index >= 0; index--)
        {
            scripts[index].InvokeEnd();
        }

        scripts.Clear();
    }

    //创建 AOT 友好的脚本实例，不通过反射构造。
    private static ScriptBehaviour? CreateScript(ScriptMount mount, Ens ens)
    {
        return StripAssemblyName(mount.Type) switch
        {
            "ExampleGame.SampleBehaviour" => CreateSampleBehaviour(ens, mount.Values),
            "ExampleGame.CubeTestBehaviour" => CreateCubeTestBehaviour(ens, mount.Values),
            _ => null,
        };
    }

    //创建示例脚本并写入 sidecar 序列化字段。
    private static SampleBehaviour CreateSampleBehaviour(Ens ens, IReadOnlyDictionary<string, string> values)
    {
        SampleBehaviour script = new(ens);
        script.ApplySerializedValues(values);
        return script;
    }

    //创建 Cube 测试脚本并写入 sidecar 序列化字段。
    private static CubeTestBehaviour CreateCubeTestBehaviour(Ens ens, IReadOnlyDictionary<string, string> values)
    {
        CubeTestBehaviour script = new(ens);
        script.ApplySerializedValues(values);
        return script;
    }

    //获取当前启动 world 对应的脚本 sidecar。
    private static string GetStartupWorldSidecarPath()
    {
        string projectRoot = PathDefines.ProjectRoot;
        if (string.IsNullOrWhiteSpace(projectRoot) || !Directory.Exists(projectRoot)) return string.Empty;

        string? projectFile = Directory.EnumerateFiles(projectRoot, "*.oeproj", SearchOption.TopDirectoryOnly).FirstOrDefault();
        if (string.IsNullOrEmpty(projectFile)) return string.Empty;

        string startupWorld = ReadAttribute(File.ReadAllText(projectFile), "startupWorld");
        if (string.IsNullOrWhiteSpace(startupWorld)) return string.Empty;

        string worldPath = Path.Combine(projectRoot, startupWorld.Replace('/', Path.DirectorySeparatorChar));
        return Path.GetFullPath(worldPath) + ".scripts.json";
    }

    //读取 XML 单行项目文件中的属性。
    private static string ReadAttribute(string text, string name)
    {
        string token = name + "=\"";
        int start = text.IndexOf(token, StringComparison.Ordinal);
        if (start < 0) return string.Empty;

        start += token.Length;
        int end = text.IndexOf('"', start);
        return end > start ? text[start..end] : string.Empty;
    }

    //读取 world sidecar 脚本挂载项。
    private static IEnumerable<ScriptMount> ReadScriptMounts(string sidecarPath)
    {
        using JsonDocument document = JsonDocument.Parse(File.ReadAllText(sidecarPath));
        if (!document.RootElement.TryGetProperty("scripts", out JsonElement scriptsElement)) yield break;
        if (scriptsElement.ValueKind != JsonValueKind.Array) yield break;

        foreach (JsonElement element in scriptsElement.EnumerateArray())
        {
            ScriptMount? mount = ReadScriptMount(element);
            if (mount != null) yield return mount;
        }
    }

    //读取单个脚本挂载项。
    private static ScriptMount? ReadScriptMount(JsonElement element)
    {
        string stableId = element.TryGetProperty("stableId", out JsonElement stableIdElement) ? stableIdElement.GetString() ?? string.Empty : string.Empty;
        string type = element.TryGetProperty("type", out JsonElement typeElement) ? typeElement.GetString() ?? string.Empty : string.Empty;
        if (string.IsNullOrWhiteSpace(stableId) || string.IsNullOrWhiteSpace(type)) return null;

        ScriptMount mount = new() { StableId = stableId, Type = StripAssemblyName(type) };
        if (!element.TryGetProperty("values", out JsonElement valuesElement)) return mount;
        if (valuesElement.ValueKind != JsonValueKind.Object) return mount;

        foreach (JsonProperty property in valuesElement.EnumerateObject())
        {
            mount.Values[property.Name] = ReadSerializedValue(property.Value);
        }

        return mount;
    }

    //读取 sidecar 字段值文本。
    private static string ReadSerializedValue(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object && element.TryGetProperty("value", out JsonElement valueElement))
        {
            return GetJsonValueText(valueElement);
        }

        return GetJsonValueText(element);
    }

    //把 JsonElement 转成可解析文本。
    private static string GetJsonValueText(JsonElement element)
    {
        return element.ValueKind == JsonValueKind.String ? element.GetString() ?? string.Empty : element.GetRawText();
    }

    //规范化脚本类型名。
    private static string StripAssemblyName(string typeName)
    {
        string value = typeName.Trim();
        int commaIndex = value.IndexOf(',');
        return commaIndex >= 0 ? value[..commaIndex].Trim() : value;
    }
}

/// <summary>脚本 sidecar 字段值解析工具。</summary>
internal static class ScriptValueReader
{
    /// <summary>读取 string 字段。</summary>
    public static bool TryGetString(IReadOnlyDictionary<string, string> values, string name, out string value)
    {
        if (values.TryGetValue(name, out string? text))
        {
            value = text;
            return true;
        }

        value = string.Empty;
        return false;
    }

    /// <summary>读取 bool 字段。</summary>
    public static bool TryGetBool(IReadOnlyDictionary<string, string> values, string name, out bool value)
    {
        value = false;
        return values.TryGetValue(name, out string? text) && bool.TryParse(text, out value);
    }

    /// <summary>读取 int 字段。</summary>
    public static bool TryGetInt(IReadOnlyDictionary<string, string> values, string name, out int value)
    {
        value = 0;
        return values.TryGetValue(name, out string? text)
            && int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out value);
    }

    /// <summary>读取 float 字段。</summary>
    public static bool TryGetFloat(IReadOnlyDictionary<string, string> values, string name, out float value)
    {
        value = 0.0f;
        return values.TryGetValue(name, out string? text)
            && float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value);
    }

    /// <summary>读取 vector3 字段。</summary>
    public static bool TryGetVector3(IReadOnlyDictionary<string, string> values, string name, out vector3 value)
    {
        value = new vector3();
        if (!values.TryGetValue(name, out string? text)) return false;

        string[] parts = text.Split(new[] { ' ', ',', ';' }, StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length != 3) return false;
        if (!float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out float x)) return false;
        if (!float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out float y)) return false;
        if (!float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out float z)) return false;

        value = new vector3(x, y, z);
        return true;
    }
}
)ORB";

    constexpr const char* GuiOverlayText = R"ORB(using OrbedenCore.CSharp;

namespace ExampleGame;

/// <summary>示例项目的运行时 GUI。</summary>
public static class GuiOverlay
{
    private static int clickCount;

    /// <summary>绘制示例运行时 GUI。</summary>
    public static void Draw()
    {
        bool visible = GUI.BeginPanel("C# Runtime GUI");
        try
        {
            if (!visible) return;

            GUI.Label("Hello from OrbedenCore.CSharp");
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

    constexpr const char* SampleBehaviourText = R"ORB(using System.Collections.Generic;
using OrbedenCore.CSharp;

namespace ExampleGame;

/// <summary>示例托管脚本行为。</summary>
public sealed class SampleBehaviour : ScriptBehaviour
{
    [SerializeField]
    private vector3 startPosition;
    [SerializeField]
    private float totalTime;
    [SerializeField]
    private float elapsedTime;
    [SerializeField]
    private int reportCount;

    /// <summary>创建示例托管脚本行为。</summary>
    public SampleBehaviour(Ens ens) : base(ens) {}

    /// <summary>应用 world sidecar 中保存的序列化字段。</summary>
    internal void ApplySerializedValues(IReadOnlyDictionary<string, string> values)
    {
        if (ScriptValueReader.TryGetVector3(values, nameof(startPosition), out vector3 startPositionValue)) startPosition = startPositionValue;
        if (ScriptValueReader.TryGetFloat(values, nameof(totalTime), out float totalTimeValue)) totalTime = totalTimeValue;
        if (ScriptValueReader.TryGetFloat(values, nameof(elapsedTime), out float elapsedTimeValue)) elapsedTime = elapsedTimeValue;
        if (ScriptValueReader.TryGetInt(values, nameof(reportCount), out int reportCountValue)) reportCount = reportCountValue;
    }

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

    constexpr const char* CubeTestBehaviourText = R"ORB(using System.Collections.Generic;
using OrbedenCore.CSharp;

namespace ExampleGame;

/// <summary>挂在示例 Cube 上的脚本组件测试。</summary>
public sealed class CubeTestBehaviour : ScriptBehaviour
{
    [SerializeField]
    private string label = "Cube script component";
    [SerializeField]
    private bool animateScale = true;
    [SerializeField]
    private float pulseAmplitude = 0.18f;
    [SerializeField]
    private float pulseSpeed = 2.5f;
    [SerializeField]
    private vector3 debugOffset = new(0.0f, 0.0f, 0.0f);

    private vector3 baseScale;
    private float elapsedTime;
    private int updateCount;

    /// <summary>创建 Cube 脚本组件测试。</summary>
    public CubeTestBehaviour(Ens ens) : base(ens) {}

    /// <summary>Inspector 中显示当前运行状态。</summary>
    public string Status => $"{label}: {updateCount} updates";

    /// <summary>应用 world sidecar 中保存的序列化字段。</summary>
    internal void ApplySerializedValues(IReadOnlyDictionary<string, string> values)
    {
        if (ScriptValueReader.TryGetString(values, nameof(label), out string labelValue)) label = labelValue;
        if (ScriptValueReader.TryGetBool(values, nameof(animateScale), out bool animateScaleValue)) animateScale = animateScaleValue;
        if (ScriptValueReader.TryGetFloat(values, nameof(pulseAmplitude), out float pulseAmplitudeValue)) pulseAmplitude = pulseAmplitudeValue;
        if (ScriptValueReader.TryGetFloat(values, nameof(pulseSpeed), out float pulseSpeedValue)) pulseSpeed = pulseSpeedValue;
        if (ScriptValueReader.TryGetVector3(values, nameof(debugOffset), out vector3 debugOffsetValue)) debugOffset = debugOffsetValue;
    }

    /// <summary>脚本启动时调用。</summary>
    protected override void OnStart()
    {
        baseScale = Ens.Space.localScale;
        Console.WriteLine($"CubeTestBehaviour start: {label}");
    }

    /// <summary>脚本每帧更新时调用。</summary>
    protected override void OnUpdate(float deltaTime)
    {
        elapsedTime += deltaTime;
        updateCount++;

        if (animateScale)
        {
            float scale = 1.0f + MathF.Sin(elapsedTime * pulseSpeed) * pulseAmplitude;
            Ens.Space.localScale = new vector3(baseScale.x * scale, baseScale.y * scale, baseScale.z * scale);
        }

        if (debugOffset.x != 0.0f || debugOffset.y != 0.0f || debugOffset.z != 0.0f)
        {
            vector3 position = Ens.Space.localPosition;
            position.x += debugOffset.x * deltaTime;
            position.y += debugOffset.y * deltaTime;
            position.z += debugOffset.z * deltaTime;
            Ens.Space.localPosition = position;
        }
    }

    /// <summary>脚本结束时调用。</summary>
    protected override void OnEnd()
    {
        Ens.Space.localScale = baseScale;
        Console.WriteLine($"CubeTestBehaviour end: {label}");
    }
}
)ORB";

    constexpr const char* ScriptGitIgnoreText = "bin/\nobj/\n";
    constexpr const char* ManagedGitIgnoreText = "*\n!.gitignore\n";
    constexpr const char* WorldScriptsText = R"ORB({
  "scripts": [
    {
      "stableId": "world://examples/world/cube",
      "type": "ExampleGame.SampleBehaviour"
    },
    {
      "stableId": "world://examples/world/cube",
      "type": "ExampleGame.CubeTestBehaviour",
      "values": {
        "label": {
          "type": "string",
          "value": "Cube sidecar test"
        },
        "animateScale": {
          "type": "bool",
          "value": "true"
        },
        "pulseAmplitude": {
          "type": "float",
          "value": "0.18"
        },
        "pulseSpeed": {
          "type": "float",
          "value": "2.5"
        },
        "debugOffset": {
          "type": "vector3",
          "value": "0 0 0"
        }
      }
    }
  ]
}
)ORB";

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

    std::string ToCleanPath(const std::filesystem::path& path)
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
            Log::Error(("Example project text file generate failed: " + ToCleanPath(path)).c_str());
            return false;
        }

        output << text;
        return true;
    }

    bool WriteOrbShaderFile(const std::filesystem::path& path, const char* vertexText, const char* fragmentText)
    {
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output)
        {
            Log::Error(("Example project orbshader file generate failed: " + ToCleanPath(path)).c_str());
            return false;
        }

        output << "--------vert\n";
        output << vertexText;
        output << "\n--------frag\n";
        output << fragmentText;
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
            Log::Error(("Example project binary file generate failed: " + ToCleanPath(path)).c_str());
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
    succeeded = WriteOrbShaderFile(root / "Resource/Shader/blinn_phong_shadow.orbshader", BlinnPhongVertexShaderText, BlinnPhongFragmentShaderText) && succeeded;
    succeeded = WriteOrbShaderFile(root / "Resource/Shader/shadow_depth.orbshader", ShadowDepthVertexShaderText, ShadowDepthFragmentShaderText) && succeeded;
    succeeded = WriteOrbShaderFile(root / "Resource/Shader/skybox.orbshader", SkyboxVertexShaderText, SkyboxFragmentShaderText) && succeeded;
    succeeded = WriteBinaryFile(root / "Resource/Texture/sky_blue.png", SkyBluePngBytes, sizeof(SkyBluePngBytes)) && succeeded;

    //写入 C# 示例脚本工程。
    succeeded = WriteTextFile(root / "Script/ExampleGame.csproj", ExampleGameProjectText) && succeeded;
    succeeded = WriteTextFile(root / "Script/Directory.Build.props", DirectoryBuildPropsText) && succeeded;
    succeeded = WriteTextFile(root / "Script/GameModule.cs", GameModuleText) && succeeded;
    succeeded = WriteTextFile(root / "Script/MountedScriptRuntime.cs", MountedScriptRuntimeText) && succeeded;
    succeeded = WriteTextFile(root / "Script/GuiOverlay.cs", GuiOverlayText) && succeeded;
    succeeded = WriteTextFile(root / "Script/SampleBehaviour.cs", SampleBehaviourText) && succeeded;
    succeeded = WriteTextFile(root / "Script/CubeTestBehaviour.cs", CubeTestBehaviourText) && succeeded;
    succeeded = WriteTextFile(root / "Script/.gitignore", ScriptGitIgnoreText) && succeeded;
    succeeded = WriteTextFile(root / "Managed/.gitignore", ManagedGitIgnoreText) && succeeded;
    succeeded = WriteTextFile(root / "World/example_world.world.scripts.json", WorldScriptsText) && succeeded;

    //写入示例 World。
    succeeded = GenerateWorldFile(projectRoot, "World/example_world.world") && succeeded;
    if (succeeded)
    {
        Log::Info(("Example project generated: " + ToCleanPath(root)).c_str());
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
        Log::Error(("Example world generate failed: " + ToCleanPath(worldPath)).c_str());
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
        "                <Field name=\"color\" type=\"color\" value=\"1 0.96 0.86 1\" />\n"
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
        "                <Field name=\"clearColor\" type=\"color\" value=\"0.62 0.78 0.96 1\" />\n"
        "            </Component>\n"
        "        </Ens>\n"
        "    </Ens>\n"
        "</World>\n";

    Log::Info(("Example world generated: " + ToCleanPath(worldPath)).c_str());
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
