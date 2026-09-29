#include "Rendering/AtmosphereRenderer.h"

#include "Log/Log.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/RenderMath.h"
#include "Rendering/RenderScene.h"
#include "ResourceManager/ResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    //内置 Shader 的固定资源 Key
    constexpr const char* TransmittanceShaderKey = "Builtin/Shaders/atmosphere_transmittance.orbshader";
    constexpr const char* AerialShaderKey = "Builtin/Shaders/atmosphere_aerial.orbshader";
    constexpr const char* SkyLutShaderKey = "Builtin/Shaders/atmosphere_sky_lut.orbshader";
    constexpr const char* SkyShaderKey = "Builtin/Shaders/atmosphere_sky.orbshader";

    //地球半径与大气顶高度，与 Shader 内的常数一致
    constexpr float64 EarthRadiusKm = 6371.0;
    //MOR 的 5% 透射阈值，浓雾能见度按它换算消光系数
    constexpr float64 VisibilityExtinctionFactor = 2.995732273553991;

    //大气要求的最低片元纹理槽数量：材质纹理之外还要放阴影、相机纹理、环境反射与两张图集
    constexpr uint32 MinimumFragmentTextureUnits = 16;

    //读取矩阵一列的前三个分量
    vector3 GetMatrixColumn(const matrix4x4& matrix, int32 column)
    {
        return { matrix.m[column * 4 + 0], matrix.m[column * 4 + 1], matrix.m[column * 4 + 2] };
    }
}

AtmosphereQualityDesc AtmosphereRenderer::GetQualityDesc(AtmosphereQuality quality)
{
    AtmosphereQualityDesc desc;
    if (quality == AtmosphereQuality::Balanced)
    {
        desc.transWidth = 256;
        desc.transHeight = 64;
        desc.aerialWidth = 48;
        desc.aerialHeight = 28;
        desc.aerialSlices = 24;
        desc.atlasColumns = 6;
        desc.skyWidth = 192;
        desc.skyHeight = 96;
        desc.viewSteps = 24;
        desc.skySteps = 48;
        return desc;
    }

    desc.transWidth = 128;
    desc.transHeight = 64;
    desc.aerialWidth = 24;
    desc.aerialHeight = 14;
    desc.aerialSlices = 16;
    desc.atlasColumns = 4;
    desc.skyWidth = 96;
    desc.skyHeight = 48;
    desc.viewSteps = 12;
    desc.skySteps = 24;
    return desc;
}

//记录一次资源错误，同一代资源只报一次
void AtmosphereRenderer::ReportResourceError(const std::string& message)
{
    if (reportedResourceError) return;

    reportedResourceError = true;
    Log::Error(message.c_str());
}

//记录一次非法设置，启用大气期间只报一次
void AtmosphereRenderer::ReportConfigError(const std::string& message)
{
    if (reportedConfigError) return;

    reportedConfigError = true;
    Log::Error(message.c_str());
}

void AtmosphereRenderer::Initialize(RenderBackend* renderBackend)
{
    if (backend == renderBackend) return;

    //换后端时旧资源的句柄已经失效，直接丢弃
    Shutdown();
    backend = renderBackend;
    quad.Initialize(backend);
    if (!backend) return;

    //材质纹理槽在关闭空气透视时要有一个有效句柄，1×1 零值即可
    const uint8 zeroPixel[4] = { 0, 0, 0, 0 };
    GpuTextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.channels = 4;
    desc.pixels = zeroPixel;
    neutralTexture = backend->CreateTexture(desc);
    if (!neutralTexture.IsValid()) Log::Error("Atmosphere neutral texture creation failed.");
}

void AtmosphereRenderer::Shutdown()
{
    InvalidateResources();
    if (backend && neutralTexture.IsValid()) backend->DeleteTexture(neutralTexture);

    neutralTexture = GpuTextureID();
    quad.Shutdown();
    backend = nullptr;
}

//释放查找表目标，保留四边形与占位纹理
void AtmosphereRenderer::ReleaseTargets()
{
    if (backend)
    {
        backend->DeleteRenderTarget(transmittanceTarget);
        backend->DeleteRenderTarget(aerialRadianceTarget);
        backend->DeleteRenderTarget(aerialOpticalDepthTarget);
        backend->DeleteRenderTarget(skyTarget);
    }

    transmittanceTarget = GpuRenderTargetID();
    aerialRadianceTarget = GpuRenderTargetID();
    aerialOpticalDepthTarget = GpuRenderTargetID();
    skyTarget = GpuRenderTargetID();
    transmittanceTexture = GpuTextureID();
    aerialRadianceTexture = GpuTextureID();
    aerialOpticalDepthTexture = GpuTextureID();
    skyTexture = GpuTextureID();
    targetsCreated = false;
    transmittanceValid = false;
    aerialValid = false;
    skyValid = false;
}

void AtmosphereRenderer::InvalidateResources()
{
    ReleaseTargets();
    transmittanceShader.Set(nullptr);
    aerialShader.Set(nullptr);
    skyLutShader.Set(nullptr);
    skyShader.Set(nullptr);
    shadersResolved = false;
    transmittancePass = GpuShaderProgramID();
    aerialPass = GpuShaderProgramID();
    skyLutPass = GpuShaderProgramID();
    skyPass = GpuShaderProgramID();
    cachedTransmittanceProgram = 0;
    cachedAerialProgram = 0;
    cachedSkyProgram = 0;
    aerialKey = AtmosphereFrame();
    skyKey = AtmosphereFrame();
    skyValid = false;
    transmittanceQuality = AtmosphereQuality::Low;
    transmittanceDensity = -1.0f;
    reportedResourceError = false;
    reportedConfigError = false;
    reportedFrameState = false;
}

//创建一张无深度颜色目标并取回颜色纹理
bool AtmosphereRenderer::CreateLutTarget(int32 width, int32 height, GpuRenderTargetID& target, GpuTextureID& texture)
{
    GpuRenderTargetDesc desc;
    desc.width = width;
    desc.height = height;
    desc.colorOnly = true;
    desc.linearColorFilter = true;
    desc.format = GpuRenderTargetFormat::RGBA16F;
    target = backend->CreateRenderTarget(desc);
    texture = backend->GetRenderTargetColorTexture(target);
    if (target.IsValid() && texture.IsValid()) return true;

    //半成品立刻回收，避免留下没有颜色纹理的目标
    backend->DeleteRenderTarget(target);
    target = GpuRenderTargetID();
    texture = GpuTextureID();
    return false;
}

bool AtmosphereRenderer::PrepareResources(GpuResourceManager& gpuResourceManager, const AtmosphereQualityDesc& quality,
    bool needFog, bool needSky)
{
    if (!backend || !quad.EnsureReady())
    {
        ReportResourceError("Atmosphere setup failed: the fullscreen quad is unavailable.");
        return false;
    }

    //能力门槛：片元纹理槽不足时两种大气功能整体停用，贴图天空盒仍可用
    if (backend->GetFragmentTextureUnitCount() < MinimumFragmentTextureUnits)
    {
        ReportResourceError("Atmosphere is disabled: the backend exposes fewer than 16 fragment texture units.");
        return false;
    }

    //按固定资源 Key 加载内置 Shader，每次资源失效后只解析一次
    if (!shadersResolved)
    {
        shadersResolved = true;
        transmittanceShader.Set(ResourceManager::Load<Shader>(TransmittanceShaderKey));
        aerialShader.Set(ResourceManager::Load<Shader>(AerialShaderKey));
        skyLutShader.Set(ResourceManager::Load<Shader>(SkyLutShaderKey));
        skyShader.Set(ResourceManager::Load<Shader>(SkyShaderKey));
    }

    //解析本帧需要的 program，缺少任何一项都不进入大气路径
    const GpuShader* transmittance = gpuResourceManager.GetShader(transmittanceShader.Get());
    if (!transmittance || transmittance->passes.empty() || !transmittance->passes[0].shaderProgram.IsValid())
    {
        ReportResourceError(std::string("Atmosphere shader is unavailable: ") + TransmittanceShaderKey);
        return false;
    }
    transmittancePass = transmittance->passes[0].shaderProgram;

    if (needFog)
    {
        const GpuShader* aerial = gpuResourceManager.GetShader(aerialShader.Get());
        if (!aerial || aerial->passes.empty() || !aerial->passes[0].shaderProgram.IsValid())
        {
            ReportResourceError(std::string("Atmosphere shader is unavailable: ") + AerialShaderKey);
            return false;
        }
        aerialPass = aerial->passes[0].shaderProgram;
    }

    if (needSky)
    {
        const GpuShader* skyLut = gpuResourceManager.GetShader(skyLutShader.Get());
        const GpuShader* sky = gpuResourceManager.GetShader(skyShader.Get());
        if (!skyLut || skyLut->passes.empty() || !skyLut->passes[0].shaderProgram.IsValid())
        {
            ReportResourceError(std::string("Atmosphere shader is unavailable: ") + SkyLutShaderKey);
            return false;
        }
        if (!sky || sky->passes.empty() || !sky->passes[0].shaderProgram.IsValid())
        {
            ReportResourceError(std::string("Atmosphere shader is unavailable: ") + SkyShaderKey);
            return false;
        }
        skyLutPass = skyLut->passes[0].shaderProgram;
        skyPass = sky->passes[0].shaderProgram;
    }

    //质量档位改变时先释放旧尺寸目标
    if (targetsCreated && createdQuality != frame.quality) ReleaseTargets();
    if (!targetsCreated)
    {
        createdQuality = frame.quality;
        targetsCreated = true;
    }

    //只创建缺失的目标，已分配的保留供再次启用
    const int32 atlasWidth = quality.aerialWidth * quality.atlasColumns;
    const int32 atlasHeight = quality.aerialHeight * (quality.aerialSlices / quality.atlasColumns);
    if (!transmittanceTarget.IsValid() &&
        !CreateLutTarget(quality.transWidth, quality.transHeight, transmittanceTarget, transmittanceTexture))
    {
        ReportResourceError("Atmosphere transmittance target creation failed.");
        return false;
    }
    if (needFog && !aerialRadianceTarget.IsValid() &&
        !CreateLutTarget(atlasWidth, atlasHeight, aerialRadianceTarget, aerialRadianceTexture))
    {
        ReportResourceError("Atmosphere aerial radiance target creation failed.");
        return false;
    }
    if (needFog && !aerialOpticalDepthTarget.IsValid() &&
        !CreateLutTarget(atlasWidth, atlasHeight, aerialOpticalDepthTarget, aerialOpticalDepthTexture))
    {
        ReportResourceError("Atmosphere aerial optical depth target creation failed.");
        return false;
    }
    if (needSky && !skyTarget.IsValid() &&
        !CreateLutTarget(quality.skyWidth, quality.skyHeight, skyTarget, skyTexture))
    {
        ReportResourceError("Atmosphere sky target creation failed.");
        return false;
    }

    return true;
}

void AtmosphereRenderer::BindLutUniforms()
{
    const AtmosphereQualityDesc quality = GetQualityDesc(frame.quality);
    const float32 atlasWidth = static_cast<float32>(quality.aerialWidth * quality.atlasColumns);
    const float32 atlasHeight = static_cast<float32>(quality.aerialHeight * (quality.aerialSlices / quality.atlasColumns));

    backend->SetUniformFloat("u_AtmosphereCameraHeightKm", frame.cameraHeightKm);
    backend->SetUniformVector3("u_AtmosphereCameraUp", frame.cameraUp);
    backend->SetUniformVector3("u_AtmosphereSunDirection", frame.sunDirection);
    backend->SetUniformColor("u_AtmosphereSunRadiance", frame.sunRadiance);
    backend->SetUniformColor("u_AtmosphereGroundBackground", frame.groundBackground);
    backend->SetUniformFloat("u_AtmosphereAerosolDensity", frame.settings.aerosolDensity);
    backend->SetUniformFloat("u_AtmosphereMaxDistanceKm", frame.maxDistanceKm);
    backend->SetUniformMatrix4("u_AtmosphereInverseProjection", frame.inverseProjection);
    backend->SetUniformMatrix4("u_AtmosphereCameraWorldRotation", frame.cameraWorldRotation);
    backend->SetUniformColor("u_AtmosphereAtlas",
        { static_cast<float32>(quality.aerialWidth), static_cast<float32>(quality.aerialHeight),
          static_cast<float32>(quality.aerialSlices), static_cast<float32>(quality.atlasColumns) });
    backend->SetUniformColor("u_AtmosphereTransSize",
        { static_cast<float32>(quality.transWidth), static_cast<float32>(quality.transHeight), 0.0f, 0.0f });
    backend->SetUniformColor("u_AtmosphereSkySize",
        { static_cast<float32>(quality.skyWidth), static_cast<float32>(quality.skyHeight), 0.0f, 0.0f });
}

bool AtmosphereRenderer::RenderLut(GpuRenderTargetID target, int32 width, int32 height,
    GpuShaderProgramID program, int32 stepCount, int32 outputKind)
{
    if (!backend || !target.IsValid() || !program.IsValid() || width <= 0 || height <= 0) return false;
    if (!quad.GetVertexInput().IsValid()) return false;

    RenderPassDesc passDesc;
    passDesc.x = 0;
    passDesc.y = 0;
    passDesc.width = width;
    passDesc.height = height;
    passDesc.renderTarget = target;
    passDesc.clearMode = ClearMode::ColorOnly;
    passDesc.clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    backend->BeginPass(passDesc);

    //查找表是线性数据，不需要深度测试、混合与剔除
    backend->SetDepthTest(false);
    backend->SetDepthWrite(false);
    backend->SetBlend(false);
    backend->SetCullMode(CullMode::None);
    backend->BindShaderProgram(program);
    backend->SetUniformInt("u_AtmosphereStepCount", stepCount);
    backend->SetUniformInt("u_AtmosphereOutputKind", outputKind);
    BindLutUniforms();

    //太阳透光率供图集与天空查询；生成它自己的那一趟不能采样自身
    if (target.id != transmittanceTarget.id && transmittanceTexture.IsValid())
    {
        backend->SetUniformInt("u_AtmosphereTransmittanceTexture", 0);
        backend->BindTexture(0, transmittanceTexture);
    }

    backend->BindVertexInput(quad.GetVertexInput());
    backend->DrawIndexed(0, 6);

    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->EndPass();
    return true;
}

bool AtmosphereRenderer::UpdateTransmittance(const AtmosphereQualityDesc& quality)
{
    if (!transmittancePass.IsValid() || !transmittanceTarget.IsValid()) return false;

    //键一致且查找表仍有效时跳过重建
    if (transmittanceValid && cachedTransmittanceProgram == transmittancePass.id &&
        transmittanceQuality == frame.quality && transmittanceDensity == frame.settings.aerosolDensity)
    {
        return true;
    }

    if (!RenderLut(transmittanceTarget, quality.transWidth, quality.transHeight, transmittancePass, 64, 1))
    {
        return false;
    }

    transmittanceValid = true;
    cachedTransmittanceProgram = transmittancePass.id;
    transmittanceQuality = frame.quality;
    transmittanceDensity = frame.settings.aerosolDensity;
    return true;
}

bool AtmosphereRenderer::UpdateAerial(const AtmosphereQualityDesc& quality)
{
    if (!aerialPass.IsValid() || !aerialRadianceTarget.IsValid() || !aerialOpticalDepthTarget.IsValid()) return false;

    //视线、太阳与参数全部不变时复用上一帧的图集
    if (aerialValid && cachedAerialProgram == aerialPass.id && MatchesFrame(aerialKey, frame)) return true;

    const int32 atlasWidth = quality.aerialWidth * quality.atlasColumns;
    const int32 atlasHeight = quality.aerialHeight * (quality.aerialSlices / quality.atlasColumns);
    if (!RenderLut(aerialRadianceTarget, atlasWidth, atlasHeight, aerialPass, quality.viewSteps, 0)) return false;
    if (!RenderLut(aerialOpticalDepthTarget, atlasWidth, atlasHeight, aerialPass, quality.viewSteps, 1)) return false;

    aerialValid = true;
    cachedAerialProgram = aerialPass.id;
    aerialKey = frame;
    return true;
}

bool AtmosphereRenderer::UpdateSky(const AtmosphereQualityDesc& quality)
{
    if (!skyLutPass.IsValid() || !skyTarget.IsValid()) return false;

    //天空查找表与图集各自记录缓存键，两者可以单独重建
    if (skyValid && cachedSkyProgram == skyLutPass.id && MatchesFrame(skyKey, frame)) return true;

    if (!RenderLut(skyTarget, quality.skyWidth, quality.skyHeight, skyLutPass, quality.skySteps, 0)) return false;

    skyValid = true;
    cachedSkyProgram = skyLutPass.id;
    skyKey = frame;
    return true;
}

bool AtmosphereRenderer::MatchesFrame(const AtmosphereFrame& current, const AtmosphereFrame& other)
{
    return current.fogActive == other.fogActive
        && current.skyActive == other.skyActive
        && current.quality == other.quality
        && current.cameraHeightKm == other.cameraHeightKm
        && current.maxDistanceKm == other.maxDistanceKm
        && current.cameraUp.x == other.cameraUp.x && current.cameraUp.y == other.cameraUp.y && current.cameraUp.z == other.cameraUp.z
        && current.sunDirection.x == other.sunDirection.x && current.sunDirection.y == other.sunDirection.y && current.sunDirection.z == other.sunDirection.z
        && current.sunRadiance.r == other.sunRadiance.r && current.sunRadiance.g == other.sunRadiance.g
        && current.sunRadiance.b == other.sunRadiance.b && current.sunRadiance.a == other.sunRadiance.a
        && std::equal(std::begin(current.inverseProjection.m), std::end(current.inverseProjection.m), std::begin(other.inverseProjection.m))
        && std::equal(std::begin(current.cameraWorldRotation.m), std::end(current.cameraWorldRotation.m), std::begin(other.cameraWorldRotation.m))
        && current.viewportWidth == other.viewportWidth
        && current.viewportHeight == other.viewportHeight
        && current.groundBackground.r == other.groundBackground.r && current.groundBackground.g == other.groundBackground.g
        && current.groundBackground.b == other.groundBackground.b && current.groundBackground.a == other.groundBackground.a
        && current.settings.fogEnabled == other.settings.fogEnabled
        && current.settings.quality == other.settings.quality
        && current.settings.planetCenterX == other.settings.planetCenterX
        && current.settings.planetCenterY == other.settings.planetCenterY
        && current.settings.planetCenterZ == other.settings.planetCenterZ
        && current.settings.metersPerWorldUnit == other.settings.metersPerWorldUnit
        && current.settings.aerosolDensity == other.settings.aerosolDensity
        && current.settings.sunRadianceScale == other.settings.sunRadianceScale
        && current.denseFogExtinction == other.denseFogExtinction
        && current.denseFogScattering.r == other.denseFogScattering.r
        && current.denseFogScattering.g == other.denseFogScattering.g
        && current.denseFogScattering.b == other.denseFogScattering.b
        && current.denseFogScattering.a == other.denseFogScattering.a
        && current.denseFogProfile.r == other.denseFogProfile.r
        && current.denseFogProfile.g == other.denseFogProfile.g
        && current.denseFogProfile.b == other.denseFogProfile.b
        && current.denseFogProfile.a == other.denseFogProfile.a
        && current.denseFogTopFadeKm == other.denseFogTopFadeKm
        && current.denseFogDebugView == other.denseFogDebugView;
}

bool AtmosphereRenderer::BuildFrame(const RenderScene& scene, const RenderCamera& camera,
    const RenderDirectionalLight* mainLight, AtmosphereFrame& outFrame)
{
    outFrame = AtmosphereFrame();

    const RenderSettings& settings = scene.renderSettings;
    const AtmosphereSettings& atmosphere = settings.atmosphere;
    const uint32 skyMode = static_cast<uint32>(settings.skyMode);
    const uint32 quality = static_cast<uint32>(atmosphere.quality);

    //非法设置停用本相机大气
    const bool valid = std::isfinite(atmosphere.metersPerWorldUnit) && atmosphere.metersPerWorldUnit > 0.0f
        && std::isfinite(atmosphere.aerosolDensity) && std::isfinite(atmosphere.sunRadianceScale)
        && std::isfinite(atmosphere.planetCenterX) && std::isfinite(atmosphere.planetCenterY)
        && std::isfinite(atmosphere.planetCenterZ) && skyMode <= 1u && quality <= 1u
        && std::isfinite(atmosphere.denseFog.visibilityMeters) && atmosphere.denseFog.visibilityMeters > 0.0f
        && std::isfinite(atmosphere.denseFog.referenceHeightMeters) && std::isfinite(atmosphere.denseFog.layerHeightMeters)
        && std::isfinite(atmosphere.denseFog.fadeMeters) && std::isfinite(atmosphere.denseFog.scatteringScale)
        && std::isfinite(atmosphere.denseFog.sunScatteringScale)
        && std::isfinite(atmosphere.denseFog.topFadeMeters) && atmosphere.denseFog.topFadeMeters >= 0.0f
        && atmosphere.denseFog.debugView >= 0 && atmosphere.denseFog.debugView <= 7;
    if (!valid)
    {
        ReportConfigError("Atmosphere settings are invalid; the camera falls back to the skybox path.");
        return false;
    }

    outFrame.settings = atmosphere;
    outFrame.quality = atmosphere.quality;
    outFrame.fogActive = atmosphere.fogEnabled;
    outFrame.skyActive = settings.skyboxEnabled && settings.skyMode == SkyMode::Atmosphere
        && camera.clearMode == ClearMode::SolidColor;

    //阴影调试视图下两种大气功能都不参与绘制
    if (mainLight && mainLight->shadowDebugView != 0)
    {
        outFrame.fogActive = false;
        outFrame.skyActive = false;
    }
    if (!outFrame.fogActive && !outFrame.skyActive) return true;

    //相机位置换算到米，再求高度与向上方向
    const float64 metersPerWorldUnit = static_cast<float64>(atmosphere.metersPerWorldUnit);
    const float64 positionX = static_cast<float64>(camera.position.x) * metersPerWorldUnit - atmosphere.planetCenterX;
    const float64 positionY = static_cast<float64>(camera.position.y) * metersPerWorldUnit - atmosphere.planetCenterY;
    const float64 positionZ = static_cast<float64>(camera.position.z) * metersPerWorldUnit - atmosphere.planetCenterZ;
    const float64 radius = std::sqrt(positionX * positionX + positionY * positionY + positionZ * positionZ);
    if (!(radius > 0.0))
    {
        ReportConfigError("Atmosphere camera position is degenerate; the camera falls back to the skybox path.");
        return false;
    }

    //相机可以在大气层外：保留真实高度，由着色器求入射与出射，不按高度停用
    //半径为零或落到地心以下属于无效位置，不是低海拔地形
    const float64 heightKm = radius / 1000.0 - EarthRadiusKm;
    if (heightKm <= -EarthRadiusKm)
    {
        ReportConfigError("Atmosphere camera position is invalid: the camera is at or below the planet center.");
        return false;
    }
    outFrame.cameraHeightKm = static_cast<float32>(heightKm);
    outFrame.cameraUp = { static_cast<float32>(positionX / radius), static_cast<float32>(positionY / radius),
        static_cast<float32>(positionZ / radius) };

    //只接受透视投影：正交投影的 w 行不含 -z 项
    if (std::fabs(camera.projectionMatrix.m[11] + 1.0f) > 1.0e-4f)
    {
        ReportConfigError("Atmosphere requires a perspective camera; the camera falls back to the skybox path.");
        return false;
    }
    outFrame.inverseProjection = RenderMath::Inverse(camera.projectionMatrix);

    //取相机世界矩阵的归一化旋转基，平移置零
    matrix4x4 rotation = camera.worldMatrix;
    rotation.m[12] = 0.0f;
    rotation.m[13] = 0.0f;
    rotation.m[14] = 0.0f;
    for (int32 column = 0; column < 3; ++column)
    {
        vector3 basis = GetMatrixColumn(rotation, column);
        const float32 length = std::sqrt(basis.x * basis.x + basis.y * basis.y + basis.z * basis.z);
        if (!(length > 1.0e-6f))
        {
            ReportConfigError("Atmosphere camera basis is degenerate; the camera falls back to the skybox path.");
            return false;
        }

        basis.x /= length;
        basis.y /= length;
        basis.z /= length;
        rotation.m[column * 4 + 0] = basis.x;
        rotation.m[column * 4 + 1] = basis.y;
        rotation.m[column * 4 + 2] = basis.z;
    }
    outFrame.cameraWorldRotation = rotation;

    outFrame.viewportWidth = camera.viewportWidth;
    outFrame.viewportHeight = camera.viewportHeight;
    outFrame.groundBackground = camera.clearColor;
    outFrame.maxDistanceKm = std::clamp(camera.farPlane * atmosphere.metersPerWorldUnit / 1000.0f, 1.0f, 2500.0f);

    //太阳方向取主光传播方向的反向，无主光时散射亮度为零
    if (mainLight)
    {
        const vector3 sunDirection = RenderMath::Normalize(
            { -mainLight->direction.x, -mainLight->direction.y, -mainLight->direction.z });
        const float32 length = std::sqrt(sunDirection.x * sunDirection.x + sunDirection.y * sunDirection.y +
            sunDirection.z * sunDirection.z);
        outFrame.sunDirection = length > 0.5f ? sunDirection : vector3{ 0.0f, -1.0f, 0.0f };
        const float32 scale = std::max(mainLight->intensity, 0.0f) * atmosphere.sunRadianceScale;
        outFrame.sunRadiance = { mainLight->color.r * scale, mainLight->color.g * scale,
            mainLight->color.b * scale, 1.0f };
    }
    else
    {
        outFrame.sunDirection = { 0.0f, -1.0f, 0.0f };
        outFrame.sunRadiance = { 0.0f, 0.0f, 0.0f, 1.0f };
    }

    //总开关同时管薄霾与浓雾，浓雾再有自己的开关
    const DenseFogSettings& denseFog = atmosphere.denseFog;
    if (outFrame.fogActive && denseFog.enabled)
    {
        //MOR 5% 阈值换算消光系数：sigmaT = -ln(0.05) / 能见度
        outFrame.denseFogExtinction = static_cast<float32>(VisibilityExtinctionFactor) /
            (denseFog.visibilityMeters * 0.001f);

        //散射亮度取环境光与主光之和：只有环境光时浓雾会随环境光变暗甚至全黑，
        //而实际浓雾的亮度主要由被多次散射的太阳光决定。两项都是美术照明量，夜间都会归零。
        const color& ambient = scene.renderSettings.ambientColor;
        float32 scatteringR = ambient.r * denseFog.scatteringScale;
        float32 scatteringG = ambient.g * denseFog.scatteringScale;
        float32 scatteringB = ambient.b * denseFog.scatteringScale;
        if (mainLight)
        {
            const float32 sun = std::max(mainLight->intensity, 0.0f) * denseFog.sunScatteringScale;
            scatteringR += mainLight->color.r * sun;
            scatteringG += mainLight->color.g * sun;
            scatteringB += mainLight->color.b * sun;
        }
        outFrame.denseFogScattering = { scatteringR, scatteringG, scatteringB, 1.0f };

        //提交底面过渡与雾顶衰减尺度，衰减尺度不超过层厚
        const float32 layerKm = std::max(denseFog.layerHeightMeters, 0.0f) * 0.001f;
        outFrame.denseFogProfile = { denseFog.referenceHeightMeters * 0.001f, layerKm,
            std::max(denseFog.fadeMeters, 0.0f) * 0.001f, static_cast<float32>(EarthRadiusKm) };
        outFrame.denseFogTopFadeKm = std::clamp(std::max(denseFog.topFadeMeters, 0.0f) * 0.001f, 0.0f, layerKm);
        outFrame.denseFogDebugView = denseFog.debugView;
    }

    return true;
}

bool AtmosphereRenderer::PrepareCamera(const RenderScene& scene, const RenderCamera& camera,
    const RenderDirectionalLight* mainLight, GpuResourceManager& gpuResourceManager)
{
    frame = AtmosphereFrame();
    if (!backend) return false;

    AtmosphereFrame built;
    if (!BuildFrame(scene, camera, mainLight, built)) return false;
    if (!built.fogActive && !built.skyActive) return false;

    frame = built;
    const AtmosphereQualityDesc quality = GetQualityDesc(frame.quality);

    //首次激活时打印一次本帧实际生效的参数，方便核对设置是否送到渲染器
    if (!reportedFrameState && frame.fogActive)
    {
        reportedFrameState = true;
        char line[512];
        snprintf(line, sizeof(line),
            "Atmosphere frame: fog=%d sky=%d height=%.3f km maxDistance=%.1f km "
            "denseFog=%d extinction=%.1f/km profile=(base=%.4f top=%.4f baseFade=%.4f) topFade=%.4f km debug=%d",
            frame.fogActive ? 1 : 0, frame.skyActive ? 1 : 0, frame.cameraHeightKm, frame.maxDistanceKm,
            frame.denseFogExtinction > 0.0f ? 1 : 0, frame.denseFogExtinction,
            frame.denseFogProfile.r, frame.denseFogProfile.r + frame.denseFogProfile.g,
            frame.denseFogProfile.b, frame.denseFogTopFadeKm, frame.denseFogDebugView);
        Log::Info(line);
    }

    //透光率服务于图集、天空与太阳盘，只要有一项启用就要更新
    const bool ready = PrepareResources(gpuResourceManager, quality, frame.fogActive, frame.skyActive)
        && UpdateTransmittance(quality)
        && (!frame.fogActive || UpdateAerial(quality))
        && (!frame.skyActive || UpdateSky(quality));
    if (ready) return true;

    //任何一步失败都整体停用本相机大气，背景退回原天空盒路径
    frame = AtmosphereFrame();
    return false;
}

void AtmosphereRenderer::BindMaterialUniforms()
{
    if (!backend) return;

    const AtmosphereQualityDesc quality = GetQualityDesc(frame.quality);
    backend->SetUniformInt("u_AtmosphereFogEnabled", frame.fogActive ? 1 : 0);
    backend->SetUniformFloat("u_AtmosphereMaxDistanceKm", frame.maxDistanceKm);
    backend->SetUniformFloat("u_AtmosphereMetersPerWorldUnit", frame.settings.metersPerWorldUnit);
    backend->SetUniformFloat("u_AtmosphereCameraHeightKm", frame.cameraHeightKm);
    backend->SetUniformVector3("u_AtmosphereCameraUp", frame.cameraUp);
    backend->SetUniformInt("u_AtmosphereDenseFogEnabled", frame.denseFogExtinction > 0.0f ? 1 : 0);
    backend->SetUniformFloat("u_AtmosphereDenseFogExtinction", frame.denseFogExtinction);
    backend->SetUniformColor("u_AtmosphereDenseFogScattering", frame.denseFogScattering);
    backend->SetUniformColor("u_AtmosphereDenseFogProfile", frame.denseFogProfile);
    backend->SetUniformFloat("u_AtmosphereDenseFogTopFade", frame.denseFogTopFadeKm);
    backend->SetUniformInt("u_AtmosphereFogDebugView", frame.denseFogDebugView);
    backend->SetUniformColor("u_AtmosphereViewport",
        { static_cast<float32>(frame.viewportWidth), static_cast<float32>(frame.viewportHeight), 0.0f, 0.0f });
    backend->SetUniformColor("u_AtmosphereAtlas",
        { static_cast<float32>(quality.aerialWidth), static_cast<float32>(quality.aerialHeight),
          static_cast<float32>(quality.aerialSlices), static_cast<float32>(quality.atlasColumns) });
}

void AtmosphereRenderer::BindMaterialTextures(uint32 firstSlot)
{
    if (!backend) return;

    //关闭空气透视时绑定零占位，材质分支只走动态判断不做查询
    const GpuTextureID radiance = frame.fogActive && aerialRadianceTexture.IsValid()
        ? aerialRadianceTexture : neutralTexture;
    const GpuTextureID opticalDepth = frame.fogActive && aerialOpticalDepthTexture.IsValid()
        ? aerialOpticalDepthTexture : neutralTexture;

    backend->SetUniformInt("u_AtmosphereRadianceTexture", static_cast<int32>(firstSlot));
    backend->BindTexture(firstSlot, radiance);
    backend->SetUniformInt("u_AtmosphereOpticalDepthTexture", static_cast<int32>(firstSlot + 1));
    backend->BindTexture(firstSlot + 1, opticalDepth);
}

bool AtmosphereRenderer::RenderSky()
{
    if (!backend || !frame.skyActive || !skyPass.IsValid()) return false;
    if (!skyTexture.IsValid() || !transmittanceTexture.IsValid()) return false;

    const AtmosphereQualityDesc quality = GetQualityDesc(frame.quality);

    //天空在主 Pass 内绘制，不写深度也不参与深度测试
    backend->SetDepthTest(false);
    backend->SetDepthWrite(false);
    backend->SetBlend(false);
    backend->BindShaderProgram(skyPass);
    backend->SetUniformFloat("u_AtmosphereAerosolDensity", frame.settings.aerosolDensity);
    backend->SetUniformInt("u_AtmosphereStepCount", quality.skySteps);
    backend->SetUniformMatrix4("u_AtmosphereInverseProjection", frame.inverseProjection);
    backend->SetUniformMatrix4("u_AtmosphereCameraWorldRotation", frame.cameraWorldRotation);
    backend->SetUniformFloat("u_AtmosphereCameraHeightKm", frame.cameraHeightKm);
    backend->SetUniformVector3("u_AtmosphereCameraUp", frame.cameraUp);
    backend->SetUniformVector3("u_AtmosphereSunDirection", frame.sunDirection);
    backend->SetUniformColor("u_AtmosphereSunRadiance", frame.sunRadiance);
    backend->SetUniformColor("u_AtmosphereSkySize",
        { static_cast<float32>(quality.skyWidth), static_cast<float32>(quality.skyHeight), 0.0f, 0.0f });
    backend->SetUniformColor("u_AtmosphereTransSize",
        { static_cast<float32>(quality.transWidth), static_cast<float32>(quality.transHeight), 0.0f, 0.0f });
    backend->SetUniformInt("u_AtmosphereDenseFogEnabled", frame.denseFogExtinction > 0.0f ? 1 : 0);
    backend->SetUniformFloat("u_AtmosphereDenseFogExtinction", frame.denseFogExtinction);
    backend->SetUniformColor("u_AtmosphereDenseFogScattering", frame.denseFogScattering);
    backend->SetUniformColor("u_AtmosphereDenseFogProfile", frame.denseFogProfile);
    backend->SetUniformFloat("u_AtmosphereDenseFogTopFade", frame.denseFogTopFadeKm);
    backend->SetUniformInt("u_AtmosphereFogDebugView", frame.denseFogDebugView);
    //槽 0 是天空查找表，槽 1 是太阳盘查询需要的透光率
    backend->SetUniformInt("u_AtmosphereSkyTexture", 0);
    backend->BindTexture(0, skyTexture);
    backend->SetUniformInt("u_AtmosphereTransmittanceTexture", 1);
    backend->BindTexture(1, transmittanceTexture);
    backend->BindVertexInput(quad.GetVertexInput());
    backend->DrawIndexed(0, 6);

    //恢复主 Pass 基线
    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->SetDepthWrite(true);
    backend->SetDepthTest(true);
    return true;
}
