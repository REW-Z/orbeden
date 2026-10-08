#pragma once

#include "Editor/EditorGizmoHandles.h"
#include "Rendering/RenderScene.h"
#include <unordered_map>

class EditorScene;
class World;

/// <summary>SceneView 的内置组件 Gizmos；与变换手柄分别控制显示。</summary>
class EditorSceneGizmos
{
    struct MeshLines
    {
        uint64 hash = 0;
        List<vector3> lines;
    };
    //Gizmo 几何的输出口：绘制与拾取共用同一份几何生成，定义在源文件里
    struct Sink;
    mutable std::unordered_map<uint64, MeshLines> meshes;

    //把当前世界的组件 Gizmo 几何写进输出口
    void Emit(World& world, const EditorScene& scene, const EditorGizmoView& view,
        const RenderScene& renderScene, Sink& sink) const;
public:
    bool enabled = true;
    bool lights = true;
    bool cameras = true;
    bool colliders = true;
    bool allColliders = false;

    /// <summary>按当前视口和选择绘制方向光、相机与碰撞体。</summary>
    void Draw(World& world, const EditorScene& scene, const EditorGizmoView& view,
        const RenderScene& renderScene) const;

    /// <summary>按屏幕距离拾取 Gizmo，返回它所属的 Ens；没有网格的 Ens 只能这样点选。</summary>
    bool Pick(World& world, const EditorScene& scene, const EditorGizmoView& view,
        const RenderScene& renderScene, const vector2& screenPosition, EnsId& hitEns) const;
};
