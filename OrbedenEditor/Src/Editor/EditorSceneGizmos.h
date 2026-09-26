#pragma once

#include "Editor/EditorGizmoHandles.h"
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
    mutable std::unordered_map<uint64, MeshLines> meshes;
public:
    bool enabled = true;
    bool lights = true;
    bool colliders = true;
    bool allColliders = false;

    /// <summary>按当前视口和选择绘制方向光与碰撞体。</summary>
    void Draw(World& world, const EditorScene& scene, const EditorGizmoView& view) const;
};
