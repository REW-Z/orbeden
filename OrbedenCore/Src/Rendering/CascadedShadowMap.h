#pragma once
#include "Rendering/DrawBatchBuilder.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/InstanceDrawData.h"
#include "Rendering/ParticleRenderer.h"
#include "Rendering/RenderScene.h"
#include "Rendering/ShadowCascadeBuilder.h"

//管理每相机级联绘制和异步采样分区
class StaticBatchCache;

class CascadedShadowMap
{
    struct CameraHistory
    {
        RenderCamera camera;
        RenderCamera submittedCamera;
        RenderCamera sampledCamera;
        ShadowCascadeSettings settings;
        GpuDepthDistributionID query;
        GpuDepthDistribution distribution;
        float32 splits[ShadowCascadeSettings::MaxCascades] = {};
        bool initialized = false;
        bool hasDistribution = false;
    };
    RenderBackend* backend = nullptr;
    GpuDepthTextureID atlas;
    GpuRenderTargetID target;
    int32 atlasWidth = 0;
    int32 atlasHeight = 0;
    bool ready = false;
    ShadowCascadeSettings settings;
    ShadowCascade cascades[ShadowCascadeSettings::MaxCascades];
    List<CameraHistory> histories;
    //阴影候选与批次共用临时容器，稳态只清空不收缩
    List<DrawItem> shadowItems;
    List<DrawBatch> shadowBatches;
    List<GpuMeshInstance> shadowInstances;
    List<ExpandedGeometryChunk> expandedChunks;
    //本帧的静态批缓存，非拥有型；为空时静态成员仍逐对象投影
    StaticBatchCache* staticBatchCache = nullptr;

public:
    //绑定渲染后端
    void Initialize(RenderBackend* value);
    //释放 atlas 与全部相机统计
    void Shutdown();
    //清理失效相机历史
    void BeginFrame(const RenderScene& scene);
    //生成当前相机级联阴影。候选来自完整场景、全部显式实例与 Opaque Mesh 粒子，与主相机可见集合无关。
    void Render(const RenderScene& scene, const List<InstanceSubmission>& submissions,
        const ParticleFrameSnapshot& particles, const RenderCamera& camera,
        const RenderDirectionalLight& light, Shader* depthShader, GpuResourceManager& resources,
        ParticleRenderer& particleRenderer, StaticBatchCache* staticBatches, DrawBatchBuilder& builder,
        GpuDrawStream& stream, RenderBatchStats& stats);
    //绑定当前相机阴影查询参数
    void BindUniforms(const RenderCamera& camera);

    //阴影调试视图是否真正生效（与 u_ShadowDebugView 的取值一致）。
    //输出 Pass 据此跳过色调映射：调试色板写的是显示色，再映射一次就不是原样了。
    bool IsDebugViewActive() const;
    //绑定阴影 atlas
    void BindTexture(uint32 slot);
    //提交冻结相机深度统计
    void CaptureDepth(const RenderCamera& camera);

private:
    //阴影批次被跳过的原因位，同一原因只报一次，便于定位"某个物体没阴影"
    enum ShadowSkipReason : uint32
    {
        ShadowSkipInstances = 1u << 0,
        ShadowSkipMesh = 1u << 1,
        ShadowSkipUpload = 1u << 2,
        ShadowSkipBind = 1u << 3,
        ShadowSkipStaticGroup = 1u << 4,
    };
    uint32 reportedShadowSkips = 0;

    //记录一次阴影批次跳过，同一原因只报一次
    void ReportShadowSkip(uint32 reason, const DrawBatch& batch, const char* detail);

    //判断统计是否适用于当前相机
    static bool CanReuseView(const RenderCamera& previous, const RenderCamera& current, float32 maxDistance);
    //创建指定尺寸的 atlas
    bool CreateAtlas();
};
