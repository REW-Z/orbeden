#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/CascadedShadowMap.h"
#include "Rendering/DrawBatchBuilder.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/ParticleRenderer.h"
#include "Rendering/RenderScene.h"

#include <unordered_set>

//Forward 渲染管线，负责组织阴影、天空盒和场景几何的绘制顺序。
class ForwardPipeline
{
private:
    //后端接口，用于创建资源并提交绘制命令。
    RenderBackend* backend = nullptr;

    //管线内置 shader，分别用于阴影深度和天空盒绘制。
    Ref<Shader> shadowDepthShader;
    Ref<Shader> skyboxShader;

    CascadedShadowMap shadows;
    GpuEnvironmentReflection environmentReflection;

    //内置天空盒立方体网格。
    GpuMesh skyboxMesh;

    //内置 shader 是否需要从当前内容根目录重新加载。
    bool builtinShadersInvalidated = true;

    //可绘制项合并与批次构建，按相机重用临时容器。
    DrawBatchBuilder batchBuilder;
    //实例与展开几何的流式绘制缓冲。
    GpuDrawStream drawStream;
    bool drawStreamReady = false;

    //当前相机的统一绘制项与批次。
    List<DrawItem> cameraItems;
    List<DrawBatch> cameraBatches;

    //本帧批次统计，由 RenderSystem 在每帧开始时清零。
    RenderBatchStats batchStats;

    //本帧生效的显式实例提交，只在一次 Render 内有效，由 RenderSystem 注入。
    const List<InstanceSubmission>* drawSubmissions = nullptr;
    //本帧用于刷新粒子快照的世界，由 RenderSystem 注入。
    World* frameWorld = nullptr;

    //实例数据构建的复用容器，稳态只清空不收缩。
    List<GpuMeshInstance> instanceScratch;
    List<GpuTrailInstance> trailInstanceScratch;
    List<ExpandedGeometryChunk> expandedChunks;

    //粒子几何的来源，快照每帧刷新一次，与本帧所有相机共用。
    ParticleRenderer particleRenderer;
    ParticleFrameSnapshot particleSnapshot;
    bool particleRendererReady = false;

    //刷新粒子渲染快照，runtime 与 preview 二选一。
    void CaptureParticleFrame(World& world);

public:
    //初始化后端引用并等待首次绘制时加载内置 shader。
    void Initialize(RenderBackend* renderBackend);

    //释放资源相关状态并保留当前渲染后端。
    void InvalidateResourceCaches();

    //释放管线持有的所有内置 GPU 资源。
    void Shutdown();

    //加载内置资源并清理相机阴影历史。
    void PrepareFrame(const RenderScene& scene, GpuResourceManager& gpuResourceManager);

    //按照既定 pass 顺序渲染指定相机的可见集合。
    void Render(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& gpuResourceManager);

    //阴影调试视图是否生效，供输出 Pass 决定要不要跳过色调映射。
    bool IsShadowDebugViewActive() const { return shadows.IsDebugViewActive(); }

    //清零本帧批次统计，每帧渲染开始调用。
    void ResetBatchStats() { batchStats = RenderBatchStats(); }

    //读取本帧批次统计。
    const RenderBatchStats& GetBatchStats() const { return batchStats; }

    //供级联阴影累加同一份统计。
    RenderBatchStats& MutableBatchStats() { return batchStats; }

    //注入本帧生效的显式实例提交；传空表示没有提交。
    void SetDrawSubmissions(const List<InstanceSubmission>* submissions) { drawSubmissions = submissions; }

    //注入本帧的世界，用于刷新粒子渲染快照。
    void SetFrameWorld(World* world) { frameWorld = world; }

    //读取本帧生效的显式实例提交。
    const List<InstanceSubmission>* GetDrawSubmissions() const { return drawSubmissions; }

    //实例与展开几何的流式绘制缓冲，供级联阴影共用。
    GpuDrawStream& GetDrawStream() { return drawStream; }

    //按需创建流式绘制缓冲。
    bool PrepareDrawStream();

private:
    //从当前内容根目录加载管线内置 shader。
    void LoadBuiltinShaders();

    //检查后端状态，并确保天空盒 cube mesh 已创建。
    bool PrepareSkyboxMesh();

    //把当前相机的可见项合并为统一的绘制项与批次。
    void BuildFrameBatches(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& gpuResourceManager);

    //按指定队列执行已经排好序的批次。
    void ExecuteQueueBatches(const RenderScene& scene, const RenderCamera& camera, GpuResourceManager& gpuResourceManager,
        DrawQueue drawQueue, const RenderDirectionalLight* mainLight, bool cameraTexturesReady);

    //执行单个批次，按几何模式分派。
    void ExecuteBatch(const DrawBatch& batch, const RenderScene& scene, const RenderCamera& camera,
        GpuResourceManager& gpuResourceManager, const RenderDirectionalLight* mainLight, bool cameraTexturesReady,
        std::unordered_set<uint32>& configuredPrograms);

    //以普通单绘制执行批次，多 Pass 来源在这里逐对象跑完全部 Pass。
    bool ExecuteUniformBatch(const DrawBatch& batch, const RenderScene& scene, const RenderCamera& camera,
        GpuResourceManager& gpuResourceManager, const RenderDirectionalLight* mainLight, bool cameraTexturesReady,
        const GpuMaterial& material, std::unordered_set<uint32>& configuredPrograms);

    //以实例绘制执行批次。
    bool ExecuteInstancedBatch(const DrawBatch& batch, const RenderScene& scene, const RenderCamera& camera,
        GpuResourceManager& gpuResourceManager, const RenderDirectionalLight* mainLight, bool cameraTexturesReady,
        const GpuMaterial& material, std::unordered_set<uint32>& configuredPrograms);

    //以展开几何执行批次，按 chunk 依次上传并绘制。
    bool ExecuteExpandedBatch(const DrawBatch& batch, const RenderScene& scene, const RenderCamera& camera,
        GpuResourceManager& gpuResourceManager, const RenderDirectionalLight* mainLight, bool cameraTexturesReady,
        const GpuMaterial& material, std::unordered_set<uint32>& configuredPrograms);

    //以拖尾实例执行批次，共享四边形的 InstancedTrail 顶点输入。
    bool ExecuteTrailInstancedBatch(const DrawBatch& batch, const RenderScene& scene, const RenderCamera& camera,
        GpuResourceManager& gpuResourceManager, const RenderDirectionalLight* mainLight, bool cameraTexturesReady,
        const GpuMaterial& material, std::unordered_set<uint32>& configuredPrograms);

    //绑定一个批次的固定功能状态、公共 uniform 与材质参数。
    void BindDrawState(const DrawBatch& batch, const RenderScene& scene, const RenderCamera& camera,
        const RenderDirectionalLight* mainLight, bool cameraTexturesReady, const GpuMaterial& material, uint32 programId,
        std::unordered_set<uint32>& configuredPrograms);

    //在当前相机的 color pass 中绘制全局天空盒。
    void RenderSkybox(const RenderScene& scene, const RenderCamera& camera, GpuResourceManager& gpuResourceManager);
};
