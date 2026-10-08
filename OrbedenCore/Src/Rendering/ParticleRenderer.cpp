#include "Rendering/ParticleRenderer.h"

#include "Log/Log.h"
#include "Rendering/GeometryExpander.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Particles/ParticleSimulationSystem.h"
#include "Runtime/Object/Ens.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/World.h"

#include <algorithm>
#include <cmath>

namespace
{
    //Billboard 四边形的顶点布局与引擎网格一致：位置、法线、uv、切线
    constexpr uint32 QuadVertexFloatCount = 11;
    constexpr uint32 QuadVertexStride = QuadVertexFloatCount * sizeof(float32);
    //单批展开几何的上限
    constexpr usize MaximumExpandedVertices = 262144u;
    constexpr usize MaximumExpandedIndices = 786432u;

    //从矩阵取旋转部分
    quaternion GetRotation(const matrix4x4& matrix)
    {
        quaternion result;
        float32 trace = matrix.m[0] + matrix.m[5] + matrix.m[10];
        if (trace > 0.0f)
        {
            float32 scale = std::sqrt(trace + 1.0f) * 2.0f;
            result.w = 0.25f * scale;
            result.x = (matrix.m[6] - matrix.m[9]) / scale;
            result.y = (matrix.m[8] - matrix.m[2]) / scale;
            result.z = (matrix.m[1] - matrix.m[4]) / scale;
        }
        else if (matrix.m[0] > matrix.m[5] && matrix.m[0] > matrix.m[10])
        {
            float32 scale = std::sqrt(1.0f + matrix.m[0] - matrix.m[5] - matrix.m[10]) * 2.0f;
            result.w = (matrix.m[6] - matrix.m[9]) / scale;
            result.x = 0.25f * scale;
            result.y = (matrix.m[4] + matrix.m[1]) / scale;
            result.z = (matrix.m[8] + matrix.m[2]) / scale;
        }
        else if (matrix.m[5] > matrix.m[10])
        {
            float32 scale = std::sqrt(1.0f + matrix.m[5] - matrix.m[0] - matrix.m[10]) * 2.0f;
            result.w = (matrix.m[8] - matrix.m[2]) / scale;
            result.x = (matrix.m[4] + matrix.m[1]) / scale;
            result.y = 0.25f * scale;
            result.z = (matrix.m[9] + matrix.m[6]) / scale;
        }
        else
        {
            float32 scale = std::sqrt(1.0f + matrix.m[10] - matrix.m[0] - matrix.m[5]) * 2.0f;
            result.w = (matrix.m[1] - matrix.m[4]) / scale;
            result.x = (matrix.m[8] + matrix.m[2]) / scale;
            result.y = (matrix.m[9] + matrix.m[6]) / scale;
            result.z = 0.25f * scale;
        }

        return result;
    }

    //序列帧的 uv 变换
    color ComputeUvRect(uint32 tilesX, uint32 tilesY, float32 animationCycles, uint32 startFrame, float32 normalizedAge)
    {
        uint32 frameCount = tilesX * tilesY;
        if (frameCount <= 1) return { 0.0f, 0.0f, 1.0f, 1.0f };

        //寿命末端留一点余量，避免浮点误差把归一化年龄推到 1 之外
        float32 clamped = std::clamp(normalizedAge, 0.0f, 1.0f - 1.0e-7f);
        uint32 advanced = static_cast<uint32>(std::floor(clamped * animationCycles * static_cast<float32>(frameCount)));
        uint32 frame = (startFrame + advanced) % frameCount;

        float32 scaleU = 1.0f / static_cast<float32>(tilesX);
        float32 scaleV = 1.0f / static_cast<float32>(tilesY);
        uint32 column = frame % tilesX;
        uint32 row = frame / tilesX;
        //图集行从图像上方开始，所以 V 轴从上往下推进
        return { static_cast<float32>(column) * scaleU, 1.0f - static_cast<float32>(row + 1) * scaleV, scaleU, scaleV };
    }

    //Billboard 的世界半径是四边形外接圆
    float32 GetBillboardRadius(float32 size)
    {
        return std::sqrt(2.0f) * size * 0.5f;
    }
}

bool ParticleRenderer::Initialize(RenderBackend* renderBackend)
{
    if (!renderBackend)
    {
        Log::Error("ParticleRenderer initialization failed: no render backend.");
        return false;
    }

    backend = renderBackend;
    return true;
}

bool ParticleRenderer::PrepareQuad()
{
    if (quadReady) return true;

    //共享四边形：中心在原点的单位正方形，正面朝 +Z
    const float32 quadVertices[4][3] =
    {
        { -0.5f, -0.5f, 0.0f },
        { 0.5f, -0.5f, 0.0f },
        { 0.5f, 0.5f, 0.0f },
        { -0.5f, 0.5f, 0.0f },
    };
    const float32 quadUvs[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
    const uint32 quadIndices[6] = { 0, 1, 2, 0, 2, 3 };

    float32 vertexData[4 * QuadVertexFloatCount] = {};
    for (uint32 vertex = 0; vertex < 4; ++vertex)
    {
        usize offset = static_cast<usize>(vertex) * QuadVertexFloatCount;
        vertexData[offset + 0] = quadVertices[vertex][0];
        vertexData[offset + 1] = quadVertices[vertex][1];
        vertexData[offset + 2] = quadVertices[vertex][2];
        //法线朝 +Z，切线沿 +X
        vertexData[offset + 5] = 1.0f;
        vertexData[offset + 6] = quadUvs[vertex][0];
        vertexData[offset + 7] = quadUvs[vertex][1];
        vertexData[offset + 8] = 1.0f;
    }

    GpuBufferDesc vertexDesc;
    vertexDesc.data = vertexData;
    vertexDesc.size = sizeof(vertexData);
    GpuBufferDesc indexDesc;
    indexDesc.data = quadIndices;
    indexDesc.size = sizeof(quadIndices);

    quadVertexBuffer = backend->CreateVertexBuffer(vertexDesc);
    quadIndexBuffer = backend->CreateIndexBuffer(indexDesc);
    if (quadVertexBuffer.IsValid() && quadIndexBuffer.IsValid())
    {
        GpuVertexInputDesc inputDesc;
        inputDesc.vertexBuffer = quadVertexBuffer;
        inputDesc.indexBuffer = quadIndexBuffer;
        inputDesc.stride = QuadVertexStride;

        quadUniformInput = backend->CreateVertexInput(inputDesc);
        inputDesc.layout = GpuVertexLayout::InstancedMesh;
        quadInstancedInput = backend->CreateVertexInput(inputDesc);
        inputDesc.layout = GpuVertexLayout::InstancedTrail;
        quadTrailInput = backend->CreateVertexInput(inputDesc);
    }

    if (!quadUniformInput.IsValid() || !quadInstancedInput.IsValid() || !quadTrailInput.IsValid())
    {
        Log::Error("ParticleRenderer quad setup failed; releasing the partially created resources.");
        InvalidateResourceCaches();
        return false;
    }

    quadReady = true;
    return true;
}

void ParticleRenderer::InvalidateResourceCaches()
{
    if (backend)
    {
        //先释放三份顶点输入再释放它们引用的缓冲
        backend->DeleteVertexInput(quadUniformInput);
        backend->DeleteVertexInput(quadInstancedInput);
        backend->DeleteVertexInput(quadTrailInput);
        backend->DeleteVertexBuffer(quadVertexBuffer);
        backend->DeleteIndexBuffer(quadIndexBuffer);
    }

    quadUniformInput = GpuVertexInputID();
    quadInstancedInput = GpuVertexInputID();
    quadTrailInput = GpuVertexInputID();
    quadVertexBuffer = GpuVertexBufferID();
    quadIndexBuffer = GpuIndexBufferID();
    quadReady = false;
    trailSegments.clear();
}

void ParticleRenderer::Shutdown()
{
    InvalidateResourceCaches();
    backend = nullptr;
}

GpuVertexInputID ParticleRenderer::GetQuadVertexInput(GeometryMode mode) const
{
    if (mode == GeometryMode::Instanced) return quadInstancedInput;
    if (mode == GeometryMode::TrailInstanced) return quadTrailInput;
    return quadUniformInput;
}

void ParticleRenderer::CaptureFrame(World& world, const ParticleSimulationContext& context,
    const TransformCache& transformCache, ParticleFrameSnapshot& snapshot)
{
    snapshot.world = &world;
    snapshot.revision = world.GetContentRevision();
    snapshot.preview = context.IsPreview();
    snapshot.particles.clear();
    snapshot.trails.clear();

    for (const ParticleEmitterState& state : context.GetEmitters())
    {
        ParticleSystem* source = state.source.Get();
        if (!source) continue;

        Ens* ens = source->GetEns();
        if (!ens || !ens->GetWorldActive() || !source->GetEnabled()) continue;

        const ParticleSettings& settings = state.validatedSettings;
        if (state.particles.empty() && state.trails.empty()) continue;

        bool local = settings.main.simulationSpace == ParticleSimulationSpace::Local;
        matrix4x4 emitterWorld = transformCache.GetWorldMatrix(source->GetEnsId());
        quaternion emitterRotation = GetRotation(emitterWorld);
        //局部空间的尺寸与拖尾宽度都是发射器局部单位，提交渲染前乘发射器世界尺度
        float32 emitterBasis = local ? RenderMath::GetMaximumBasisLength(emitterWorld) : 1.0f;

        for (const ParticleRecord& particle : state.particles)
        {
            float32 normalizedAge = particle.lifetime > 0.0f
                ? std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f) : 1.0f;
            float32 size = particle.startSize * particle.inheritedSize *
                std::max(0.0f, EvaluateCurve(settings.motion.sizeOverLifetime, normalizedAge)) * emitterBasis;
            //尺寸为 0 仍然模拟，但不提交渲染
            if (!(size > 0.0f)) continue;

            color gradient = EvaluateGradient(settings.motion.colorOverLifetime, normalizedAge);
            color linearColor;
            linearColor.r = particle.startLinearColor.r * particle.inheritedColor.r * gradient.r;
            linearColor.g = particle.startLinearColor.g * particle.inheritedColor.g * gradient.g;
            linearColor.b = particle.startLinearColor.b * particle.inheritedColor.b * gradient.b;
            linearColor.a = particle.startLinearColor.a * particle.inheritedColor.a * gradient.a;
            if (!std::isfinite(linearColor.r) || !std::isfinite(linearColor.g) || !std::isfinite(linearColor.b) ||
                !std::isfinite(linearColor.a)) continue;

            ParticleRenderRecord record;
            record.sourceObjectId = source->GetObjectId();
            record.owner = source->GetEnsId();
            record.birthId = particle.birthId;
            //局部模拟在这里一次换算到世界空间，之后的相机阶段不再读发射器矩阵
            record.worldPosition = local ? RenderMath::TransformPoint(emitterWorld, particle.position) : particle.position;
            record.worldRotation = local ? RenderMath::Mul(emitterRotation, particle.rotation) : particle.rotation;
            record.size = size;
            record.angle = particle.angle;
            record.linearColor = linearColor;
            record.uvRect = ComputeUvRect(settings.rendering.tilesX, settings.rendering.tilesY,
                settings.rendering.animationCycles, particle.startFrame, normalizedAge);
            //Mesh 粒子的世界矩阵：angle 初值已经进入 rotation，这里只补上增量旋转
            record.worldModel = RenderMath::TRS(record.worldPosition,
                RenderMath::Mul(record.worldRotation, RenderMath::RotationZ(particle.angle - particle.startAngle)),
                { size, size, size });
            record.mesh = source->mesh;
            record.materials = source->materials;
            record.drawLayer = source->drawLayer;
            record.castShadows = source->castShadows;
            record.receiveShadows = source->receiveShadows;
            record.renderPath = settings.rendering.path;
            record.renderMode = settings.rendering.mode;
            record.blendMode = settings.rendering.blendMode;

            //保守包围盒：Billboard 用外接圆，Mesh 用源本地盒经世界矩阵变换
            if (record.renderMode == ParticleRenderMode::Mesh && record.mesh.Get())
            {
                const bounds3& localBounds = record.mesh.Get()->GetLocalBounds();
                record.worldBounds = localBounds.valid ? RenderMath::TransformBounds(record.worldModel, localBounds) : bounds3();
            }
            else
            {
                float32 radius = GetBillboardRadius(size);
                record.worldBounds.center = record.worldPosition;
                record.worldBounds.extents = { radius, radius, radius };
                record.worldBounds.valid = true;
            }

            if (record.worldBounds.valid && std::isfinite(record.worldBounds.extents.x) &&
                std::isfinite(record.worldBounds.extents.y) && std::isfinite(record.worldBounds.extents.z))
            {
                snapshot.particles.push_back(std::move(record));
            }
        }

        //拖尾：位置换算到世界，长度曲线、颜色渐变与尾部淡出在这里一次求值
        for (const ParticleTrailRecord& trail : state.trails)
        {
            if (trail.count < 2) continue;

            ParticleTrailRenderRecord record;
            record.sourceObjectId = source->GetObjectId();
            record.owner = source->GetEnsId();
            record.trailId = trail.trailId;
            record.material = source->trailMaterial;
            record.renderPath = settings.rendering.path;
            record.blendMode = settings.rendering.blendMode;
            record.drawLayer = source->drawLayer;
            record.textureTileLength = settings.trails.textureTileLength;
            record.points.resize(trail.count);
            uint32 capacity = settings.trails.maxPointsPerTrail;
            for (uint32 index = 0; index < trail.count; ++index)
            {
                //环形点池：head 是下一个写入位置，最早的点在 head-count 处
                uint32 slot = (trail.head + capacity - trail.count + index) % capacity;
                const ParticleTrailPoint& point = state.trailPoints[trail.pointStart + slot];
                ParticleTrailPointSnapshot& target = record.points[index];
                target.position = local ? RenderMath::TransformPoint(emitterWorld, point.position) : point.position;
                target.width = point.width * emitterBasis;
                target.linearColor = point.linearColor;
                target.length = point.accumulatedLength;
                //按点年龄淡出；年龄取所属发射器的模拟时间，暂停时不会因别处推进而继续变淡
                float32 pointAge = static_cast<float32>(std::max(0.0, state.simulationTime - point.time));
                target.linearColor.a *= std::clamp(1.0f - pointAge / std::max(settings.trails.lifetime, 1.0e-6f), 0.0f, 1.0f);
            }

            //归一化到本条拖尾的有效弧长上：长度曲线乘宽度，长度渐变成颜色
            float32 totalLength = record.points.back().length - record.points.front().length;
            float32 inverseLength = totalLength > 1.0e-6f ? 1.0f / totalLength : 0.0f;
            for (ParticleTrailPointSnapshot& point : record.points)
            {
                float32 along = std::clamp((point.length - record.points.front().length) * inverseLength, 0.0f, 1.0f);
                point.width = std::max(0.0f, point.width * EvaluateCurve(settings.trails.widthOverLength, along));
                color tint = EvaluateGradient(settings.trails.colorOverLength, along);
                point.linearColor.r *= tint.r;
                point.linearColor.g *= tint.g;
                point.linearColor.b *= tint.b;
                point.linearColor.a *= tint.a;
            }

            snapshot.trails.push_back(std::move(record));
        }
    }
}

void ParticleRenderer::AppendCameraItems(const ParticleFrameSnapshot& snapshot, const RenderCamera& camera, List<DrawItem>& items)
{
    trailSegments.clear();

    //相机基向量：右手系下 backward 指向观察者，四边形正面朝 +Z
    vector3 right = RenderMath::Normalize({ camera.worldMatrix.m[0], camera.worldMatrix.m[1], camera.worldMatrix.m[2] });
    vector3 up = { camera.worldMatrix.m[4], camera.worldMatrix.m[5], camera.worldMatrix.m[6] };
    //正交化，避免非均匀缩放的父级把手柄带歪
    float32 upProjection = RenderMath::Dot(up, right);
    up = RenderMath::Normalize({ up.x - right.x * upProjection, up.y - right.y * upProjection, up.z - right.z * upProjection });
    vector3 backward = RenderMath::Cross(right, up);

    for (const ParticleRenderRecord& record : snapshot.particles)
    {
        if ((record.drawLayer & camera.drawLayerMask) == 0) continue;
        if (record.worldBounds.valid && !RenderMath::Intersects(camera.viewFrustum, record.worldBounds)) continue;

        vector3 toItem = { record.worldBounds.center.x - camera.position.x, record.worldBounds.center.y - camera.position.y,
            record.worldBounds.center.z - camera.position.z };
        float32 cameraDistance = RenderMath::Dot(toItem, toItem);

        if (record.renderMode == ParticleRenderMode::Billboard)
        {
            DrawItem item;
            item.source = DrawSource::Particle;
            item.geometry = DrawGeometry::BillboardQuad;
            item.owner = record.owner;
            item.sourceObjectId = record.sourceObjectId;
            item.elementId = record.birthId;
            //共享四边形固定六个索引，批次键与实例绘制都按这个区间
            item.indexStart = 0;
            item.indexCount = 6;
            item.drawLayer = record.drawLayer;
            item.material = record.materials.empty() ? nullptr : record.materials[0].Get();
            if (!item.material) continue;
            item.queue = item.material->GetDrawQueue();
            item.blendMode = record.blendMode;
            //数量为一颗时也走实例绘制，路径由组件配置决定
            item.mode = record.renderPath == ParticleRenderPath::Instanced ? GeometryMode::Instanced : GeometryMode::Expanded;
            item.worldBounds = record.worldBounds;
            item.cameraDistance = cameraDistance;
            item.linearTint = record.linearColor;
            item.uvRect = record.uvRect;
            item.castShadows = false;
            item.receiveShadows = record.receiveShadows;
            item.strategy = DrawStrategy::Auto;

            //旋转后的基向量构成 Billboard 的世界矩阵
            float32 angleRadians = record.angle * 0.01745329251994329577f;
            float32 cosine = std::cos(angleRadians);
            float32 sine = std::sin(angleRadians);
            vector3 rotatedRight = { (right.x * cosine + up.x * sine) * record.size,
                (right.y * cosine + up.y * sine) * record.size, (right.z * cosine + up.z * sine) * record.size };
            vector3 rotatedUp = { (-right.x * sine + up.x * cosine) * record.size,
                (-right.y * sine + up.y * cosine) * record.size, (-right.z * sine + up.z * cosine) * record.size };
            item.model.m[0] = rotatedRight.x;
            item.model.m[1] = rotatedRight.y;
            item.model.m[2] = rotatedRight.z;
            item.model.m[4] = rotatedUp.x;
            item.model.m[5] = rotatedUp.y;
            item.model.m[6] = rotatedUp.z;
            item.model.m[8] = backward.x;
            item.model.m[9] = backward.y;
            item.model.m[10] = backward.z;
            item.model.m[12] = record.worldPosition.x;
            item.model.m[13] = record.worldPosition.y;
            item.model.m[14] = record.worldPosition.z;
            items.push_back(item);
            continue;
        }

        //Mesh 模式逐子网格展开
        Mesh* mesh = record.mesh.Get();
        if (!mesh) continue;
        for (usize subIndex = 0; subIndex < mesh->subMeshes.size(); ++subIndex)
        {
            Material* material = subIndex < record.materials.size() ? record.materials[subIndex].Get() : nullptr;
            if (!material) continue;

            const SubMesh& subMesh = mesh->subMeshes[subIndex];
            usize start = static_cast<usize>(subMesh.indexStart);
            usize count = static_cast<usize>(subMesh.indexCount);
            if (count == 0 || start > mesh->indices.size() || count > mesh->indices.size() - start) continue;

            DrawItem item;
            item.source = DrawSource::Particle;
            item.geometry = DrawGeometry::Mesh;
            item.owner = record.owner;
            item.sourceObjectId = record.sourceObjectId;
            item.elementId = record.birthId;
            item.subMeshIndex = static_cast<uint32>(subIndex);
            item.indexStart = subMesh.indexStart;
            item.indexCount = subMesh.indexCount;
            item.drawLayer = record.drawLayer;
            item.mesh = mesh;
            item.material = material;
            item.queue = material->GetDrawQueue();
            item.blendMode = record.blendMode;
            item.mode = record.renderPath == ParticleRenderPath::Instanced ? GeometryMode::Instanced : GeometryMode::Expanded;
            item.model = record.worldModel;
            item.worldBounds = record.worldBounds;
            item.cameraDistance = cameraDistance;
            item.linearTint = record.linearColor;
            item.uvRect = record.uvRect;
            //Billboard 与拖尾不投射阴影，只有 Opaque Mesh 粒子投射
            item.castShadows = record.castShadows;
            item.receiveShadows = record.receiveShadows;
            item.strategy = DrawStrategy::Auto;
            items.push_back(item);
        }
    }

    //拖尾：每个段单独排序，不把整条长尾迹用一个中心排序
    for (const ParticleTrailRenderRecord& record : snapshot.trails)
    {
        if ((record.drawLayer & camera.drawLayerMask) == 0) continue;
        Material* material = record.material.Get();
        if (!material) continue;

        for (usize index = 0; index + 1 < record.points.size(); ++index)
        {
            const ParticleTrailPointSnapshot& startPoint = record.points[index];
            const ParticleTrailPointSnapshot& endPoint = record.points[index + 1];

            //切线取相邻差，端点用唯一的那一段
            vector3 previous = index > 0 ? record.points[index - 1].position : startPoint.position;
            vector3 next = index + 2 < record.points.size() ? record.points[index + 2].position : endPoint.position;
            vector3 tangent = RenderMath::Normalize({ next.x - previous.x, next.y - previous.y, next.z - previous.z });
            if (RenderMath::Dot(tangent, tangent) <= 1.0e-12f) continue;

            vector3 midpoint = { (startPoint.position.x + endPoint.position.x) * 0.5f,
                (startPoint.position.y + endPoint.position.y) * 0.5f,
                (startPoint.position.z + endPoint.position.z) * 0.5f };
            vector3 viewDirection = RenderMath::Normalize({ camera.position.x - midpoint.x,
                camera.position.y - midpoint.y, camera.position.z - midpoint.z });
            vector3 side = RenderMath::Cross(tangent, viewDirection);
            if (RenderMath::Dot(side, side) <= 1.0e-12f)
            {
                //视线与切线平行时退化，改用相机右向量投影到垂直切线的平面
                float32 projection = RenderMath::Dot(right, tangent);
                side = { right.x - tangent.x * projection, right.y - tangent.y * projection, right.z - tangent.z * projection };
                if (RenderMath::Dot(side, side) <= 1.0e-12f) side = up;
            }
            side = RenderMath::Normalize(side);

            ParticleTrailSegment segment;
            float32 startHalf = startPoint.width * 0.5f;
            float32 endHalf = endPoint.width * 0.5f;
            segment.corners[0] = { startPoint.position.x - side.x * startHalf, startPoint.position.y - side.y * startHalf,
                startPoint.position.z - side.z * startHalf };
            segment.corners[1] = { startPoint.position.x + side.x * startHalf, startPoint.position.y + side.y * startHalf,
                startPoint.position.z + side.z * startHalf };
            segment.corners[2] = { endPoint.position.x + side.x * endHalf, endPoint.position.y + side.y * endHalf,
                endPoint.position.z + side.z * endHalf };
            segment.corners[3] = { endPoint.position.x - side.x * endHalf, endPoint.position.y - side.y * endHalf,
                endPoint.position.z - side.z * endHalf };
            segment.startColor = startPoint.linearColor;
            segment.endColor = endPoint.linearColor;
            //沿长度的纹理坐标按实际弧长走，避免采样点疏密影响贴图速度
            segment.u0 = startPoint.length / std::max(record.textureTileLength, 1.0e-4f);
            segment.u1 = endPoint.length / std::max(record.textureTileLength, 1.0e-4f);

            vector3 minimum = { std::min(std::min(segment.corners[0].x, segment.corners[1].x), std::min(segment.corners[2].x, segment.corners[3].x)),
                std::min(std::min(segment.corners[0].y, segment.corners[1].y), std::min(segment.corners[2].y, segment.corners[3].y)),
                std::min(std::min(segment.corners[0].z, segment.corners[1].z), std::min(segment.corners[2].z, segment.corners[3].z)) };
            vector3 maximum = { std::max(std::max(segment.corners[0].x, segment.corners[1].x), std::max(segment.corners[2].x, segment.corners[3].x)),
                std::max(std::max(segment.corners[0].y, segment.corners[1].y), std::max(segment.corners[2].y, segment.corners[3].y)),
                std::max(std::max(segment.corners[0].z, segment.corners[1].z), std::max(segment.corners[2].z, segment.corners[3].z)) };

            DrawItem item;
            item.source = DrawSource::Trail;
            item.geometry = DrawGeometry::TrailQuad;
            item.owner = record.owner;
            item.sourceObjectId = record.sourceObjectId;
            //记录编号由拖尾编号与段下标复合而成
            item.elementId = (record.trailId << 32) | static_cast<uint64>(index);
            item.indexStart = 0;
            item.indexCount = 6;
            item.drawLayer = record.drawLayer;
            item.material = material;
            item.queue = material->GetDrawQueue();
            item.blendMode = record.blendMode;
            item.mode = record.renderPath == ParticleRenderPath::Instanced
                ? GeometryMode::TrailInstanced : GeometryMode::Expanded;
            item.worldBounds.center = { (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f, (minimum.z + maximum.z) * 0.5f };
            item.worldBounds.extents = { (maximum.x - minimum.x) * 0.5f, (maximum.y - minimum.y) * 0.5f, (maximum.z - minimum.z) * 0.5f };
            item.worldBounds.valid = true;

            vector3 segmentCenter = item.worldBounds.center;
            vector3 toSegment = { segmentCenter.x - camera.position.x, segmentCenter.y - camera.position.y,
                segmentCenter.z - camera.position.z };
            item.cameraDistance = RenderMath::Dot(toSegment, toSegment);
            item.castShadows = false;
            item.receiveShadows = false;
            item.strategy = DrawStrategy::Auto;
            //段几何按 sourceIndex 存进本相机的临时容器
            item.sourceIndex = static_cast<uint32>(trailSegments.size());
            trailSegments.push_back(segment);
            items.push_back(item);
        }
    }
}

void ParticleRenderer::AppendShadowItems(const ParticleFrameSnapshot& snapshot, const RenderCamera& camera,
    const frustum& lightFrustum, List<DrawItem>& items)
{
    //Billboard 与拖尾不投影，只有开启投影的 Opaque Mesh 粒子进阴影候选
    for (const ParticleRenderRecord& record : snapshot.particles)
    {
        if (record.renderMode != ParticleRenderMode::Mesh || !record.castShadows) continue;
        if ((record.drawLayer & camera.drawLayerMask) == 0) continue;
        if (!record.worldBounds.valid || !RenderMath::Intersects(lightFrustum, record.worldBounds)) continue;

        Mesh* mesh = record.mesh.Get();
        if (!mesh) continue;
        for (usize subIndex = 0; subIndex < mesh->subMeshes.size(); ++subIndex)
        {
            Material* material = subIndex < record.materials.size() ? record.materials[subIndex].Get() : nullptr;
            if (!material || material->GetDrawQueue() != DrawQueue::Opaque) continue;

            const SubMesh& subMesh = mesh->subMeshes[subIndex];
            usize start = static_cast<usize>(subMesh.indexStart);
            usize count = static_cast<usize>(subMesh.indexCount);
            if (count == 0 || start > mesh->indices.size() || count > mesh->indices.size() - start) continue;

            DrawItem item;
            item.source = DrawSource::Particle;
            item.geometry = DrawGeometry::Mesh;
            item.owner = record.owner;
            item.sourceObjectId = record.sourceObjectId;
            item.elementId = record.birthId;
            item.subMeshIndex = static_cast<uint32>(subIndex);
            item.indexStart = subMesh.indexStart;
            item.indexCount = subMesh.indexCount;
            item.drawLayer = record.drawLayer;
            item.mesh = mesh;
            item.material = material;
            item.queue = DrawQueue::Opaque;
            item.mode = record.renderPath == ParticleRenderPath::Instanced ? GeometryMode::Instanced : GeometryMode::Expanded;
            item.model = record.worldModel;
            item.worldBounds = record.worldBounds;
            item.linearTint = record.linearColor;
            item.uvRect = record.uvRect;
            item.castShadows = true;
            item.receiveShadows = false;
            item.strategy = DrawStrategy::Auto;
            items.push_back(item);
        }
    }
}

uint32 ParticleRenderer::BuildMeshInstances(const DrawBatch& batch, const List<DrawItem>& items, List<GpuMeshInstance>& instances)
{
    instances.clear();
    instances.reserve(batch.items.size());
    uint32 rejected = 0;
    for (uint32 itemIndex : batch.items)
    {
        if (itemIndex >= items.size())
        {
            ++rejected;
            continue;
        }

        const DrawItem& item = items[itemIndex];
        GpuMeshInstance instance;
        if (!BuildGpuMeshInstance(item.model, item.linearTint, item.uvRect, instance))
        {
            ++rejected;
            continue;
        }

        instances.push_back(instance);
    }

    return rejected;
}

void ParticleRenderer::BuildTrailInstances(const DrawBatch& batch, const List<DrawItem>& items, List<GpuTrailInstance>& instances)
{
    instances.clear();
    instances.reserve(batch.items.size());
    for (uint32 itemIndex : batch.items)
    {
        if (itemIndex >= items.size()) continue;
        const DrawItem& item = items[itemIndex];
        if (item.sourceIndex >= trailSegments.size()) continue;

        const ParticleTrailSegment& segment = trailSegments[item.sourceIndex];
        GpuTrailInstance instance;
        //四个角点按列填入 corners 数组，顺序是 startLeft、startRight、endRight、endLeft
        for (uint32 corner = 0; corner < 4; ++corner)
        {
            instance.corners[corner * 4 + 0] = segment.corners[corner].x;
            instance.corners[corner * 4 + 1] = segment.corners[corner].y;
            instance.corners[corner * 4 + 2] = segment.corners[corner].z;
            instance.corners[corner * 4 + 3] = 1.0f;
        }
        instance.startColor[0] = segment.startColor.r;
        instance.startColor[1] = segment.startColor.g;
        instance.startColor[2] = segment.startColor.b;
        instance.startColor[3] = segment.startColor.a;
        instance.endColor[0] = segment.endColor.r;
        instance.endColor[1] = segment.endColor.g;
        instance.endColor[2] = segment.endColor.b;
        instance.endColor[3] = segment.endColor.a;
        instance.uvRange[0] = segment.u0;
        instance.uvRange[1] = segment.u1;
        instance.uvRange[2] = 0.0f;
        instance.uvRange[3] = 1.0f;
        instances.push_back(instance);
    }
}

void ParticleRenderer::ExpandBatch(const DrawBatch& batch, const List<DrawItem>& items,
    List<ExpandedGeometryChunk>& chunks, GpuResourceManager& resources)
{
    chunks.clear();
    ExpandedGeometryChunk chunk;

    auto flushChunk = [&]()
    {
        if (chunk.vertices.empty()) return;
        chunks.push_back(std::move(chunk));
        chunk = ExpandedGeometryChunk();
    };

    for (uint32 itemIndex : batch.items)
    {
        if (itemIndex >= items.size()) continue;
        const DrawItem& item = items[itemIndex];

        //展开顶点写入世界空间，一个源三角形必须完整落在同一个 chunk 里
        auto appendTriangle = [&](const GpuExpandedVertex& a, const GpuExpandedVertex& b, const GpuExpandedVertex& c)
        {
            if (chunk.vertices.size() + 3 > MaximumExpandedVertices || chunk.indices.size() + 3 > MaximumExpandedIndices)
            {
                flushChunk();
            }
            uint32 base = static_cast<uint32>(chunk.vertices.size());
            chunk.vertices.push_back(a);
            chunk.vertices.push_back(b);
            chunk.vertices.push_back(c);
            chunk.indices.push_back(base);
            chunk.indices.push_back(base + 1);
            chunk.indices.push_back(base + 2);
        };

        if (item.geometry == DrawGeometry::TrailQuad)
        {
            if (item.sourceIndex >= trailSegments.size()) continue;
            const ParticleTrailSegment& segment = trailSegments[item.sourceIndex];
            GpuExpandedVertex corners[4];
            for (uint32 corner = 0; corner < 4; ++corner)
            {
                corners[corner].position[0] = segment.corners[corner].x;
                corners[corner].position[1] = segment.corners[corner].y;
                corners[corner].position[2] = segment.corners[corner].z;
                corners[corner].normal[0] = 0.0f;
                corners[corner].normal[1] = 0.0f;
                corners[corner].normal[2] = 1.0f;
            }
            //纹理 U 沿长度、V 横跨宽度；角点顺序与实例路径的 quad uv 约定一致。
            //展开路径写好的结果必须与实例路径的 (mix(u0,u1,uv.y), uv.x) 完全一致。
            const float32 lateral[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
            const float32 longitudinal[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            const color colors[4] = { segment.startColor, segment.startColor, segment.endColor, segment.endColor };
            for (uint32 corner = 0; corner < 4; ++corner)
            {
                corners[corner].uv[0] = longitudinal[corner] > 0.5f ? segment.u1 : segment.u0;
                corners[corner].uv[1] = lateral[corner];
                corners[corner].tint[0] = colors[corner].r;
                corners[corner].tint[1] = colors[corner].g;
                corners[corner].tint[2] = colors[corner].b;
                corners[corner].tint[3] = colors[corner].a;
            }

            appendTriangle(corners[0], corners[1], corners[2]);
            appendTriangle(corners[0], corners[2], corners[3]);
            continue;
        }

        if (item.geometry == DrawGeometry::BillboardQuad)
        {
            const float32 quadPositions[4][3] =
            {
                { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f }, { 0.5f, 0.5f, 0.0f }, { -0.5f, 0.5f, 0.0f },
            };
            const float32 quadUvs[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
            GpuExpandedVertex corners[4];
            for (uint32 corner = 0; corner < 4; ++corner)
            {
                vector3 world = RenderMath::TransformPoint(item.model,
                    { quadPositions[corner][0], quadPositions[corner][1], quadPositions[corner][2] });
                corners[corner].position[0] = world.x;
                corners[corner].position[1] = world.y;
                corners[corner].position[2] = world.z;
                //四边形法线朝观察者，展开顶点已经是世界空间
                vector3 normal = RenderMath::Normalize({ item.model.m[8], item.model.m[9], item.model.m[10] });
                corners[corner].normal[0] = normal.x;
                corners[corner].normal[1] = normal.y;
                corners[corner].normal[2] = normal.z;
                //图集变换在 CPU 完成，展开顶点不再二次变换
                corners[corner].uv[0] = quadUvs[corner][0] * item.uvRect.b + item.uvRect.r;
                corners[corner].uv[1] = quadUvs[corner][1] * item.uvRect.a + item.uvRect.g;
                corners[corner].tangent[0] = item.model.m[0];
                corners[corner].tangent[1] = item.model.m[1];
                corners[corner].tangent[2] = item.model.m[2];
                corners[corner].tint[0] = item.linearTint.r;
                corners[corner].tint[1] = item.linearTint.g;
                corners[corner].tint[2] = item.linearTint.b;
                corners[corner].tint[3] = item.linearTint.a;
            }

            appendTriangle(corners[0], corners[1], corners[2]);
            appendTriangle(corners[0], corners[2], corners[3]);
            continue;
        }

        Mesh* mesh = item.mesh;
        if (!mesh) continue;
        if (!resources.GetMesh(mesh)) continue;

        //单个来源自身超过单批上限时不跨块拆分，明确报错跳过，不静默少画
        uint32 itemVertices = GeometryExpander::CountReferencedVertices(*mesh, item.indexStart, item.indexCount);
        if (itemVertices > MaximumExpandedVertices || item.indexCount > MaximumExpandedIndices)
        {
            Log::Error("ParticleRenderer mesh geometry skipped: the source submesh exceeds the single-chunk limits.");
            continue;
        }

        //先腾出放得下这一项的空间，保证一个来源完整落在同一个 chunk 里
        if (chunk.vertices.size() + itemVertices > MaximumExpandedVertices ||
            chunk.indices.size() + item.indexCount > MaximumExpandedIndices)
        {
            flushChunk();
        }

        //Mesh 粒子的展开与普通动态批、静态缓存共用同一份实现，两条路径必须一模一样
        if (!GeometryExpander::AppendExpandedMesh(*mesh, item.subMeshIndex, item.model, item.linearTint,
            item.uvRect, chunk.vertices, chunk.indices))
        {
            continue;
        }
    }

    flushChunk();
}
