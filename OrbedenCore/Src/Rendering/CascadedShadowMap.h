#pragma once
#include "Rendering/ShadowCascadeBuilder.h"
#include "Rendering/RenderScene.h"
#include "Rendering/GpuResourceManager.h"

//管理每相机级联绘制和异步采样分区
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

public:
    //绑定渲染后端
    void Initialize(RenderBackend* value);
    //释放 atlas 与全部相机统计
    void Shutdown();
    //清理失效相机历史
    void BeginFrame(const RenderScene& scene);
    //生成当前相机级联阴影
    void Render(const RenderScene& scene, const RenderCamera& camera, const RenderDirectionalLight& light,
        Shader* depthShader, GpuResourceManager& resources);
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
    //判断统计是否适用于当前相机
    static bool CanReuseView(const RenderCamera& previous, const RenderCamera& current, float32 maxDistance);
    //创建指定尺寸的 atlas
    bool CreateAtlas();
};
