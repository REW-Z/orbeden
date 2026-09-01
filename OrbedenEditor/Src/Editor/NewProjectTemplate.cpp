#include "Editor/NewProjectTemplate.h"

#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"

#include <cstddef>
#include <filesystem>
#include <fstream>

namespace
{
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

    constexpr const char* ShadowCommonText = R"ORB(--------vert
out vec4 v_LightSpacePosition;

--------frag
in vec4 v_LightSpacePosition;

uniform sampler2D u_ShadowMap;
uniform bool u_UseShadowMap;
uniform bool u_ReceiveShadows;
uniform float u_ShadowBias;
uniform float u_ShadowStrength;

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

    constexpr const char* CameraTextureCommonText = R"ORB(--------frag
uniform sampler2D u_CameraColorTexture;
uniform sampler2D u_CameraDepthTexture;
uniform bool u_UseCameraTextures;
uniform float u_CameraNearPlane;
uniform float u_CameraFarPlane;

vec2 GetCameraScreenUv(vec4 clipPosition)
{
    vec2 ndc = clipPosition.xy / max(clipPosition.w, 0.00001);
    return ndc * 0.5 + 0.5;
}

vec2 ClampCameraScreenUv(vec2 uv)
{
    return clamp(uv, vec2(0.001), vec2(0.999));
}

float LinearizeCameraDepth(float depth)
{
    float ndcDepth = depth * 2.0 - 1.0;
    float denominator = u_CameraFarPlane + u_CameraNearPlane - ndcDepth * (u_CameraFarPlane - u_CameraNearPlane);
    return (2.0 * u_CameraNearPlane * u_CameraFarPlane) / max(denominator, 0.00001);
}

vec2 RejectForegroundCameraUv(vec2 baseUv, vec2 distortedUv, float surfaceDepth)
{
    float sceneDepth = LinearizeCameraDepth(texture(u_CameraDepthTexture, distortedUv).r);
    float refractorDepth = LinearizeCameraDepth(surfaceDepth);
    float depthBias = max(u_CameraNearPlane * 0.25, 0.01);
    return sceneDepth + depthBias < refractorDepth ? baseUv : distortedUv;
}
)ORB";

    constexpr const char* RefractionVertexShaderText = R"ORB(#version 430 core

layout(location = 0) in vec3 a_Position;
layout(location = 2) in vec2 a_TexCoord;

uniform mat4 u_Model;
uniform mat4 u_ViewProjection;

out vec2 v_TexCoord;
out vec4 v_ClipPosition;

void main()
{
    v_TexCoord = a_TexCoord;
    v_ClipPosition = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
    gl_Position = v_ClipPosition;
}
)ORB";

    constexpr const char* RainGlassFragmentShaderText = R"ORB(#version 430 core

#include "Builtin/camera_texture_common.orbinc"

in vec2 v_TexCoord;
in vec4 v_ClipPosition;

uniform float u_Time;
uniform float u_RefractionStrength;
uniform float u_DropletScale;
uniform float u_FlowSpeed;
uniform float u_Opacity;
uniform vec4 u_TintColor;

out vec4 FragColor;

float RandomCell(vec2 cell)
{
    return fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
}

vec2 CalculateRainOffset(vec2 uv)
{
    float scale = max(u_DropletScale, 1.0);
    vec2 scaledUv = uv * scale;
    vec2 cell = floor(scaledUv);
    vec2 localUv = fract(scaledUv);
    float randomValue = RandomCell(cell);
    float dropY = fract(randomValue + u_Time * max(u_FlowSpeed, 0.0));
    vec2 delta = localUv - vec2(randomValue, dropY);

    float droplet = 1.0 - smoothstep(0.03, 0.22, length(delta * vec2(1.0, 1.7)));
    float trailWidth = 1.0 - smoothstep(0.02, 0.08, abs(delta.x));
    float trailLength = smoothstep(0.0, 0.7, delta.y) * (1.0 - smoothstep(0.7, 0.95, delta.y));
    float strength = droplet + trailWidth * trailLength * 0.25;
    return vec2(delta.x, -0.35) * strength * max(u_RefractionStrength, 0.0);
}

void main()
{
    float opacity = clamp(u_Opacity, 0.0, 1.0);
    if (!u_UseCameraTextures)
    {
        FragColor = vec4(u_TintColor.rgb, opacity * 0.12);
        return;
    }

    vec2 screenUv = ClampCameraScreenUv(GetCameraScreenUv(v_ClipPosition));
    vec2 distortedUv = ClampCameraScreenUv(screenUv + CalculateRainOffset(v_TexCoord));
    distortedUv = RejectForegroundCameraUv(screenUv, distortedUv, gl_FragCoord.z);

    vec3 sceneColor = texture(u_CameraColorTexture, distortedUv).rgb;
    vec3 tintedColor = mix(sceneColor, sceneColor * u_TintColor.rgb, 0.25);
    FragColor = vec4(tintedColor, opacity);
}
)ORB";

    constexpr const char* HeatWaveFragmentShaderText = R"ORB(#version 430 core

#include "Builtin/camera_texture_common.orbinc"

in vec2 v_TexCoord;
in vec4 v_ClipPosition;

uniform float u_Time;
uniform float u_DistortionStrength;
uniform float u_NoiseScale;
uniform float u_DistortionSpeed;
uniform float u_EdgeFade;
uniform float u_Opacity;
uniform vec4 u_TintColor;

out vec4 FragColor;

void main()
{
    float opacity = clamp(u_Opacity, 0.0, 1.0);
    if (!u_UseCameraTextures)
    {
        FragColor = vec4(u_TintColor.rgb, opacity * 0.08);
        return;
    }

    vec2 screenUv = ClampCameraScreenUv(GetCameraScreenUv(v_ClipPosition));
    float scale = max(u_NoiseScale, 1.0);
    float phase = u_Time * u_DistortionSpeed;
    float waveX = sin(v_TexCoord.y * scale + phase * 1.7);
    float waveY = sin(v_TexCoord.x * scale * 0.73 - phase);

    float edgeDistance = min(min(v_TexCoord.x, 1.0 - v_TexCoord.x), min(v_TexCoord.y, 1.0 - v_TexCoord.y));
    float edgeMask = smoothstep(0.0, max(u_EdgeFade, 0.0001), edgeDistance);
    vec2 offset = vec2(waveX, waveY) * max(u_DistortionStrength, 0.0) * edgeMask;

    vec2 distortedUv = ClampCameraScreenUv(screenUv + offset);
    distortedUv = RejectForegroundCameraUv(screenUv, distortedUv, gl_FragCoord.z);
    vec3 sceneColor = texture(u_CameraColorTexture, distortedUv).rgb;
    vec3 tintedColor = mix(sceneColor, sceneColor * u_TintColor.rgb, 0.15);
    FragColor = vec4(tintedColor, opacity * edgeMask);
}
)ORB";

    constexpr const char* ScriptProjectTemplate = R"ORB(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <AssemblyName>{{PROJECT_NAME}}</AssemblyName>
    <RootNamespace>{{PROJECT_NAME}}</RootNamespace>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <InvariantGlobalization>true</InvariantGlobalization>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
    <AppendRuntimeIdentifierToOutputPath>false</AppendRuntimeIdentifierToOutputPath>
  </PropertyGroup>

  <ItemGroup>
    <Reference Include="OrbedenCore.CSharp">
      <HintPath>Lib\OrbedenCore.CSharp.dll</HintPath>
      <Private>true</Private>
    </Reference>
    <TrimmerRootAssembly Include="$(AssemblyName)" />
    <TrimmerRootAssembly Include="OrbedenCore.CSharp" />
  </ItemGroup>
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

    constexpr const char* AotExportsText = R"ORB(using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Orbeden;

// NativeAOT 只导出游戏主程序集中的入口；每个阶段在此进入托管域一次。
internal static class OrbedenAotExports
{
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Initialize", CallConvs = [typeof(CallConvCdecl)])]
    public static void Initialize(IntPtr nativeApi) => GameScriptRuntime.Initialize(nativeApi);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Shutdown", CallConvs = [typeof(CallConvCdecl)])]
    public static void Shutdown() => GameScriptRuntime.Shutdown();

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Update", CallConvs = [typeof(CallConvCdecl)])]
    public static void Update(float deltaTime) => GameScriptRuntime.Update(deltaTime);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_FixedUpdate", CallConvs = [typeof(CallConvCdecl)])]
    public static void FixedUpdate(float fixedDeltaTime) => GameScriptRuntime.FixedUpdate(fixedDeltaTime);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_LateUpdate", CallConvs = [typeof(CallConvCdecl)])]
    public static void LateUpdate(float deltaTime) => GameScriptRuntime.LateUpdate(deltaTime);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_EnsWorldActiveChanged", CallConvs = [typeof(CallConvCdecl)])]
    public static void EnsWorldActiveChanged(EnsId ens, byte worldActive) =>
        GameScriptRuntime.OnEnsWorldActiveChanged(ens, worldActive != 0);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_EnsDestroyed", CallConvs = [typeof(CallConvCdecl)])]
    public static void EnsDestroyed(EnsId ens) => GameScriptRuntime.OnEnsDestroyed(ens);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_DrawGui", CallConvs = [typeof(CallConvCdecl)])]
    public static void DrawGui() => GameScriptRuntime.DrawGUI();
}
)ORB";

    constexpr const char* NativeCMakeTemplate = R"ORB(cmake_minimum_required(VERSION 3.26)

project({{PROJECT_NAME}}Native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ORBEDEN_ENGINE_ROOT "" CACHE PATH "Orbeden repository root")
set(ORBEDEN_CORE_LIB "" CACHE FILEPATH "Editor OrbedenCore import library")

if(NOT ORBEDEN_ENGINE_ROOT OR NOT EXISTS "${ORBEDEN_ENGINE_ROOT}/Tools/OrbedenMetaGen/OrbedenMetaGen.csproj")
    message(FATAL_ERROR "ORBEDEN_ENGINE_ROOT is invalid")
endif()
if(NOT ORBEDEN_CORE_LIB OR NOT EXISTS "${ORBEDEN_CORE_LIB}")
    message(FATAL_ERROR "ORBEDEN_CORE_LIB is invalid")
endif()

file(GLOB_RECURSE GAME_NATIVE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
set(GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Generated")
set(GENERATED_REFLECTION "${GENERATED_DIRECTORY}/Reflection.Generated.cpp")

add_custom_command(
    OUTPUT "${GENERATED_REFLECTION}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIRECTORY}"
    COMMAND dotnet run --project "${ORBEDEN_ENGINE_ROOT}/Tools/OrbedenMetaGen/OrbedenMetaGen.csproj" -- "${CMAKE_CURRENT_SOURCE_DIR}" "${GENERATED_DIRECTORY}" --game-module
    DEPENDS ${GAME_NATIVE_SOURCES} "${ORBEDEN_ENGINE_ROOT}/Tools/OrbedenMetaGen/Program.cs"
    VERBATIM)

add_library({{PROJECT_NAME}}Native SHARED ${GAME_NATIVE_SOURCES} "${GENERATED_REFLECTION}")
target_include_directories({{PROJECT_NAME}}Native PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${ORBEDEN_ENGINE_ROOT}/OrbedenCore/Src")
target_link_libraries({{PROJECT_NAME}}Native PRIVATE "${ORBEDEN_CORE_LIB}")
target_compile_options({{PROJECT_NAME}}Native PRIVATE /utf-8)
set_target_properties({{PROJECT_NAME}}Native PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
)ORB";

    constexpr const char* NativeModuleTemplate = R"ORB(#include "Scripting/NativeGameModule.h"

extern "C" void OrbedenGameNative_RegisterReflection();

namespace
{
    const OrbedenNativeGameModuleApi ModuleApi
    {
        OrbedenNativeGameModuleAbiVersion,
        sizeof(OrbedenNativeGameModuleApi),
        "{{PROJECT_NAME}}Native",
        &OrbedenGameNative_RegisterReflection,
    };
}

extern "C" ORBEDEN_GAME_MODULE_EXPORT const OrbedenNativeGameModuleApi* OrbedenGameNative_GetApi()
{
    return &ModuleApi;
}
)ORB";

    constexpr const char* SampleNativeBehaviourHeader = R"ORB(#pragma once

#include "Scripting/ScriptBehaviour.h"

//无需 C# binding 的高性能原生脚本组件。
class SampleNativeBehaviour final : public ScriptBehaviour
{
    OBJECT_TYPE_DECLARE(SampleNativeBehaviour)

public:
    float32 speed = 2.0f;

private:
    float32 elapsedTime = 0.0f;

protected:
    void OnStart();
    void OnUpdate(float32 deltaTime);
    void OnLateUpdate(float32 deltaTime);
    void OnDrawGUI();
    void OnEnd();
};
)ORB";

    constexpr const char* SampleNativeBehaviourSource = R"ORB(#include "SampleNativeBehaviour.h"

#include "Runtime/Ens.h"
#include "Runtime/Object/TransformComponent.h"

#include <cmath>

OBJECT_TYPE_IMPLEMENT(SampleNativeBehaviour, ScriptBehaviour)

void SampleNativeBehaviour::OnStart()
{
    elapsedTime = 0.0f;
}

void SampleNativeBehaviour::OnUpdate(float32 deltaTime)
{
    elapsedTime += deltaTime * speed;
    Ens* ens = GetEns();
    TransformComponent* transform = ens ? ens->Transform() : nullptr;
    if (!transform) return;

    vector3 position = transform->GetLocalPosition();
    position.y = 1.0f + std::sin(elapsedTime) * 0.2f;
    transform->SetLocalPosition(position);
}

void SampleNativeBehaviour::OnLateUpdate(float32 deltaTime)
{
    (void)deltaTime;
}

void SampleNativeBehaviour::OnDrawGUI()
{
}

void SampleNativeBehaviour::OnEnd()
{
}
)ORB";

    constexpr const char* NativeGitIgnoreText = "Build/\n";

    constexpr const char* SampleBehaviourText = R"ORB(using System.Collections.Generic;
using Orbeden;

namespace {{PROJECT_NAME}};

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
        startPosition = Ens.Transform.localPosition;
        Console.WriteLine($"SampleBehaviour start: Ens({EnsId.id}, {EnsId.version})");
    }

    /// <summary>脚本每帧更新时调用。</summary>
    protected override void OnUpdate(float deltaTime)
    {
        totalTime += deltaTime;
        TransformComponent transform = Ens.Transform;
        vector3 position = startPosition;
        position.y += MathF.Sin(totalTime) * 0.25f;
        transform.localPosition = position;

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
using Orbeden;

namespace {{PROJECT_NAME}};

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
        baseScale = Ens.Transform.localScale;
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
            Ens.Transform.localScale = new vector3(baseScale.x * scale, baseScale.y * scale, baseScale.z * scale);
        }

        if (debugOffset.x != 0.0f || debugOffset.y != 0.0f || debugOffset.z != 0.0f)
        {
            vector3 position = Ens.Transform.localPosition;
            position.x += debugOffset.x * deltaTime;
            position.y += debugOffset.y * deltaTime;
            position.z += debugOffset.z * deltaTime;
            Ens.Transform.localPosition = position;
        }
    }

    /// <summary>脚本结束时调用。</summary>
    protected override void OnEnd()
    {
        Ens.Transform.localScale = baseScale;
        Console.WriteLine($"CubeTestBehaviour end: {label}");
    }
}
)ORB";

    constexpr const char* ScriptGitIgnoreText = "bin/\nobj/\n";
    constexpr const char* ManagedGitIgnoreText = "*\n!.gitignore\n";
    constexpr const char* WorldScriptsText = R"ORB({
  "scripts": [
    {
      "id": "sample-behaviour",
      "stableId": "world://{{PROJECT_NAME}}/main/cube",
      "type": "{{PROJECT_NAME}}.SampleBehaviour"
    },
    {
      "id": "cube-test-behaviour",
      "stableId": "world://{{PROJECT_NAME}}/main/cube",
      "type": "{{PROJECT_NAME}}.CubeTestBehaviour",
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
        return Utf8Path::ToUtf8(path.lexically_normal());
    }

    std::string ExpandTemplate(const char* text, const std::string& projectName)
    {
        constexpr const char* Token = "{{PROJECT_NAME}}";
        std::string expanded = text;
        std::size_t position = 0;
        while ((position = expanded.find(Token, position)) != std::string::npos)
        {
            expanded.replace(position, std::char_traits<char>::length(Token), projectName);
            position += projectName.size();
        }

        return expanded;
    }

    bool WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output)
        {
            Log::Error(("New project text file generation failed: " + ToCleanPath(path)).c_str());
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
            Log::Error(("New project orbshader generation failed: " + ToCleanPath(path)).c_str());
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
            Log::Error(("New project binary file generation failed: " + ToCleanPath(path)).c_str());
            return false;
        }

        output.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(size));
        return true;
    }

}

static bool GenerateWorldFile(const std::string& projectRoot, const std::string& projectName);

//生成新项目模板的资源、脚本和 World 文件
const char* NewProjectTemplate::GetAotExportsText()
{
    return AotExportsText;
}

bool NewProjectTemplate::GenerateProjectFiles(const std::string& projectRoot,
    const std::string& projectName,
    std::string& outError)
{
    outError.clear();
    std::filesystem::path root = Utf8Path::FromUtf8(projectRoot);
    std::string projectFileText = "<OrbedenProject version=\"1\" name=\"" + projectName
        + "\" startupWorld=\"World/main.world\" resourceRoot=\"Resource\" scriptRoot=\"Script\" managedRoot=\"Managed\" nativeRoot=\"Native\" />\n";

    //写入项目描述和资源目录。
    bool succeeded = true;
    succeeded = WriteTextFile(root / (projectName + ".oeproj"), projectFileText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Mesh/cube.obj", CubeObjText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Mesh/ground.obj", GroundObjText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Material/cube.mtl", CubeMtlText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Material/ground.mtl", GroundMtlText) && succeeded;

    //写入默认场景和引擎内置 Shader。
    succeeded = WriteOrbShaderFile(root / "Resource/Shader/blinn_phong_shadow.orbshader", BlinnPhongVertexShaderText, BlinnPhongFragmentShaderText) && succeeded;
    succeeded = WriteOrbShaderFile(root / "Resource/Shader/shadow_depth.orbshader", ShadowDepthVertexShaderText, ShadowDepthFragmentShaderText) && succeeded;
    succeeded = WriteOrbShaderFile(root / "Resource/Shader/skybox.orbshader", SkyboxVertexShaderText, SkyboxFragmentShaderText) && succeeded;
    succeeded = WriteOrbShaderFile(root / "Resource/Shader/rain_glass_refraction.orbshader", RefractionVertexShaderText, RainGlassFragmentShaderText) && succeeded;
    succeeded = WriteOrbShaderFile(root / "Resource/Shader/heat_wave_distortion.orbshader", RefractionVertexShaderText, HeatWaveFragmentShaderText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Shader/Builtin/shadow_common.orbinc", ShadowCommonText) && succeeded;
    succeeded = WriteTextFile(root / "Resource/Shader/Builtin/camera_texture_common.orbinc", CameraTextureCommonText) && succeeded;
    succeeded = WriteBinaryFile(root / "Resource/Texture/sky_blue.png", SkyBluePngBytes, sizeof(SkyBluePngBytes)) && succeeded;

    //写入 C# 默认脚本工程。
    succeeded = WriteTextFile(root / "Script" / (projectName + ".csproj"), ExpandTemplate(ScriptProjectTemplate, projectName)) && succeeded;
    succeeded = WriteTextFile(root / "Script/Directory.Build.props", DirectoryBuildPropsText) && succeeded;
    succeeded = WriteTextFile(root / "Script/OrbedenAotExports.cs", AotExportsText) && succeeded;
    succeeded = WriteTextFile(root / "Script/SampleBehaviour.cs", ExpandTemplate(SampleBehaviourText, projectName)) && succeeded;
    succeeded = WriteTextFile(root / "Script/CubeTestBehaviour.cs", ExpandTemplate(CubeTestBehaviourText, projectName)) && succeeded;
    succeeded = WriteTextFile(root / "Script/.gitignore", ScriptGitIgnoreText) && succeeded;
    succeeded = WriteTextFile(root / "Managed/.gitignore", ManagedGitIgnoreText) && succeeded;
    succeeded = WriteTextFile(root / "World/main.world.scripts.json", ExpandTemplate(WorldScriptsText, projectName)) && succeeded;

    //写入 C++ 游戏模块、示例原生脚本和 MetaGen 构建步骤。
    succeeded = WriteTextFile(root / "Native/CMakeLists.txt", ExpandTemplate(NativeCMakeTemplate, projectName)) && succeeded;
    succeeded = WriteTextFile(root / "Native/GameModule.cpp", ExpandTemplate(NativeModuleTemplate, projectName)) && succeeded;
    succeeded = WriteTextFile(root / "Native/SampleNativeBehaviour.h", SampleNativeBehaviourHeader) && succeeded;
    succeeded = WriteTextFile(root / "Native/SampleNativeBehaviour.cpp", SampleNativeBehaviourSource) && succeeded;
    succeeded = WriteTextFile(root / "Native/.gitignore", NativeGitIgnoreText) && succeeded;

    //写入默认 World。
    succeeded = GenerateWorldFile(projectRoot, projectName) && succeeded;
    if (succeeded)
    {
        Log::Info(("New project template generated: " + ToCleanPath(root)).c_str());
        return true;
    }

    outError = "Generate new project template failed: " + ToCleanPath(root);
    return false;
}

//生成默认 World 文件
static bool GenerateWorldFile(const std::string& projectRoot, const std::string& projectName)
{
    std::filesystem::path worldPath = Utf8Path::FromUtf8(projectRoot) / "World/main.world";
    if (worldPath.has_parent_path())
    {
        std::filesystem::create_directories(worldPath.parent_path());
    }

    std::ofstream output(worldPath, std::ios::out | std::ios::trunc);
    if (!output)
    {
        Log::Error(("New project world generation failed: " + ToCleanPath(worldPath)).c_str());
        return false;
    }

    std::string stableIdPrefix = "world://" + projectName + "/main/";

    output <<
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<World version=\"1\">\n"
        "    <Ens stableId=\"" << stableIdPrefix << "root\" name=\"SceneRoot\">\n"
        "        <Component type=\"TransformComponent\">\n"
        "            <Field name=\"localPosition\" type=\"vector3\" value=\"0 0 0\" />\n"
        "            <Field name=\"localRotation\" type=\"quaternion\" value=\"0 0 0 1\" />\n"
        "            <Field name=\"localScale\" type=\"vector3\" value=\"1 1 1\" />\n"
        "        </Component>\n"
        "        <Ens stableId=\"" << stableIdPrefix << "cube\" name=\"Cube\">\n"
        "            <Component type=\"TransformComponent\">\n"
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
        "            <Component type=\"SampleNativeBehaviour\">\n"
        "                <Field name=\"enabled\" type=\"bool\" value=\"true\" />\n"
        "                <Field name=\"speed\" type=\"float32\" value=\"2\" />\n"
        "            </Component>\n"
        "        </Ens>\n"
        "        <Ens stableId=\"" << stableIdPrefix << "ground\" name=\"Ground\">\n"
        "            <Component type=\"TransformComponent\">\n"
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
        "        <Ens stableId=\"" << stableIdPrefix << "directional_light\" name=\"DirectionalLight\">\n"
        "            <Component type=\"TransformComponent\">\n"
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
        "        <Ens stableId=\"" << stableIdPrefix << "camera\" name=\"Camera\">\n"
        "            <Component type=\"TransformComponent\">\n"
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

    Log::Info(("New project world generated: " + ToCleanPath(worldPath)).c_str());
    return true;
}
