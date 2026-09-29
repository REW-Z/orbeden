#include "Rendering/CascadedShadowMap.h"
#include "Rendering/ParticleRenderer.h"
#include "Rendering/StaticBatchCache.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Log/Log.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

void CascadedShadowMap::Initialize(RenderBackend* value)
{
    backend = value;
}

void CascadedShadowMap::Shutdown()
{
    if (backend)
    {
        for (const CameraHistory& history : histories) backend->DeleteDepthDistribution(history.query);
        backend->DeleteRenderTarget(target);
        backend->DeleteDepthTexture(atlas);
    }
    histories.clear();
    target = {};
    atlas = {};
    atlasWidth = atlasHeight = 0;
    ready = false;
    backend = nullptr;
}

void CascadedShadowMap::BeginFrame(const RenderScene& scene)
{
    ready = false;
    if (!backend) return;
    //移除已销毁相机的统计资源
    for (auto iterator = histories.begin(); iterator != histories.end();)
    {
        bool present = std::any_of(scene.cameras.begin(), scene.cameras.end(),
            [&](const RenderCamera& camera) { return camera.ens == iterator->camera.ens; });
        bool enabled = std::any_of(scene.directionalLights.begin(), scene.directionalLights.end(),
            [](const RenderDirectionalLight& light) { return light.castShadows; });
        if (!present || !enabled)
        {
            backend->DeleteDepthDistribution(iterator->query);
            iterator = histories.erase(iterator);
        }
        else ++iterator;
    }
}

bool CascadedShadowMap::CanReuseView(const RenderCamera& previous, const RenderCamera& current, float32 maxDistance)
{
    if (previous.ens != current.ens || previous.drawLayerMask != current.drawLayerMask ||
        previous.viewportWidth != current.viewportWidth || previous.viewportHeight != current.viewportHeight ||
        previous.nearPlane != current.nearPlane || previous.farPlane != current.farPlane ||
        std::memcmp(previous.projectionMatrix.m, current.projectionMatrix.m, sizeof(matrix4x4)) != 0) return false;
    vector3 offset{ previous.position.x-current.position.x, previous.position.y-current.position.y, previous.position.z-current.position.z };
    if (RenderMath::Dot(offset, offset) > maxDistance * maxDistance) return false;
    for (int32 axis : {0, 2})
    {
        vector3 a{previous.worldMatrix.m[axis*4],previous.worldMatrix.m[axis*4+1],previous.worldMatrix.m[axis*4+2]};
        vector3 b{current.worldMatrix.m[axis*4],current.worldMatrix.m[axis*4+1],current.worldMatrix.m[axis*4+2]};
        if (RenderMath::Dot(RenderMath::Normalize(a), RenderMath::Normalize(b)) < 0.95f) return false;
    }
    float32 elapsed = current.elapsedTime - previous.elapsedTime;
    return elapsed >= 0.0f && elapsed <= 1.0f;
}

bool CascadedShadowMap::CreateAtlas()
{
    int32 width = settings.resolution * 2;
    int32 height = settings.resolution * ((settings.count + 1) / 2);
    if (atlas.IsValid() && target.IsValid() && width == atlasWidth && height == atlasHeight) return true;
    backend->DeleteRenderTarget(target);
    backend->DeleteDepthTexture(atlas);
    target = {};
    atlas = {};
    atlasWidth = width;
    atlasHeight = height;
    GpuDepthTextureDesc texture;
    texture.width = width;
    texture.height = height;
    texture.floatingPoint = true;
    atlas = backend->CreateDepthTexture(texture);
    if (!atlas.IsValid()) return false;
    GpuRenderTargetDesc framebuffer;
    framebuffer.width = width;
    framebuffer.height = height;
    framebuffer.depthTexture = atlas;
    framebuffer.depthOnly = true;
    target = backend->CreateRenderTarget(framebuffer);
    if (!target.IsValid())
    {
        backend->DeleteDepthTexture(atlas);
        atlas = {};
        return false;
    }
    return true;
}

//记录一次阴影批次跳过，同一原因只报一次，避免每帧刷屏
void CascadedShadowMap::ReportShadowSkip(uint32 reason, const DrawBatch& batch, const char* detail)
{
    if ((reportedShadowSkips & reason) != 0) return;
    reportedShadowSkips |= reason;

    std::string message = "Shadow caster batch skipped: ";
    message += detail;
    message += ". mode=";
    message += std::to_string(static_cast<uint32>(batch.key.mode));
    message += " items=";
    message += std::to_string(batch.items.size());
    message += " indexStart=";
    message += std::to_string(batch.key.indexStart);
    message += " indexCount=";
    message += std::to_string(batch.key.indexCount);
    message += " program=";
    message += std::to_string(batch.key.programId);
    Log::Error(message.c_str());
}

void CascadedShadowMap::Render(const RenderScene& scene, const List<InstanceSubmission>& submissions,
    const ParticleFrameSnapshot& particles, const RenderCamera& camera, const RenderDirectionalLight& light,
    Shader* depthShader, GpuResourceManager& resources, ParticleRenderer& particleRenderer,
    StaticBatchCache* staticBatches, DrawBatchBuilder& builder, GpuDrawStream& stream, RenderBatchStats& stats)
{
    ready = false;
    staticBatchCache = staticBatches;
    if (!backend || !depthShader || !std::isfinite(camera.nearPlane) || !std::isfinite(camera.farPlane) ||
        camera.nearPlane <= 0.0f || camera.farPlane <= camera.nearPlane) return;
    settings.count = light.shadowCascadeCount;
    settings.resolution = light.shadowMapResolution;
    settings.distance = light.shadowDistance;
    settings.splitLambda = light.shadowSplitLambda;
    settings.blendRatio = light.shadowBlendRatio;
    settings.depthBias = light.shadowBias;
    settings.normalBias = light.shadowNormalBias;
    settings.adaptive = light.shadowAdaptive;
    settings.debugView = light.shadowDebugView;
    settings = ShadowCascadeBuilder::ValidateSettings(settings);
    if (settings.distance <= camera.nearPlane) return;
    const GpuShader* shader = resources.GetShader(depthShader);
    if (!shader || shader->passes.empty() || !CreateAtlas()) return;

    //建立相机独立分区历史
    auto found = std::find_if(histories.begin(), histories.end(),
        [&](const CameraHistory& history) { return history.camera.ens == camera.ens; });
    if (found == histories.end())
    {
        histories.emplace_back();
        found = histories.end()-1;
        found->camera = camera;
    }
    CameraHistory& history = *found;
    float32 maxDistance = std::max(2.0f, history.splits[0] * 0.25f);
    bool reset = !history.initialized || !CanReuseView(history.camera, camera, maxDistance) ||
        history.settings.count != settings.count || history.settings.distance != settings.distance ||
        history.settings.splitLambda != settings.splitLambda || history.settings.adaptive != settings.adaptive;
    if (reset)
    {
        history.hasDistribution = false;
        backend->DeleteDepthDistribution(history.query);
        history.query = {};
    }
    if (!settings.adaptive && history.query.IsValid())
    {
        backend->DeleteDepthDistribution(history.query);
        history.query = {};
        history.hasDistribution = false;
    }
    if (settings.adaptive)
    {
        if (!history.query.IsValid())
        {
            history.query = backend->CreateDepthDistribution();
            if (!history.query.IsValid()) Log::Error("SDSM histogram unavailable; stable CSM remains active.");
        }
        GpuDepthDistribution result;
        if (backend->TryReadDepthDistribution(history.query, result))
        {
            history.hasDistribution = CanReuseView(history.submittedCamera, camera, maxDistance);
            history.distribution = result;
            history.sampledCamera = history.submittedCamera;
        }
        if (history.hasDistribution && !CanReuseView(history.sampledCamera, camera, maxDistance))
            history.hasDistribution = false;
    }
    float32 splits[ShadowCascadeSettings::MaxCascades]{};
    ShadowCascadeBuilder::BuildSplits(settings, camera.nearPlane, camera.farPlane,
        history.hasDistribution ? &history.distribution : nullptr, reset ? nullptr : history.splits,
        camera.elapsedTime-history.camera.elapsedTime, splits);
    std::copy_n(splits, settings.count, history.splits);
    history.camera = camera;
    history.settings = settings;
    history.initialized = true;

    //收集全场景同层不透明投射物，用于分区包围盒与阴影候选
    List<StaticMeshRenderer*> casters;
    List<bounds3> bounds;
    for (StaticMeshRenderer* renderer : scene.renderers)
    {
        if (!renderer || !renderer->IsRenderSceneEligible() || !renderer->castShadows ||
            !(renderer->drawLayer & camera.drawLayerMask) ||
            !renderer->renderState.mesh) continue;
        //筛选包含不透明子网格材质的投影对象
        bool hasOpaqueMaterial = false;
        for (usize subIndex = 0; subIndex < renderer->renderState.mesh->subMeshes.size() && subIndex < renderer->materials.size(); ++subIndex)
        {
            Material* material = renderer->materials[subIndex].Get();
            if (material && material->GetDrawQueue() == DrawQueue::Opaque)
            {
                hasOpaqueMaterial = true;
                break;
            }
        }
        if (!hasOpaqueMaterial) continue;
        casters.push_back(renderer);
        bounds.push_back(renderer->renderState.worldBounds);
    }

    //绘制各级局部 atlas
    for (int32 index = 0; index < settings.count; ++index)
    {
        float32 sliceNear = index == 0 ? camera.nearPlane : cascades[index-1].blendStart;
        float32 logicalNear = index == 0 ? camera.nearPlane : splits[index-1];
        cascades[index] = ShadowCascadeBuilder::BuildCascade(camera.worldMatrix, camera.projectionMatrix,
            light.direction, sliceNear, splits[index], settings, bounds);
        cascades[index].blendStart = splits[index] - (splits[index]-logicalNear) * settings.blendRatio;

        //每级从完整候选独立剔除并独立成批，不复用主相机的可见结果
        builder.BuildShadowItems(scene, submissions, particles, camera, cascades[index].lightFrustum, shadowItems);
        builder.BuildShadowBatches(shadowItems, *shader, shadowBatches, stats);

        RenderPassDesc pass;
        pass.renderTarget = target;
        pass.x = (index % 2)*settings.resolution;
        pass.y = (index / 2)*settings.resolution;
        pass.width = pass.height = settings.resolution;
        pass.clearMode = ClearMode::DepthOnly;
        backend->BeginPass(pass);
        backend->SetDepthTest(true);
        backend->SetDepthCompare(DepthCompare::Less);
        backend->SetDepthWrite(true);
        backend->SetBlend(false);
        //深度 Pass 不使用加法混合，显式回到基线以免上一批留下加法方程
        backend->SetBlendMode(BlendMode::Alpha);
        backend->SetCullMode(CullMode::None);
        backend->SetPolygonOffset(false, 0.0f, 0.0f);

        //深度变体按批次模式切换，u_LightViewProjection 是逐 program 的 uniform，换 program 之后必须重设
        const GpuShaderPass& depthPass = shader->passes[0];
        auto bindDepthProgram = [&](const GpuShaderProgramID& program)
        {
            backend->BindShaderProgram(program);
            backend->SetUniformMatrix4("u_LightViewProjection", cascades[index].worldToShadow);
        };

        for (const DrawBatch& batch : shadowBatches)
        {
            //持久静态批：直接绑定缓存组，按级联裁剪出的索引区间绘制
            if (batch.persistentGeometry)
            {
                const StaticBatchGroup* group = staticBatchCache ? staticBatchCache->GetGroup(batch.staticGroup) : nullptr;
                if (!group || !group->vertexInput.IsValid() || !depthPass.expandedProgram.IsValid())
                {
                    ReportShadowSkip(ShadowSkipStaticGroup, batch, "the persistent static group or expanded depth program is invalid");
                    continue;
                }

                bindDepthProgram(depthPass.expandedProgram);
                backend->BindVertexInput(group->vertexInput);
                backend->DrawIndexed(batch.key.indexStart, batch.key.indexCount);
                ++stats.dynamicBatchDraws;
                ++stats.shadowDraws;
                continue;
            }

            if (batch.key.mode == GeometryMode::Instanced && depthPass.instancedProgram.IsValid())
            {
                //实例化阴影：同一深度程序，实例数据走流式缓冲
                //被拒绝的项已经计入 invalidTransforms，这里没有可画的实例就直接跳过
                stats.invalidTransforms += particleRenderer.BuildMeshInstances(batch, shadowItems, shadowInstances);
                if (shadowInstances.empty())
                {
                    ReportShadowSkip(ShadowSkipInstances, batch, "no instance survived");
                    continue;
                }

                const DrawItem& first = shadowItems[batch.items[0]];
                const GpuMesh* mesh = resources.GetMesh(first.mesh);
                if (!mesh || !mesh->instancedVertexInput.IsValid())
                {
                    ++stats.invalidResources;
                    ReportShadowSkip(ShadowSkipMesh, batch, "the instanced vertex input is invalid");
                    continue;
                }

                if (!stream.UploadMeshInstances(shadowInstances))
                {
                    ++stats.failedUploads;
                    ReportShadowSkip(ShadowSkipUpload, batch, "the instance upload was rejected");
                    continue;
                }

                bindDepthProgram(depthPass.instancedProgram);
                stats.uploadedBytes += static_cast<uint64>(shadowInstances.size()) * sizeof(GpuMeshInstance);
                stats.submittedInstances += shadowInstances.size();
                backend->BindVertexInput(mesh->instancedVertexInput);
                if (!backend->BindInstanceBuffer(stream.GetInstanceBuffer(), 0))
                {
                    ++stats.failedUploads;
                    ReportShadowSkip(ShadowSkipBind, batch, "the instance buffer bind was rejected");
                    continue;
                }

                backend->DrawIndexedInstanced(batch.key.indexStart, batch.key.indexCount,
                    static_cast<uint32>(shadowInstances.size()));
                ++stats.instancedDraws;
                ++stats.shadowDraws;
                continue;
            }

            if (batch.key.mode == GeometryMode::Expanded && depthPass.expandedProgram.IsValid())
            {
                //动态展开的粒子阴影：顶点已是世界空间，位置只乘光照矩阵
                particleRenderer.ExpandBatch(batch, shadowItems, expandedChunks, resources);
                if (expandedChunks.empty()) continue;

                bindDepthProgram(depthPass.expandedProgram);
                backend->BindVertexInput(stream.GetExpandedVertexInput());
                for (const ExpandedGeometryChunk& chunk : expandedChunks)
                {
                    if (chunk.vertices.empty() || chunk.indices.empty()) continue;
                    if (!stream.UploadExpanded(chunk.vertices, chunk.indices))
                    {
                        ++stats.failedUploads;
                        continue;
                    }

                    stats.uploadedBytes += static_cast<uint64>(chunk.vertices.size()) * sizeof(GpuExpandedVertex) +
                        static_cast<uint64>(chunk.indices.size()) * sizeof(uint32);
                    stats.expandedVertices += chunk.vertices.size();
                    stats.expandedIndices += chunk.indices.size();
                    backend->DrawIndexed(0, static_cast<uint32>(chunk.indices.size()));
                    ++stats.dynamicBatchDraws;
                    ++stats.shadowDraws;
                }
                continue;
            }

            //Uniform 与「展开变体被声明关闭的深度 Shader」都走逐对象绘制
            bindDepthProgram(depthPass.shaderProgram);
            for (uint32 itemIndex : batch.items)
            {
                const DrawItem& item = shadowItems[itemIndex];
                const GpuMesh* mesh = resources.GetMesh(item.mesh);
                if (!mesh)
                {
                    ++stats.invalidResources;
                    continue;
                }

                backend->SetUniformMatrix4("u_Model", item.model);
                backend->BindVertexInput(mesh->vertexInput);
                backend->DrawIndexed(item.indexStart, item.indexCount);
                ++stats.ordinaryDraws;
                ++stats.shadowDraws;
            }
        }

        backend->EndPass();
    }
    backend->BindVertexInput({});
    backend->BindShaderProgram({});
    ready = true;
}

void CascadedShadowMap::BindUniforms(const RenderCamera& camera)
{
    backend->SetUniformInt("u_ShadowCascadeCount", ready ? settings.count : 0);
    backend->SetUniformMatrix4("u_ShadowView", camera.viewMatrix);
    backend->SetUniformFloat("u_ShadowBias", settings.depthBias);
    backend->SetUniformFloat("u_ShadowNormalBias", settings.normalBias);
    backend->SetUniformInt("u_ShadowDebugView", ready ? settings.debugView : 0);
    for (int32 index = 0; ready && index < settings.count; ++index)
    {
        std::string suffix = "[" + std::to_string(index) + "]";
        backend->SetUniformMatrix4(("u_ShadowMatrices"+suffix).c_str(), cascades[index].worldToShadow);
        backend->SetUniformColor(("u_ShadowRects"+suffix).c_str(),
            {0.5f,static_cast<float32>(settings.resolution)/atlasHeight,
            static_cast<float32>(index%2)*0.5f,static_cast<float32>((index/2)*settings.resolution)/atlasHeight});
        backend->SetUniformColor(("u_ShadowParams"+suffix).c_str(),
            {cascades[index].splitDepth,cascades[index].blendStart,cascades[index].texelWorldSize,cascades[index].depthRange});
    }
}

//阴影调试视图是否真正生效（与 u_ShadowDebugView 的取值一致）
bool CascadedShadowMap::IsDebugViewActive() const
{
    return ready && settings.debugView != 0;
}

void CascadedShadowMap::BindTexture(uint32 slot)
{
    backend->SetUniformInt("u_ShadowMap", static_cast<int32>(slot));
    backend->SetUniformInt("u_UseShadowMap", ready ? 1 : 0);
    backend->BindDepthTexture(slot, ready ? atlas : GpuDepthTextureID{});
}

void CascadedShadowMap::CaptureDepth(const RenderCamera& camera)
{
    if (!ready || !settings.adaptive || !camera.cameraDepthTexture.IsValid()) return;
    auto found = std::find_if(histories.begin(), histories.end(),
        [&](const CameraHistory& history) { return history.camera.ens == camera.ens; });
    if (found == histories.end()) return;
    if (backend->SubmitDepthDistribution(found->query, camera.cameraDepthTexture,
        RenderMath::Inverse(camera.projectionMatrix), camera.nearPlane, std::min(camera.farPlane, settings.distance)))
        found->submittedCamera = camera;
}
