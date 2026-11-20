#pragma once
#include "Rendering/DrawSubmissionBuilder.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/InstanceDrawData.h"
#include "Rendering/ParticleRenderer.h"
#include "Rendering/RenderScene.h"
#include "Rendering/ShadowCascadeBuilder.h"

//管理每相机的手动分区级联阴影
class StaticBatchCache;

class CascadedShadowMap
{
    RenderBackend* backend = nullptr;
    GpuDepthTextureID atlas;
    GpuRenderTargetID target;
    int32 atlasWidth = 0;
    int32 atlasHeight = 0;
    bool ready = false;
    ShadowCascadeSettings settings;
    ShadowCascade cascades[MaximumShadowCascades];
    //当前相机的共享候选与投射物包围盒，逐次重建并复用容量
    List<DrawItem> shadowCandidates;
    List<bounds3> casterBounds;
    //当前级联的绘制项与批次，稳态只清空不收缩
    List<DrawItem> shadowItems;
    List<DrawBatch> shadowBatches;
    List<GpuMeshInstance> shadowInstances;
    List<ExpandedGeometryChunk> expandedChunks;
    //本帧的静态批缓存，非拥有型；为空时静态成员仍逐对象投影
    StaticBatchCache* staticBatchCache = nullptr;

public:
    //绑定渲染后端
    void Initialize(RenderBackend* value);
    //释放阴影图集
    void Shutdown();
    //重置本帧阴影状态
    void BeginFrame();
    //生成当前相机级联阴影。候选来自完整场景、全部显式实例与 Opaque Mesh 粒子，与主相机可见集合无关。
    void Render(const RenderScene& scene, std::span<const InstanceSubmission* const> batchedSubmissions,
        std::span<const InstanceSubmission* const> directSubmissions,
        const ParticleFrameSnapshot& particles, const RenderCamera& camera,
        const RenderDirectionalLight& light, Shader* depthShader, GpuResourceManager& resources,
        ParticleRenderer& particleRenderer, StaticBatchCache* staticBatches, DrawSubmissionBuilder& builder,
        GpuDrawStream& stream, RenderBatchStats& stats);
    //绑定当前相机阴影查询参数
    void BindUniforms(const RenderCamera& camera);

    //绑定阴影 atlas
    void BindTexture(int32 slot);

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

    //创建指定尺寸的 atlas
    bool CreateAtlas(int32 capacity);
};
