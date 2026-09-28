#include "Rendering/ForwardPipeline.h"

#include "Log/Log.h"
#include "FileSystem/PathDefines.h"
#include "Profiler/Profiler.h"
#include "FileSystem/Utf8Path.h"
#include "Rendering/ParticleRenderer.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/Camera.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/CookedAssetSerializer.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/Particles/ParticleSimulationSystem.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace
{
    //内置 Shader 按文件名在内容根内查找：内容根的目录结构完全自由，不能假定固定路径。
    constexpr const char* ShadowDepthShaderFileName = "shadow_depth.orbshader";
    constexpr const char* SkyboxShaderFileName = "skybox.orbshader";

    //解析结果缓存。内容根变化时随 InvalidateResourceCaches 一起作废。
    struct BuiltinShaderKeys
    {
    public:
        std::string shadowDepth;
        std::string skybox;
    };

    BuiltinShaderKeys& GetBuiltinShaderKeys()
    {
        static BuiltinShaderKeys keys;
        return keys;
    }

    //在内容根内按文件名查找资源 Key；找不到返回空串。
    //打包目录内只有产物与清单、没有源文件，因此存在清单时按清单匹配。
    std::string FindContentKeyByFileName(const std::string& fileName)
    {
        if (!PathDefines::HasContentRoot()) return std::string();

        List<std::string> matches;
        List<std::string> cookedKeys;
        if (CookedAssetSerializer::ReadIndex(cookedKeys))
        {
            for (const std::string& key : cookedKeys)
            {
                if (Utf8Path::ToUtf8(Utf8Path::FromUtf8(key).filename()) != fileName) continue;

                matches.push_back(key);
            }
        }
        else
        {
            //未打包的内容根按文件名递归扫描，目录结构完全自由。
            std::filesystem::path root = Utf8Path::FromUtf8(PathDefines::GetContentRoot());
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator iterator(root, error), end; !error && iterator != end; iterator.increment(error))
            {
                const std::filesystem::directory_entry& entry = *iterator;
                if (!entry.is_regular_file()) continue;
                if (Utf8Path::ToUtf8(entry.path().filename()) != fileName) continue;

                matches.push_back(Utf8Path::ToUtf8(entry.path().lexically_relative(root)));
            }
        }

        if (matches.empty()) return std::string();

        //同名多个时取字典序第一个，保证结果稳定。
        std::sort(matches.begin(), matches.end());
        if (matches.size() > 1)
        {
            Log::Warning(("Multiple files named '" + fileName + "' were found in the content root; using " + matches.front()).c_str());
        }

        return ResourceManager::ToResourceKey(matches.front());
    }

    //获取内置 Shader 的 Key，首次解析后缓存
    const std::string& ResolveBuiltinShaderKey(const char* fileName, std::string& cachedKey)
    {
        if (cachedKey.empty()) cachedKey = FindContentKeyByFileName(fileName);
        return cachedKey;
    }

    //获取内置 Shader
    Shader* GetOrLoadBuiltinShader(Ref<Shader>& shader, const std::string& key)
    {
        //读取已缓存的 Shader
        Shader* result = shader.Get();
        if (result || !shader.GetInstanceId().IsValid()) return result;

        //重新加载内置 Shader
        if (key.empty()) return nullptr;

        result = ResourceManager::Load<Shader>(key);
        shader.Set(result);
        return result;
    }

    //转换 Pass 三态开关
    bool ConvertPassToggleToBool(ShaderPassToggle value, bool baseline)
    {
        if (value == ShaderPassToggle::On) return true;
        if (value == ShaderPassToggle::Off) return false;
        return baseline;
    }

    //转换 Pass 剔除模式
    CullMode ConvertCullModeForBackend(CullMode value)
    {
        return value == CullMode::Auto ? CullMode::None : value;
    }

    //查找主方向光
    const RenderDirectionalLight* FindMainLight(const RenderScene& scene)
    {
        return scene.directionalLights.empty() ? nullptr : &scene.directionalLights[0];
    }

    //查找阴影方向光
    const RenderDirectionalLight* FindShadowLight(const RenderScene& scene)
    {
        for (const RenderDirectionalLight& light : scene.directionalLights)
        {
            if (light.castShadows) return &light;
        }

        return nullptr;
    }

    //释放天空盒 GPU 网格
    void DeleteGpuMesh(RenderBackend* backend, GpuMesh& mesh)
    {
        if (!backend) return;

        backend->DeleteVertexInput(mesh.vertexInput);
        backend->DeleteVertexInput(mesh.instancedVertexInput);
        backend->DeleteVertexBuffer(mesh.vertexBuffer);
        backend->DeleteIndexBuffer(mesh.indexBuffer);
        mesh = GpuMesh();
    }

}

void ForwardPipeline::Initialize(RenderBackend* renderBackend)
{
    //绑定渲染后端
    backend = renderBackend;
    shadows.Initialize(backend);
    builtinShadersInvalidated = true;
}

void ForwardPipeline::InvalidateResourceCaches()
{
    //释放管线 GPU 资源
    if (backend)
    {
        shadows.Shutdown();
        shadows.Initialize(backend);
        DeleteGpuMesh(backend, skyboxMesh);
        drawStream.Shutdown();
    }

    //重置管线资源状态
    staticBatches.Shutdown();
    staticBatchesReady = false;
    particleRenderer.Shutdown();
    particleRendererReady = false;
    particleSnapshot = ParticleFrameSnapshot();
    //Shader 重新导入后同一个材质可能已经补上变体，允许再报一次
    reportedMissingVariants.clear();
    expandedChunks.clear();
    shadowDepthShader.Set(nullptr);
    skyboxShader.Set(nullptr);
    environmentReflection = {};
    builtinShadersInvalidated = true;
    drawStreamReady = false;
    cameraItems.clear();
    cameraBatches.clear();
    //内容根可能已经换了，内置 Shader 的解析结果作废。
    GetBuiltinShaderKeys() = BuiltinShaderKeys();
}

void ForwardPipeline::Shutdown()
{
    InvalidateResourceCaches();
    shadows.Shutdown();
    backend = nullptr;
}

void ForwardPipeline::PrepareFrame(const RenderScene& scene, GpuResourceManager& gpuResourceManager)
{
    if (!backend) return;
    LoadBuiltinShaders();
    if (staticBatchesReady == false)
    {
        staticBatches.Initialize(backend);
        staticBatchesReady = true;
    }
    if (!particleRendererReady)
    {
        particleRenderer.Initialize(backend);
        particleRendererReady = true;
    }

    batchBuilder.Initialize(&gpuResourceManager, backend->SupportsInstancing(), &particleRenderer, &staticBatches);
    shadows.BeginFrame(scene);

    //静态几何缓存每帧刷新一次：收集候选、按内容版本重建受影响的组
    if (frameWorld) staticBatches.Refresh(*frameWorld, gpuResourceManager);

    //粒子快照每帧只抓一次，本帧所有相机共用
    if (frameWorld) CaptureParticleFrame(*frameWorld);
    else particleSnapshot = ParticleFrameSnapshot();
    //选择全局反射来源并准备本帧采样数据
    const RenderSettings& settings = scene.renderSettings;
    Skybox* source = settings.reflectionEnvironment.GetInstanceId().IsValid()
        ? settings.reflectionEnvironment.Get() : settings.skybox.Get();
    environmentReflection = gpuResourceManager.GetEnvironmentReflection(source, settings.reflectionIntensity);
}

void ForwardPipeline::Render(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& gpuResourceManager)
{
    if (!backend) return;

    //选择相机和主方向光
    const RenderCamera& camera = visibleSet.camera;
    const RenderDirectionalLight* shadowLight = FindShadowLight(scene);
    const RenderDirectionalLight* mainLight = shadowLight ? shadowLight : FindMainLight(scene);

    //生成当前相机的级联阴影
    if (shadowLight)
    {
        //阴影与主 Pass 共用同一份流式缓冲，必须在阴影之前就绪
        PrepareDrawStream();
        Shader* depthShader = GetOrLoadBuiltinShader(shadowDepthShader,
            ResolveBuiltinShaderKey(ShadowDepthShaderFileName, GetBuiltinShaderKeys().shadowDepth));
        static const List<InstanceSubmission> NoSubmissions;
        shadows.Render(scene, drawSubmissions ? *drawSubmissions : NoSubmissions, particleSnapshot, camera,
            *shadowLight, depthShader, gpuResourceManager, particleRenderer, &staticBatches, batchBuilder,
            drawStream, batchStats);
    }

    //开始相机主 Pass。
    //目标是引擎分配的场景缓冲，尺寸就等于视口且原点在 0，视口原点只属于最终输出目标。
    RenderPassDesc passDesc;
    passDesc.x = 0;
    passDesc.y = 0;
    passDesc.width = camera.viewportWidth;
    passDesc.height = camera.viewportHeight;
    passDesc.renderTarget = camera.renderTarget;
    passDesc.clearMode = camera.clearMode;
    passDesc.clearColor = camera.clearColor;
    backend->BeginPass(passDesc);
    backend->SetDepthTest(true);
    backend->SetDepthWrite(true);
    backend->SetBlend(false);
    backend->SetCullMode(CullMode::None);

    //绘制 [天空盒]
    if (camera.clearMode == ClearMode::SolidColor)
    {
        RenderSkybox(scene, camera, gpuResourceManager);
    }

    //合并本相机的可见项为统一绘制项与批次
    BuildFrameBatches(scene, visibleSet, gpuResourceManager);

    //绘制 [不透明队列]
    ExecuteQueueBatches(scene, camera, gpuResourceManager, DrawQueue::Opaque, mainLight, false);

    //绘制 [普通透明队列]
    ExecuteQueueBatches(scene, camera, gpuResourceManager, DrawQueue::Transparent, mainLight, false);

    //复制相机颜色和深度纹理
    bool cameraTexturesReady = false;
    if (camera.cameraTextureTarget.IsValid() && camera.cameraColorTexture.IsValid() && camera.cameraDepthTexture.IsValid())
    {
        GpuRenderTargetCopyDesc copyDesc;
        copyDesc.sourceRenderTarget = camera.renderTarget;
        copyDesc.destinationRenderTarget = camera.cameraTextureTarget;
        //源与目标都是视口尺寸、原点为 0 的场景缓冲
        copyDesc.sourceX = 0;
        copyDesc.sourceY = 0;
        copyDesc.width = camera.viewportWidth;
        copyDesc.height = camera.viewportHeight;
        cameraTexturesReady = backend->CopyRenderTarget(copyDesc);
    }

    //绘制 [折射队列]
    ExecuteQueueBatches(scene, camera, gpuResourceManager, DrawQueue::Refraction, mainLight, cameraTexturesReady);

    //结束相机主 Pass
    backend->SetBlend(false);
    backend->SetDepthWrite(true);
    backend->SetDepthTest(true);
    backend->SetCullMode(CullMode::None);
    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->EndPass();
    if (cameraTexturesReady) shadows.CaptureDepth(camera);
}

//把当前相机的可见项合并为统一的绘制项与批次
void ForwardPipeline::BuildFrameBatches(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& gpuResourceManager)
{
    PROFILE("Render/BuildBatches");
    static const List<InstanceSubmission> EmptySubmissions;
    batchBuilder.BuildCameraItems(scene, visibleSet, drawSubmissions ? *drawSubmissions : EmptySubmissions,
        particleSnapshot, cameraItems);
    batchBuilder.SortItems(cameraItems);
    batchBuilder.BuildBatches(cameraItems, cameraBatches, batchStats);
    batchBuilder.Clear();
}

//按指定队列执行已经排好序的批次
void ForwardPipeline::ExecuteQueueBatches(const RenderScene& scene, const RenderCamera& camera,
    GpuResourceManager& gpuResourceManager, DrawQueue drawQueue, const RenderDirectionalLight* mainLight,
    bool cameraTexturesReady)
{
    PROFILE("Render/DrawBatches");
    std::unordered_set<uint32> configuredPrograms;
    for (const DrawBatch& batch : cameraBatches)
    {
        if (batch.key.queue != drawQueue) continue;
        ExecuteBatch(batch, scene, camera, gpuResourceManager, mainLight, cameraTexturesReady, configuredPrograms);
    }
}

//执行单个批次，按几何模式分派到实例或普通绘制
void ForwardPipeline::ExecuteBatch(const DrawBatch& batch, const RenderScene& scene, const RenderCamera& camera,
    GpuResourceManager& gpuResourceManager, const RenderDirectionalLight* mainLight, bool cameraTexturesReady,
    std::unordered_set<uint32>& configuredPrograms)
{
    if (batch.items.empty()) return;

    const GpuMaterial* material = gpuResourceManager.GetMaterial(cameraItems[batch.items[0]].material);
    if (!material || !material->shader || material->shader->passes.empty())
    {
        ++batchStats.invalidResources;
        return;
    }

    if (batch.key.mode == GeometryMode::Instanced)
    {
        ExecuteInstancedBatch(batch, scene, camera, gpuResourceManager, mainLight, cameraTexturesReady, *material, configuredPrograms);
        return;
    }

    if (batch.key.mode == GeometryMode::Expanded)
    {
        ExecuteExpandedBatch(batch, scene, camera, gpuResourceManager, mainLight, cameraTexturesReady, *material, configuredPrograms);
        return;
    }

    if (batch.key.mode == GeometryMode::TrailInstanced)
    {
        ExecuteTrailInstancedBatch(batch, scene, camera, gpuResourceManager, mainLight, cameraTexturesReady, *material, configuredPrograms);
        return;
    }

    ExecuteUniformBatch(batch, scene, camera, gpuResourceManager, mainLight, cameraTexturesReady, *material, configuredPrograms);
}

//记录一次因材质缺少几何变体而跳过的粒子绘制
void ForwardPipeline::ReportMissingGeometryVariant(Material* source, const GpuShader& shader, const char* variant)
{
    //同一材质只报一次：Billboard 每颗粒子自成一个批次，不去重会每帧刷屏
    int32 key = source ? source->GetObjectId() : 0;
    if (!reportedMissingVariants.insert(key).second) return;

    std::string message = "Particle draw skipped: material '";
    message += source ? source->name : std::string("<none>");
    message += "' uses shader '";
    message += shader.source ? shader.source->name : std::string("<none>");
    message += "' without the ";
    message += variant;
    message += " geometry variant. Re-import the shader after fixing the declaration: "
        "expandedGeometry off disables the expanded path, and trails require the Particle contract.";
    Log::Error(message.c_str());
}

//刷新粒子渲染快照
void ForwardPipeline::CaptureParticleFrame(World& world)
{
    particleRenderer.ClearCameraScratch();

    ParticleSimulationSystem* simulation = ParticleSimulationSystem::Current();
    if (!simulation)
    {
        particleSnapshot = ParticleFrameSnapshot();
        return;
    }

    //编辑态预览优先：只要还有预览根，相机就该看预览那一份。
    //这里判「有没有内容」而不是「有没有在推进」，否则暂停的下一帧会切回空的 runtime 上下文，画面整片消失。
    //Play 期间例外：预览那一路在 Play 时不推进，照画就是停在空中的一堆粒子，所以这段必须看 runtime
    bool usePreview = simulation->HasPreviewContent() && !world.IsRuntimeActive();
    const ParticleSimulationContext& context = simulation->GetRenderContext(usePreview);
    particleRenderer.CaptureFrame(world, context, simulation->GetTransformCache(), particleSnapshot);
}

//以展开几何执行批次
bool ForwardPipeline::ExecuteExpandedBatch(const DrawBatch& batch, const RenderScene& scene, const RenderCamera& camera,
    GpuResourceManager& gpuResourceManager, const RenderDirectionalLight* mainLight, bool cameraTexturesReady,
    const GpuMaterial& material, std::unordered_set<uint32>& configuredPrograms)
{
    const GpuShaderPass& shaderPass = material.shader->passes[0];
    if (!shaderPass.expandedProgram.IsValid())
    {
        ++batchStats.failedUploads;
        ReportMissingGeometryVariant(cameraItems[batch.items[0]].material, *material.shader, "expanded");
        return false;
    }

    //持久静态批直接绑定缓存组的顶点输入，不重新展开也不上传
    if (batch.persistentGeometry)
    {
        const StaticBatchGroup* group = staticBatches.GetGroup(batch.staticGroup);
        if (!group || !group->vertexInput.IsValid())
        {
            ++batchStats.invalidResources;
            return false;
        }

        BindDrawState(batch, scene, camera, mainLight, cameraTexturesReady, material,
            shaderPass.expandedProgram.id, configuredPrograms);
        backend->BindShaderProgram(shaderPass.expandedProgram);
        backend->BindVertexInput(group->vertexInput);
        backend->DrawIndexed(batch.key.indexStart, batch.key.indexCount);
        ++batchStats.dynamicBatchDraws;
        return true;
    }

    if (!PrepareDrawStream()) return false;

    {
        PROFILE("Render/ExpandParticles");
        particleRenderer.ExpandBatch(batch, cameraItems, expandedChunks, gpuResourceManager);
    }
    if (expandedChunks.empty()) return false;

    BindDrawState(batch, scene, camera, mainLight, cameraTexturesReady, material,
        shaderPass.expandedProgram.id, configuredPrograms);
    backend->BindShaderProgram(shaderPass.expandedProgram);
    backend->BindVertexInput(drawStream.GetExpandedVertexInput());

    bool drew = false;
    for (const ExpandedGeometryChunk& chunk : expandedChunks)
    {
        if (chunk.vertices.empty() || chunk.indices.empty()) continue;
        {
            PROFILE("Render/UploadInstances");
            if (!drawStream.UploadExpanded(chunk.vertices, chunk.indices))
            {
                ++batchStats.failedUploads;
                Log::Error("ForwardPipeline expanded draw skipped: the geometry upload was rejected.");
                continue;
            }
        }

        batchStats.uploadedBytes += static_cast<uint64>(chunk.vertices.size()) * sizeof(GpuExpandedVertex) +
            static_cast<uint64>(chunk.indices.size()) * sizeof(uint32);
        batchStats.expandedVertices += chunk.vertices.size();
        batchStats.expandedIndices += chunk.indices.size();
        backend->DrawIndexed(0, static_cast<uint32>(chunk.indices.size()));
        ++batchStats.dynamicBatchDraws;
        drew = true;
    }

    return drew;
}

//以拖尾实例执行批次
bool ForwardPipeline::ExecuteTrailInstancedBatch(const DrawBatch& batch, const RenderScene& scene, const RenderCamera& camera,
    GpuResourceManager& gpuResourceManager, const RenderDirectionalLight* mainLight, bool cameraTexturesReady,
    const GpuMaterial& material, std::unordered_set<uint32>& configuredPrograms)
{
    if (!backend || !backend->SupportsInstancing()) return false;
    if (!PrepareDrawStream()) return false;
    //共享四边形只在拖尾路径使用，按需创建
    if (!particleRenderer.PrepareQuad()) return false;

    const GpuShaderPass& shaderPass = material.shader->passes[0];
    if (!shaderPass.trailInstancedProgram.IsValid())
    {
        ++batchStats.failedUploads;
        ReportMissingGeometryVariant(cameraItems[batch.items[0]].material, *material.shader, "trail instanced");
        return false;
    }

    particleRenderer.BuildTrailInstances(batch, cameraItems, trailInstanceScratch);
    if (trailInstanceScratch.empty()) return false;

    {
        PROFILE("Render/UploadInstances");
        if (!drawStream.UploadTrailInstances(trailInstanceScratch))
        {
            ++batchStats.failedUploads;
            Log::Error("ForwardPipeline trail draw skipped: the instance upload was rejected.");
            return false;
        }
    }

    batchStats.uploadedBytes += static_cast<uint64>(trailInstanceScratch.size()) * sizeof(GpuTrailInstance);
    batchStats.submittedInstances += trailInstanceScratch.size();

    BindDrawState(batch, scene, camera, mainLight, cameraTexturesReady, material,
        shaderPass.trailInstancedProgram.id, configuredPrograms);
    backend->BindShaderProgram(shaderPass.trailInstancedProgram);
    backend->BindVertexInput(particleRenderer.GetQuadVertexInput(GeometryMode::TrailInstanced));
    if (!backend->BindInstanceBuffer(drawStream.GetInstanceBuffer(), 0))
    {
        ++batchStats.failedUploads;
        return false;
    }

    //共享四边形固定六个索引
    backend->DrawIndexedInstanced(0, 6, static_cast<uint32>(trailInstanceScratch.size()));
    ++batchStats.instancedDraws;
    return true;
}

//以普通单绘制执行批次，多 Pass 来源在这里逐对象跑完全部 Pass
bool ForwardPipeline::ExecuteUniformBatch(const DrawBatch& batch, const RenderScene& scene, const RenderCamera& camera,
    GpuResourceManager& gpuResourceManager, const RenderDirectionalLight* mainLight, bool cameraTexturesReady,
    const GpuMaterial& material, std::unordered_set<uint32>& configuredPrograms)
{
    bool drew = false;
    for (uint32 itemIndex : batch.items)
    {
        const DrawItem& item = cameraItems[itemIndex];
        const GpuMesh* mesh = gpuResourceManager.GetMesh(item.mesh);
        if (!mesh)
        {
            ++batchStats.invalidResources;
            Log::Error("ForwardPipeline draw skipped: mesh GPU resources are invalid.");
            continue;
        }

        //同一对象依次完成全部 Pass 再进入下一个对象，保持既有 Pass 顺序语义
        for (const GpuShaderPass& shaderPass : material.shader->passes)
        {
            if (!shaderPass.shaderProgram.IsValid()) continue;

            BindDrawState(batch, scene, camera, mainLight, cameraTexturesReady, material,
                shaderPass.shaderProgram.id, configuredPrograms);
            backend->BindShaderProgram(shaderPass.shaderProgram);
            backend->SetUniformMatrix4("u_Model", item.model);
            backend->BindVertexInput(mesh->vertexInput);
            backend->DrawIndexed(item.indexStart, item.indexCount);
            ++batchStats.ordinaryDraws;
            drew = true;
        }
    }

    return drew;
}

//以实例绘制执行批次
bool ForwardPipeline::ExecuteInstancedBatch(const DrawBatch& batch, const RenderScene& scene, const RenderCamera& camera,
    GpuResourceManager& gpuResourceManager, const RenderDirectionalLight* mainLight, bool cameraTexturesReady,
    const GpuMaterial& material, std::unordered_set<uint32>& configuredPrograms)
{
    if (!backend || !backend->SupportsInstancing()) return false;
    if (!PrepareDrawStream()) return false;

    const GpuShaderPass& shaderPass = material.shader->passes[0];
    if (!shaderPass.instancedProgram.IsValid())
    {
        //材质没编译出实例变体（编译失败）：本批不画，但要报出原因而不是静默丢弃
        ++batchStats.failedUploads;
        ReportMissingGeometryVariant(cameraItems[batch.items[0]].material, *material.shader, "instanced");
        return false;
    }

    //四边形来源没有网格，实例顶点输入取自粒子渲染器的共享四边形
    const DrawItem& first = cameraItems[batch.items[0]];
    GpuVertexInputID instancedInput;
    if (first.geometry == DrawGeometry::Mesh)
    {
        const GpuMesh* mesh = gpuResourceManager.GetMesh(first.mesh);
        instancedInput = mesh ? mesh->instancedVertexInput : GpuVertexInputID();
    }
    else
    {
        particleRenderer.PrepareQuad();
        instancedInput = particleRenderer.GetQuadVertexInput(GeometryMode::Instanced);
    }

    if (!instancedInput.IsValid())
    {
        ++batchStats.invalidResources;
        Log::Error("ForwardPipeline instanced draw skipped: the instance vertex input is invalid.");
        return false;
    }

    //实例顺序必须与透明排序序列一致，构建与阴影 Pass 共用同一份实现
    batchStats.invalidTransforms += particleRenderer.BuildMeshInstances(batch, cameraItems, instanceScratch);
    if (instanceScratch.empty()) return false;
    {
        PROFILE("Render/UploadInstances");
        if (!drawStream.UploadMeshInstances(instanceScratch))
        {
            ++batchStats.failedUploads;
            Log::Error("ForwardPipeline instanced draw skipped: the instance upload was rejected.");
            return false;
        }
    }

    batchStats.uploadedBytes += static_cast<uint64>(instanceScratch.size()) * sizeof(GpuMeshInstance);
    batchStats.submittedInstances += instanceScratch.size();

    BindDrawState(batch, scene, camera, mainLight, cameraTexturesReady, material,
        shaderPass.instancedProgram.id, configuredPrograms);
    backend->BindShaderProgram(shaderPass.instancedProgram);
    backend->BindVertexInput(instancedInput);
    if (!backend->BindInstanceBuffer(drawStream.GetInstanceBuffer(), 0))
    {
        ++batchStats.failedUploads;
        return false;
    }

    backend->DrawIndexedInstanced(batch.key.indexStart, batch.key.indexCount, static_cast<uint32>(instanceScratch.size()));
    ++batchStats.instancedDraws;
    return true;
}

//绑定一个批次的固定功能状态、公共 uniform 与材质参数
void ForwardPipeline::BindDrawState(const DrawBatch& batch, const RenderScene& scene, const RenderCamera& camera,
    const RenderDirectionalLight* mainLight, bool cameraTexturesReady, const GpuMaterial& material, uint32 programId,
    std::unordered_set<uint32>& configuredPrograms)
{
    //uniform 只对当前绑定生效，必须在这一步就把目标 program 绑上
    backend->BindShaderProgram(GpuShaderProgramID{ programId });
    backend->SetDepthTest(batch.key.depthTest != 0);
    backend->SetDepthWrite(batch.key.depthWrite != 0);
    backend->SetBlend(batch.key.blend != 0);
    //混合方程由来源与队列共同决定，材质无法覆盖
    backend->SetBlendMode(batch.key.blendMode);
    backend->SetCullMode(ConvertCullModeForBackend(static_cast<CullMode>(batch.key.cull)));

    //相机、光照与环境反射对本队列的每个 program 只提交一次
    if (configuredPrograms.insert(programId).second)
    {
        backend->SetUniformMatrix4("u_ViewProjection", camera.viewProjectionMatrix);
        shadows.BindUniforms(camera);
        backend->SetUniformVector3("u_CameraPosition", camera.position);
        backend->SetUniformFloat("u_CameraNearPlane", camera.nearPlane);
        backend->SetUniformFloat("u_CameraFarPlane", camera.farPlane);
        backend->SetUniformFloat("u_Time", camera.elapsedTime);
        backend->SetUniformInt("u_UseCameraTextures", batch.key.queue == DrawQueue::Refraction && cameraTexturesReady ? 1 : 0);
        backend->SetUniformColor("u_AmbientColor", scene.renderSettings.ambientColor);
        backend->SetUniformFloat("u_EnvironmentIntensity", environmentReflection.intensity);
        backend->SetUniformFloat("u_EnvironmentMaxLod", environmentReflection.maxLod);
        if (mainLight)
        {
            backend->SetUniformVector3("u_LightDirection", mainLight->direction);
            backend->SetUniformColor("u_LightColor", mainLight->color);
            backend->SetUniformFloat("u_LightIntensity", mainLight->intensity);
            backend->SetUniformFloat("u_ShadowStrength", std::clamp(mainLight->shadowStrength, 0.0f, 1.0f));
        }
        else
        {
            backend->SetUniformVector3("u_LightDirection", { 0.0f, -1.0f, 0.0f });
            backend->SetUniformColor("u_LightColor", { 1.0f, 1.0f, 1.0f, 1.0f });
            backend->SetUniformFloat("u_LightIntensity", 0.0f);
            backend->SetUniformFloat("u_ShadowStrength", 0.0f);
        }
    }

    //以下参数随批次变化，即使 program 已经配置过也必须重设。
    //实例模式的每实例 tint 与 uv 走顶点属性，这两个 uniform 不会被声明。
    backend->SetUniformColor("u_InstanceTint", { 1.0f, 1.0f, 1.0f, 1.0f });
    backend->SetUniformColor("u_InstanceUvRect", { 0.0f, 0.0f, 1.0f, 1.0f });

    for (const GpuMaterialColorBinding& binding : material.colorBindings)
    {
        backend->SetUniformColor(binding.uniformName.c_str(), binding.value);
    }
    for (const GpuMaterialFloatBinding& binding : material.floatBindings)
    {
        backend->SetUniformFloat(binding.uniformName.c_str(), binding.value);
    }

    for (uint32 slot = 0; slot < material.textureBindings.size(); ++slot)
    {
        const GpuMaterialTextureBinding& binding = material.textureBindings[slot];
        backend->SetUniformInt(binding.uniformName.c_str(), static_cast<int32>(slot));
        backend->SetUniformInt(binding.presenceUniformName.c_str(), binding.hasTexture ? 1 : 0);
        backend->BindTexture(slot, binding.hasTexture ? binding.texture : GpuTextureID());
    }

    //绑定内置渲染纹理
    uint32 shadowTextureSlot = static_cast<uint32>(material.textureBindings.size());
    shadows.BindTexture(shadowTextureSlot);
    backend->SetUniformInt("u_ReceiveShadows", batch.key.receiveShadows ? 1 : 0);
    uint32 environmentSlot = shadowTextureSlot + 3;
    backend->SetUniformInt("u_EnvironmentTexture", static_cast<int32>(environmentSlot));
    backend->BindCubeTexture(environmentSlot, environmentReflection.texture);

    if (batch.key.queue == DrawQueue::Refraction)
    {
        uint32 cameraColorSlot = shadowTextureSlot + 1;
        uint32 cameraDepthSlot = shadowTextureSlot + 2;
        backend->SetUniformInt("u_CameraColorTexture", static_cast<int32>(cameraColorSlot));
        backend->SetUniformInt("u_CameraDepthTexture", static_cast<int32>(cameraDepthSlot));
        backend->BindTexture(cameraColorSlot, cameraTexturesReady ? camera.cameraColorTexture : GpuTextureID());
        backend->BindDepthTexture(cameraDepthSlot, cameraTexturesReady ? camera.cameraDepthTexture : GpuDepthTextureID());
    }
}

//按需创建流式绘制缓冲
bool ForwardPipeline::PrepareDrawStream()
{
    if (drawStreamReady) return true;
    if (!backend || !backend->SupportsInstancing()) return false;

    if (!drawStream.Initialize(backend))
    {
        Log::Error("ForwardPipeline draw stream initialization failed.");
        return false;
    }

    drawStreamReady = true;
    return true;
}

void ForwardPipeline::LoadBuiltinShaders()
{
    if (!builtinShadersInvalidated) return;

    //Editor 尚未打开项目时没有内容根；等项目加载后再解析项目内置 Shader。
    if (!PathDefines::HasContentRoot()) return;

    //阴影深度
    BuiltinShaderKeys& keys = GetBuiltinShaderKeys();
    shadowDepthShader.Set(ResourceManager::Load<Shader>(ResolveBuiltinShaderKey(ShadowDepthShaderFileName, keys.shadowDepth)));
    //天空盒
    skyboxShader.Set(ResourceManager::Load<Shader>(ResolveBuiltinShaderKey(SkyboxShaderFileName, keys.skybox)));
    builtinShadersInvalidated = false;

    //记录内置 Shader 加载错误
    if (!shadowDepthShader.Get())
    {
        Log::Error("ForwardPipeline: shadow_depth.orbshader was not found in the content root.");
    }
    if (!skyboxShader.Get())
    {
        Log::Error("ForwardPipeline: skybox.orbshader was not found in the content root.");
    }
}

bool ForwardPipeline::PrepareSkyboxMesh()
{
    if (!backend) return false;

    //复用天空盒网格
    if (skyboxMesh.IsValid()) return true;

    //定义天空盒立方体数据
    constexpr uint32 vertexFloatCount = 11;
    constexpr uint32 vertexStride = vertexFloatCount * sizeof(float32);
    const float32 positions[8][3] =
    {
        { -1.0f, -1.0f, -1.0f },
        { 1.0f, -1.0f, -1.0f },
        { 1.0f, 1.0f, -1.0f },
        { -1.0f, 1.0f, -1.0f },
        { -1.0f, -1.0f, 1.0f },
        { 1.0f, -1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f },
        { -1.0f, 1.0f, 1.0f },
    };
    const uint32 indices[] =
    {
        0, 1, 2, 2, 3, 0,
        4, 6, 5, 6, 4, 7,
        0, 4, 5, 5, 1, 0,
        3, 2, 6, 6, 7, 3,
        1, 5, 6, 6, 2, 1,
        0, 3, 7, 7, 4, 0,
    };

    //构造天空盒顶点数据
    float32 vertexData[8 * vertexFloatCount] = {};
    for (uint32 vertex = 0; vertex < 8; ++vertex)
    {
        uint32 offset = vertex * vertexFloatCount;
        vertexData[offset + 0] = positions[vertex][0];
        vertexData[offset + 1] = positions[vertex][1];
        vertexData[offset + 2] = positions[vertex][2];
    }

    //创建天空盒顶点和索引缓冲
    GpuBufferDesc vertexBufferDesc;
    vertexBufferDesc.data = vertexData;
    vertexBufferDesc.size = sizeof(vertexData);

    GpuBufferDesc indexBufferDesc;
    indexBufferDesc.data = indices;
    indexBufferDesc.size = sizeof(indices);

    skyboxMesh.vertexBuffer = backend->CreateVertexBuffer(vertexBufferDesc);
    skyboxMesh.indexBuffer = backend->CreateIndexBuffer(indexBufferDesc);
    skyboxMesh.indexCount = static_cast<uint32>(sizeof(indices) / sizeof(indices[0]));

    //创建天空盒顶点输入
    GpuVertexInputDesc inputDesc;
    inputDesc.vertexBuffer = skyboxMesh.vertexBuffer;
    inputDesc.indexBuffer = skyboxMesh.indexBuffer;
    inputDesc.stride = vertexStride;
    skyboxMesh.vertexInput = backend->CreateVertexInput(inputDesc);
    if (!skyboxMesh.IsValid())
    {
        //回收天空盒网格资源
        Log::Error("ForwardPipeline skybox setup failed: cube mesh creation failed.");
        DeleteGpuMesh(backend, skyboxMesh);
        return false;
    }

    return true;
}

void ForwardPipeline::RenderSkybox(const RenderScene& scene, const RenderCamera& camera, GpuResourceManager& gpuResourceManager)
{
    //获取天空盒 Shader
    Shader* sourceShader = GetOrLoadBuiltinShader(skyboxShader,
        ResolveBuiltinShaderKey(SkyboxShaderFileName, GetBuiltinShaderKeys().skybox));
    if (!scene.renderSettings.skyboxEnabled || !sourceShader) return;

    //获取天空盒资源
    Skybox* skybox = scene.renderSettings.skybox.Get();
    if (!skybox || !PrepareSkyboxMesh()) return;

    //上传天空盒 GPU 资源
    GpuCubeTextureID cubeTexture = gpuResourceManager.GetSkybox(skybox);
    const GpuShader* shader = gpuResourceManager.GetShader(sourceShader);
    if (!cubeTexture.IsValid() || !shader) return;

    //计算天空盒视图投影矩阵
    matrix4x4 view = camera.viewMatrix;
    view.m[12] = 0.0f;
    view.m[13] = 0.0f;
    view.m[14] = 0.0f;
    matrix4x4 viewProjection = RenderMath::Mul(camera.projectionMatrix, view);

    //绘制天空盒立方体
    backend->SetDepthTest(false);
    backend->SetDepthWrite(false);
    backend->SetBlend(false);
    const GpuShaderPass& shaderPass = shader->passes[0];
    backend->BindShaderProgram(shaderPass.shaderProgram);
    backend->SetUniformMatrix4("u_ViewProjection", viewProjection);
    backend->SetUniformInt("u_SkyboxTexture", 0);
    backend->BindCubeTexture(0, cubeTexture);
    backend->BindVertexInput(skyboxMesh.vertexInput);
    backend->DrawIndexed(0, skyboxMesh.indexCount);
    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->SetDepthWrite(true);
    backend->SetDepthTest(true);
}
