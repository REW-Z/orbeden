#include "Editor/EditorSceneGizmos.h"
#include "Editor/EditorScene.h"
#include "Physics/PhysicsSystem.h"
#include "Rendering/RenderMath.h"
#include "Runtime/World.h"
#include "Runtime/Object/Ens.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/Object/Collider.h"
#include "Runtime/Object/DirectionalLight.h"
#include <imgui.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_set>

namespace
{
    constexpr float32 Pi = 3.14159265358979323846f;
    //Gizmo 的屏幕拾取半径：不小于方向光标记圆的半径，点在线围出的圆内也算命中
    constexpr float32 PickPixels = 8.0f;
    //相机视锥画到多长：沿屏幕方向折算成世界距离，视锥形状与真实投影一致、大小随缩放稳定
    constexpr float32 FrustumPixels = 110.0f;
    //近平面画在整条视锥的多远处：只影响显示，真实近平面照实画会塌成一个点
    constexpr float32 FrustumNearRatio = 0.35f;
    constexpr float32 DegenerateEpsilon = 0.0001f;

    /// <summary>对向量做带权偏移。</summary>
    vector3 Offset(vector3 a, vector3 b, float32 scale) { return {a.x + b.x * scale, a.y + b.y * scale, a.z + b.z * scale}; }

    //一条画到 SceneView 上的 Gizmo 屏幕线段，附它所属的 Ens
    struct PickSegment
    {
        EnsId ens;
        vector2 from;
        vector2 to;
    };

    //点到线段的屏幕距离
    float32 DistanceToSegment(const vector2& point, const vector2& a, const vector2& b)
    {
        float32 abx = b.x - a.x;
        float32 aby = b.y - a.y;
        float32 lengthSquared = abx * abx + aby * aby;
        float32 t = lengthSquared > DegenerateEpsilon
            ? std::clamp(((point.x - a.x) * abx + (point.y - a.y) * aby) / lengthSquared, 0.0f, 1.0f)
            : 0.0f;
        float32 dx = point.x - (a.x + abx * t);
        float32 dy = point.y - (a.y + aby * t);
        return std::sqrt(dx * dx + dy * dy);
    }
}

//Gizmo 几何的输出口：既是绘制目标，也是拾取线段的收集器。
//绘制与拾取走同一份几何生成，所以线框预算、开关与可见性门槛对两者一致。
struct EditorSceneGizmos::Sink
{
public:
    const EditorGizmoView& view;
    ImDrawList* draw = nullptr;
    ImU32 ink = 0;
    float32 thickness = 1.4f;
    int32 remaining = 60000;
    List<PickSegment>* picked = nullptr;
    EnsId owner;

    /// <summary>裁剪辅助线到场景视口后绘制或记录拾取线段。</summary>
    void Line(vector3 a, vector3 b)
    {
        if (remaining-- <= 0) return;
        vector2 screen[2];
        if (!EditorGizmoHandles::ProjectLine(view.viewProjection, view.renderPosition, view.renderSize,
            a, b, screen[0], screen[1])) return;
        if (picked) picked->push_back({owner, screen[0], screen[1]});
        if (!draw) return;
        draw->AddLine(ImVec2(screen[0].x, screen[0].y), ImVec2(screen[1].x, screen[1].y), ink, thickness);
    }

    /// <summary>绘制任意平面上的圆弧。</summary>
    void Arc(const matrix4x4& pose, vector3 center, vector3 right, vector3 up, float32 radius, float32 from = 0, float32 to = 2 * Pi)
    {
        vector3 previous = RenderMath::TransformPoint(pose, Offset(Offset(center, right, std::cos(from) * radius), up, std::sin(from) * radius));
        for (int32 step = 1; step <= 48; ++step)
        {
            float32 angle = from + (to - from) * step / 48.0f;
            vector3 point = RenderMath::TransformPoint(pose, Offset(Offset(center, right, std::cos(angle) * radius), up, std::sin(angle) * radius));
            Line(previous, point);
            previous = point;
        }
    }
};

//按组件真实几何和当前变换绘制编辑器辅助线。
void EditorSceneGizmos::Draw(World& world, const EditorScene& scene, const EditorGizmoView& view,
    const RenderScene& renderScene) const
{
    if (!enabled || !view.valid) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImVec2(view.renderPosition.x, view.renderPosition.y),
        ImVec2(view.renderPosition.x + view.renderSize.x, view.renderPosition.y + view.renderSize.y), true);
    Sink sink{view, draw, IM_COL32(90, 230, 135, 245)};
    Emit(world, scene, view, renderScene, sink);
    draw->PopClipRect();
}

//拾取屏幕位置下最近的一条 Gizmo 线段，返回它所属的 Ens。
bool EditorSceneGizmos::Pick(World& world, const EditorScene& scene, const EditorGizmoView& view,
    const RenderScene& renderScene, const vector2& screenPosition, EnsId& hitEns) const
{
    hitEns = EnsId();
    if (!enabled || !view.valid) return false;

    //只按屏幕距离判定：Gizmo 画在场景之上，点中画出来的线才符合所见即所得
    List<PickSegment> segments;
    Sink sink{view};
    sink.picked = &segments;
    Emit(world, scene, view, renderScene, sink);

    float32 bestDistance = PickPixels;
    for (const PickSegment& segment : segments)
    {
        float32 distance = DistanceToSegment(screenPosition, segment.from, segment.to);
        if (distance >= bestDistance) continue;
        bestDistance = distance;
        hitEns = segment.ens;
    }
    return !hitEns.IsNull();
}

//把当前世界的组件 Gizmo 几何写进输出口。
void EditorSceneGizmos::Emit(World& world, const EditorScene& scene, const EditorGizmoView& view,
    const RenderScene& renderScene, Sink& sink) const
{
    for (auto item = meshes.begin(); item != meshes.end();)
        if (!Object::FindObjectById(static_cast<int32>(item->first >> 1))) item = meshes.erase(item); else ++item;

    world.ForEachEns([&](Ens& ens)
    {
        if (scene.IsTemporaryEns(ens.GetId())) return;
        Transform* transform = world.GetTransform(ens.GetId());
        if (!transform) return;
        bool selected = scene.IsSelected(ens.GetId());
        bool active = ens.GetWorldActive();
        if (!active && !selected) return;
        //本 Ens 之后写的所有线段都归属它，拾取据此回推选中对象
        sink.owner = ens.GetId();
        matrix4x4 rotation = RenderMath::Rotation(transform->worldRotation);
        const float32* m = transform->worldMatrix.m;
        vector3 scale{std::sqrt(m[0]*m[0]+m[1]*m[1]+m[2]*m[2]), std::sqrt(m[4]*m[4]+m[5]*m[5]+m[6]*m[6]), std::sqrt(m[8]*m[8]+m[9]*m[9]+m[10]*m[10])};
        for (Component* component : ens.GetComponents())
        {
            if (DirectionalLight* light = component->Cast<DirectionalLight>(); lights && light)
            {
                //光照方向由 direction 决定，Transform 只提供标记位置。
                vector3 center = transform->worldPosition;
                vector2 screen, unit;
                if (!EditorGizmoHandles::ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, center, screen)
                    || !EditorGizmoHandles::ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, Offset(center, view.cameraRight, 1.0f), unit)) continue;
                float32 pixels = std::hypot(unit.x - screen.x, unit.y - screen.y);
                if (!std::isfinite(pixels) || pixels < 0.0001f) continue;
                float32 size = 15.0f / pixels;
                sink.ink = active && light->GetEnabled() ? IM_COL32(255, 213, 112, 245) : IM_COL32(150, 145, 120, 180);
                sink.thickness = selected ? 1.8f : 1.3f;
                matrix4x4 identity = RenderMath::TRS({}, {}, {1, 1, 1});
                sink.Arc(identity, center, view.cameraRight, view.cameraUp, size * 0.5f);
                for (int32 ray = 0; ray < 8; ++ray)
                {
                    float32 angle = ray * Pi / 4;
                    vector3 radial = Offset(Offset({}, view.cameraRight, std::cos(angle)), view.cameraUp, std::sin(angle));
                    sink.Line(Offset(center, radial, size * 0.72f), Offset(center, radial, size));
                }
                //方向取自同一个 Ens 的 Transform 前向，与渲染侧生成光照快照的取法一致
                vector3 direction = RenderMath::Normalize(
                    RenderMath::TransformDirection(transform->worldMatrix, { 0.0f, 0.0f, -1.0f }));
                if (RenderMath::Dot(direction, direction) < 0.5f) continue;
                vector3 side = RenderMath::Normalize(RenderMath::Cross(direction, std::abs(direction.y) < 0.9f ? vector3{0,1,0} : vector3{1,0,0}));
                for (int32 ray = selected ? -1 : 0; ray <= (selected ? 1 : 0); ++ray)
                {
                    vector3 start = Offset(center, side, ray * size * 0.6f);
                    vector3 end = Offset(start, direction, size * (selected ? 3.0f : 2.0f));
                    sink.Line(start, end);
                    sink.Line(end, Offset(Offset(end, direction, -size * 0.45f), side, size * 0.22f));
                    sink.Line(end, Offset(Offset(end, direction, -size * 0.45f), side, -size * 0.22f));
                }
            }
            if (Camera* camera = component->Cast<Camera>(); cameras && camera)
            {
                vector2 screen, unit;
                if (!EditorGizmoHandles::ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, transform->worldPosition, screen)
                    || !EditorGizmoHandles::ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize,
                        Offset(transform->worldPosition, view.cameraRight, 1.0f), unit)) continue;
                float32 pixels = std::hypot(unit.x - screen.x, unit.y - screen.y);
                if (!std::isfinite(pixels) || pixels < 0.0001f) continue;

                //视锥按屏幕上的固定长度绘制，长度夹在该相机真实的近/远平面之间：缩放时大小稳定，
                //远平面一千米的相机也不会在视口里铺出一大块线框
                float32 nearDistance = std::max(camera->nearPlane, 0.001f);
                float32 farDistance = std::max(camera->farPlane, nearDistance * 2.0f);
                float32 depth = std::clamp(FrustumPixels / pixels, nearDistance * 1.5f, farDistance);

                //近平面按显示距离画：真实近平面相对远平面往往只有万分之几（默认 0.1 对 1000），
                //照实画会塌成一个点，整条视锥看着像从相机位置发散的四棱锥而不是截头四棱锥
                float32 nearDisplay = depth * FrustumNearRatio;

                //宽高比取该相机实际渲染的视口，没注册进渲染场景时退回场景视口比例
                float32 aspect = view.renderSize.y > 0.0f ? view.renderSize.x / view.renderSize.y : 1.0f;
                for (const RenderCamera& registered : renderScene.cameras)
                    if (registered.ens == ens.GetId() && registered.viewportWidth > 0 && registered.viewportHeight > 0)
                    {
                        aspect = static_cast<float32>(registered.viewportWidth) / static_cast<float32>(registered.viewportHeight);
                        break;
                    }

                //视锥角的半高按渲染侧同一个公式展开，前向是局部 -Z
                float32 tangent = std::tan(std::clamp(camera->fieldOfView, 1.0f, 179.0f) * 0.5f * Pi / 180.0f);
                sink.ink = active && camera->GetEnabled() ? IM_COL32(120, 200, 240, 235) : IM_COL32(105, 130, 150, 170);
                sink.thickness = selected ? 1.7f : 1.2f;
                float32 nearHalfWidth = nearDisplay * tangent * aspect;
                float32 nearHalfHeight = nearDisplay * tangent;
                float32 farHalfWidth = depth * tangent * aspect;
                float32 farHalfHeight = depth * tangent;
                vector3 nearCorners[4];
                vector3 farCorners[4];
                for (int32 index = 0; index < 4; ++index)
                {
                    nearCorners[index] = RenderMath::TransformPoint(transform->worldMatrix,
                        {(index & 1) ? nearHalfWidth : -nearHalfWidth, (index & 2) ? nearHalfHeight : -nearHalfHeight, -nearDisplay});
                    farCorners[index] = RenderMath::TransformPoint(transform->worldMatrix,
                        {(index & 1) ? farHalfWidth : -farHalfWidth, (index & 2) ? farHalfHeight : -farHalfHeight, -depth});
                }
                //角点按位编号：index 与 index|bit 只差一位，连起来正好是矩形的四条边；
                //按周长顺序连会在矩形里连出两条对角线
                for (int32 index = 0; index < 4; ++index)
                    for (int32 bit : {1, 2})
                    {
                        if (index & bit) continue;
                        sink.Line(nearCorners[index], nearCorners[index | bit]);
                        sink.Line(farCorners[index], farCorners[index | bit]);
                    }
                //侧棱与镜筒四棱从相机位置本身引出，位置标记与点选都落在相机本体上
                for (int32 index = 0; index < 4; ++index)
                {
                    sink.Line(nearCorners[index], farCorners[index]);
                    sink.Line(transform->worldPosition, nearCorners[index]);
                }
            }
            Collider* collider = component->Cast<Collider>();
            if (!colliders || !collider || (!allColliders && !selected)) continue;
            sink.ink = active && collider->enabled ? IM_COL32(90, 230, 135, selected ? 245 : 130) : IM_COL32(105, 155, 120, 140);
            sink.thickness = selected ? 1.5f : 1.0f;
            vector3 center = RenderMath::TransformPoint(rotation, {collider->center.x * scale.x, collider->center.y * scale.y, collider->center.z * scale.z});
            center = Offset(transform->worldPosition, center, 1);
            matrix4x4 pose = RenderMath::TRS(center, transform->worldRotation, {1,1,1});
            if (BoxCollider* box = collider->Cast<BoxCollider>())
            {
                vector3 extent{std::max(std::abs(box->halfExtents.x * scale.x), 0.001f), std::max(std::abs(box->halfExtents.y * scale.y), 0.001f), std::max(std::abs(box->halfExtents.z * scale.z), 0.001f)};
                vector3 corners[8];
                for (int32 index = 0; index < 8; ++index)
                    corners[index] = RenderMath::TransformPoint(pose, {(index&1)?extent.x:-extent.x, (index&2)?extent.y:-extent.y, (index&4)?extent.z:-extent.z});
                for (int32 index = 0; index < 8; ++index)
                    for (int32 bit : {1,2,4}) if (!(index & bit)) sink.Line(corners[index], corners[index | bit]);
            }
            else if (SphereCollider* sphere = collider->Cast<SphereCollider>())
            {
                float32 radius = std::max(std::abs(sphere->radius) * std::max({scale.x, scale.y, scale.z, 0.001f}), 0.001f);
                sink.Arc(pose, {}, {1,0,0}, {0,1,0}, radius);
                sink.Arc(pose, {}, {1,0,0}, {0,0,1}, radius);
                sink.Arc(pose, {}, {0,1,0}, {0,0,1}, radius);
            }
            else if (CapsuleCollider* capsule = collider->Cast<CapsuleCollider>())
            {
                float32 radius = std::max(std::abs(capsule->radius) * std::max({scale.x, scale.z, 0.001f}), 0.001f);
                float32 half = std::max(std::abs(capsule->halfHeight * scale.y), 0.001f);
                for (float32 sign : {-1.0f, 1.0f})
                {
                    sink.Arc(pose, {0, sign * half, 0}, {1,0,0}, {0,0,1}, radius);
                    sink.Arc(pose, {0, sign * half, 0}, {1,0,0}, {0,sign,0}, radius, 0, Pi);
                    sink.Arc(pose, {0, sign * half, 0}, {0,0,1}, {0,sign,0}, radius, 0, Pi);
                    sink.Line(RenderMath::TransformPoint(pose, {sign*radius,-half,0}), RenderMath::TransformPoint(pose, {sign*radius,half,0}));
                    sink.Line(RenderMath::TransformPoint(pose, {0,-half,sign*radius}), RenderMath::TransformPoint(pose, {0,half,sign*radius}));
                }
            }
            else
            {
                bool convex = collider->Is(ConvexMeshCollider::StaticType());
                Mesh* mesh = convex ? static_cast<ConvexMeshCollider*>(collider)->mesh.Get()
                    : collider->Is(TriangleMeshCollider::StaticType()) ? static_cast<TriangleMeshCollider*>(collider)->mesh.Get() : nullptr;
                if (!mesh) continue;
                uint64 hash = 1469598103934665603ull;
                for (const vector3& vertex : mesh->vertices)
                    for (float32 value : {vertex.x,vertex.y,vertex.z}) hash = (hash ^ std::bit_cast<uint32>(value)) * 1099511628211ull;
                for (uint32 index : mesh->indices) hash = (hash ^ index) * 1099511628211ull;
                MeshLines& cached = meshes[(static_cast<uint64>(mesh->GetObjectId()) << 1) | (convex ? 1u : 0u)];
                if (cached.hash != hash)
                {
                    cached.hash = hash;
                    cached.lines.clear();
                    if (convex)
                    {
                        PhysicsSystem* physics = PhysicsSystem::Current();
                        if (!physics || !physics->IsInitialized()) cached.hash = 0;
                        else physics->GetConvexWireframe(*mesh, cached.lines);
                    }
                    else
                    {
                        std::unordered_set<uint64> edges;
                        for (usize triangle = 0; triangle + 2 < mesh->indices.size(); triangle += 3)
                            for (usize edge = 0; edge < 3; ++edge)
                            {
                                uint32 a = mesh->indices[triangle + edge], b = mesh->indices[triangle + (edge + 1) % 3];
                                if (a >= mesh->vertices.size() || b >= mesh->vertices.size() || a == b) continue;
                                if (a > b) std::swap(a,b);
                                if (edges.insert((static_cast<uint64>(a) << 32) | b).second)
                                { cached.lines.push_back(mesh->vertices[a]); cached.lines.push_back(mesh->vertices[b]); }
                            }
                    }
                }
                matrix4x4 meshPose = RenderMath::TRS(center, transform->worldRotation, scale);
                for (usize index = 0; index + 1 < cached.lines.size() && sink.remaining > 0; index += 2)
                    sink.Line(RenderMath::TransformPoint(meshPose,cached.lines[index]), RenderMath::TransformPoint(meshPose,cached.lines[index+1]));
            }
        }
    });
}
