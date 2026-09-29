#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/FullscreenQuad.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/RenderSettings.h"

#include <string>

class GpuResourceManager;
struct RenderCamera;
struct RenderDirectionalLight;
class RenderScene;

//大气质量档位对应的查找表尺寸与积分步数
struct AtmosphereQualityDesc
{
public:
    int32 transWidth = 0;
    int32 transHeight = 0;
    int32 aerialWidth = 0;
    int32 aerialHeight = 0;
    int32 aerialSlices = 0;
    int32 atlasColumns = 0;
    int32 skyWidth = 0;
    int32 skyHeight = 0;
    int32 viewSteps = 0;
    int32 skySteps = 0;
};

//单个相机本帧的大气输入，同时充当查找表重建的缓存键
struct AtmosphereFrame
{
public:
    bool fogActive = false;
    bool skyActive = false;
    AtmosphereQuality quality = AtmosphereQuality::Low;
    float32 cameraHeightKm = 0.0f;
    float32 maxDistanceKm = 0.0f;
    vector3 cameraUp;
    vector3 sunDirection;
    color sunRadiance;
    matrix4x4 inverseProjection;
    matrix4x4 cameraWorldRotation;
    int32 viewportWidth = 0;
    int32 viewportHeight = 0;
    //未加载远地形的颜色底板；命中地球的射线由天空着色器按像素解析合成，不烘焙进查找表
    color groundBackground;
    AtmosphereSettings settings;

    //浓雾：消光系数（每 km）、散射亮度、剖面（雾顶高度、预留、预留、参考球半径，单位 km）
    float32 denseFogExtinction = 0.0f;
    //雾顶内部渐变厚度，单位 km
    float32 denseFogTopFadeKm = 0.0f;
    color denseFogScattering;
    color denseFogProfile;
    //浓雾调试视图，0 关闭
    int32 denseFogDebugView = 0;
};

//球形大气的全部 GPU 工作：太阳透光率查找表、每相机空气透视图集、程序化天空。
//每个 ForwardPipeline 只保留一套资源，顺序服务所有相机，资源数量不随相机数量增长。
class AtmosphereRenderer
{
private:
    RenderBackend* backend = nullptr;
    FullscreenQuad quad;

    //内置 Shader，资源失效后重新加载
    Ref<Shader> transmittanceShader;
    Ref<Shader> aerialShader;
    Ref<Shader> skyLutShader;
    Ref<Shader> skyShader;
    bool shadersResolved = false;

    //当前资源的 program，随资源失效重新解析
    GpuShaderProgramID transmittancePass;
    GpuShaderProgramID aerialPass;
    GpuShaderProgramID skyLutPass;
    GpuShaderProgramID skyPass;

    //四张查找表目标；颜色纹理随目标释放，句柄在创建时取一次
    GpuRenderTargetID transmittanceTarget;
    GpuRenderTargetID aerialRadianceTarget;
    GpuRenderTargetID aerialOpticalDepthTarget;
    GpuRenderTargetID skyTarget;
    GpuTextureID transmittanceTexture;
    GpuTextureID aerialRadianceTexture;
    GpuTextureID aerialOpticalDepthTexture;
    GpuTextureID skyTexture;

    //关闭空气透视时给材质槽占位的 1×1 零纹理
    GpuTextureID neutralTexture;

    //目标创建时使用的质量档位，档位改变时先释放旧尺寸目标
    AtmosphereQuality createdQuality = AtmosphereQuality::Low;
    bool targetsCreated = false;

    //本帧的相机数据
    AtmosphereFrame frame;

    //透光率缓存键：质量、气溶胶密度与重建时使用的 program
    bool transmittanceValid = false;
    AtmosphereQuality transmittanceQuality = AtmosphereQuality::Low;
    float32 transmittanceDensity = -1.0f;
    uint32 cachedTransmittanceProgram = 0;

    //视图缓存键：上次重建图集与天空时的帧数据与 program
    bool aerialValid = false;
    bool skyValid = false;
    AtmosphereFrame aerialKey;
    AtmosphereFrame skyKey;
    uint32 cachedAerialProgram = 0;
    uint32 cachedSkyProgram = 0;

    //资源与配置错误每代资源只报一次
    bool reportedResourceError = false;
    bool reportedConfigError = false;
    //本帧参数只打印一次，用于核对设置是否真的送到渲染器
    bool reportedFrameState = false;

    //按质量档位取尺寸与步数
    static AtmosphereQualityDesc GetQualityDesc(AtmosphereQuality quality);

    //计算本相机本帧的大气输入
    bool BuildFrame(const RenderScene& scene, const RenderCamera& camera,
        const RenderDirectionalLight* mainLight, AtmosphereFrame& outFrame);

    //按需加载 Shader、解析 program 并创建查找表目标
    bool PrepareResources(GpuResourceManager& gpuResourceManager, const AtmosphereQualityDesc& quality,
        bool needFog, bool needSky);

    //创建一张无深度颜色目标并取回颜色纹理
    bool CreateLutTarget(int32 width, int32 height, GpuRenderTargetID& target, GpuTextureID& texture);

    //重建太阳透光率查找表
    bool UpdateTransmittance(const AtmosphereQualityDesc& quality);

    //重建空气透视图集
    bool UpdateAerial(const AtmosphereQualityDesc& quality);

    //重建天空查找表
    bool UpdateSky(const AtmosphereQualityDesc& quality);

    //两份帧数据是否逐字段一致，禁止对含 padding 的结构直接比较内存
    static bool MatchesFrame(const AtmosphereFrame& current, const AtmosphereFrame& other);

    //绘制一张查找表，outputKind 为 0 输出散射亮度、为 1 输出光学厚度
    bool RenderLut(GpuRenderTargetID target, int32 width, int32 height, GpuShaderProgramID program,
        int32 stepCount, int32 outputKind);

    //提交查找表与天空绘制使用的大气参数
    void BindLutUniforms();

    //释放全部查找表目标
    void ReleaseTargets();

    //记录一次资源错误
    void ReportResourceError(const std::string& message);

    //记录一次非法设置
    void ReportConfigError(const std::string& message);

public:
    //绑定后端并创建占位纹理，不提交绘制
    void Initialize(RenderBackend* renderBackend);

    //释放全部 GPU 资源并清空后端引用
    void Shutdown();

    //内容根切换后释放可重新加载的资源，保留四边形与占位纹理
    void InvalidateResources();

    //在主 Pass 之前准备本相机需要的查找表
    bool PrepareCamera(const RenderScene& scene, const RenderCamera& camera,
        const RenderDirectionalLight* mainLight, GpuResourceManager& gpuResourceManager);

    //在当前 Pass 中绘制程序化天空，返回是否成功替代原天空盒
    bool RenderSky();

    //向当前程序提交材质的空气透视参数
    void BindMaterialUniforms();

    //绑定材质的空气透视纹理槽
    void BindMaterialTextures(uint32 firstSlot);

    //本帧是否应用空气透视
    bool IsFogActive() const { return frame.fogActive; }

    //本帧是否使用程序化天空
    bool IsSkyActive() const { return frame.skyActive; }

    //本帧查找表覆盖的最大距离
    float32 GetMaxDistanceKm() const { return frame.maxDistanceKm; }
};
