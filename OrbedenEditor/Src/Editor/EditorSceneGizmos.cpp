#include "Editor/EditorSceneGizmos.h"
#include "Editor/EditorScene.h"
#include "Physics/PhysicsSystem.h"
#include "Rendering/RenderMath.h"
#include "Runtime/World.h"
#include "Runtime/Object/Ens.h"
#include "Runtime/Object/Transform.h"
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

    /// <summary>对向量做带权偏移。</summary>
    vector3 Offset(vector3 a, vector3 b, float32 scale) { return {a.x + b.x * scale, a.y + b.y * scale, a.z + b.z * scale}; }

    /// <summary>绘制经过齐次视锥裁剪的线框，暗色衬线提升复杂背景上的辨识度。</summary>
    struct WireDrawer
    {
        const EditorGizmoView& view;
        ImDrawList* draw;
        ImU32 ink;
        float32 thickness = 1.4f;
        int32 remaining = 60000;

        /// <summary>裁剪世界线段到视锥后映射到 SceneView。</summary>
        void Line(vector3 a, vector3 b)
        {
            if (remaining-- <= 0) return;
            const float32* m = view.viewProjection.m;
            float32 p[4] = {}, q[4] = {};
            for (int32 row = 0; row < 4; ++row)
            {
                p[row] = m[row] * a.x + m[row + 4] * a.y + m[row + 8] * a.z + m[row + 12];
                q[row] = m[row] * b.x + m[row + 4] * b.y + m[row + 8] * b.z + m[row + 12];
                if (!std::isfinite(p[row]) || !std::isfinite(q[row])) return;
            }
            float32 first = 0.0f, last = 1.0f;
            for (int32 axis = 0; axis < 3; ++axis)
                for (float32 sign : {-1.0f, 1.0f})
                {
                    float32 da = p[3] + sign * p[axis], db = q[3] + sign * q[axis];
                    if (da < 0.0f && db < 0.0f) return;
                    if (da < 0.0f) first = std::max(first, da / (da - db));
                    if (db < 0.0f) last = std::min(last, da / (da - db));
                }
            if (first > last) return;
            ImVec2 screen[2];
            for (int32 index = 0; index < 2; ++index)
            {
                float32 t = index == 0 ? first : last;
                float32 w = p[3] + (q[3] - p[3]) * t;
                if (w <= 0.000001f) return;
                screen[index] = ImVec2(view.renderPosition.x + ((p[0] + (q[0] - p[0]) * t) / w + 1.0f) * view.renderSize.x * 0.5f,
                    view.renderPosition.y + (1.0f - (p[1] + (q[1] - p[1]) * t) / w) * view.renderSize.y * 0.5f);
            }
            draw->AddLine(screen[0], screen[1], IM_COL32(10, 20, 15, 140), thickness + 1.6f);
            draw->AddLine(screen[0], screen[1], ink, thickness);
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
}

/// <summary>按组件真实几何和当前变换绘制编辑器辅助线。</summary>
void EditorSceneGizmos::Draw(World& world, const EditorScene& scene, const EditorGizmoView& view) const
{
    if (!enabled || !view.valid) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImVec2(view.renderPosition.x, view.renderPosition.y),
        ImVec2(view.renderPosition.x + view.renderSize.x, view.renderPosition.y + view.renderSize.y), true);
    WireDrawer wire{view, draw, IM_COL32(90, 230, 135, 245)};
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
                wire.ink = active && light->GetEnabled() ? IM_COL32(255, 213, 112, 245) : IM_COL32(150, 145, 120, 180);
                wire.thickness = selected ? 1.8f : 1.3f;
                matrix4x4 identity = RenderMath::TRS({}, {}, {1, 1, 1});
                wire.Arc(identity, center, view.cameraRight, view.cameraUp, size * 0.5f);
                for (int32 ray = 0; ray < 8; ++ray)
                {
                    float32 angle = ray * Pi / 4;
                    vector3 radial = Offset(Offset({}, view.cameraRight, std::cos(angle)), view.cameraUp, std::sin(angle));
                    wire.Line(Offset(center, radial, size * 0.72f), Offset(center, radial, size));
                }
                vector3 direction = RenderMath::Normalize(light->direction);
                if (RenderMath::Dot(direction, direction) < 0.5f) continue;
                vector3 side = RenderMath::Normalize(RenderMath::Cross(direction, std::abs(direction.y) < 0.9f ? vector3{0,1,0} : vector3{1,0,0}));
                for (int32 ray = selected ? -1 : 0; ray <= (selected ? 1 : 0); ++ray)
                {
                    vector3 start = Offset(center, side, ray * size * 0.6f);
                    vector3 end = Offset(start, direction, size * (selected ? 3.0f : 2.0f));
                    wire.Line(start, end);
                    wire.Line(end, Offset(Offset(end, direction, -size * 0.45f), side, size * 0.22f));
                    wire.Line(end, Offset(Offset(end, direction, -size * 0.45f), side, -size * 0.22f));
                }
            }
            Collider* collider = component->Cast<Collider>();
            if (!colliders || !collider || (!allColliders && !selected)) continue;
            wire.ink = active && collider->enabled ? IM_COL32(90, 230, 135, selected ? 245 : 130) : IM_COL32(105, 155, 120, 140);
            wire.thickness = selected ? 1.5f : 1.0f;
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
                    for (int32 bit : {1,2,4}) if (!(index & bit)) wire.Line(corners[index], corners[index | bit]);
            }
            else if (SphereCollider* sphere = collider->Cast<SphereCollider>())
            {
                float32 radius = std::max(std::abs(sphere->radius) * std::max({scale.x, scale.y, scale.z, 0.001f}), 0.001f);
                wire.Arc(pose, {}, {1,0,0}, {0,1,0}, radius);
                wire.Arc(pose, {}, {1,0,0}, {0,0,1}, radius);
                wire.Arc(pose, {}, {0,1,0}, {0,0,1}, radius);
            }
            else if (CapsuleCollider* capsule = collider->Cast<CapsuleCollider>())
            {
                float32 radius = std::max(std::abs(capsule->radius) * std::max({scale.x, scale.z, 0.001f}), 0.001f);
                float32 half = std::max(std::abs(capsule->halfHeight * scale.y), 0.001f);
                for (float32 sign : {-1.0f, 1.0f})
                {
                    wire.Arc(pose, {0, sign * half, 0}, {1,0,0}, {0,0,1}, radius);
                    wire.Arc(pose, {0, sign * half, 0}, {1,0,0}, {0,sign,0}, radius, 0, Pi);
                    wire.Arc(pose, {0, sign * half, 0}, {0,0,1}, {0,sign,0}, radius, 0, Pi);
                    wire.Line(RenderMath::TransformPoint(pose, {sign*radius,-half,0}), RenderMath::TransformPoint(pose, {sign*radius,half,0}));
                    wire.Line(RenderMath::TransformPoint(pose, {0,-half,sign*radius}), RenderMath::TransformPoint(pose, {0,half,sign*radius}));
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
                for (usize index = 0; index + 1 < cached.lines.size() && wire.remaining > 0; index += 2)
                    wire.Line(RenderMath::TransformPoint(meshPose,cached.lines[index]), RenderMath::TransformPoint(meshPose,cached.lines[index+1]));
            }
        }
    });
    draw->PopClipRect();
}
