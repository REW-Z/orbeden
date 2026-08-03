#pragma once

#include "Editor/EditorLayoutState.h"
#include "Rendering/RenderScene.h"
#include "Runtime/EnsId.h"

#include <string>
#include <unordered_map>

class Application;
class ManagedEditorBridge;
class Mesh;
class PanelManager;
class World;

//Editor Gizmo 三维向量，布局与 C# Orbeden.vector3 一致。
struct EditorGizmoVector3
{
public:
    float32 x = 0.0f;
    float32 y = 0.0f;
    float32 z = 0.0f;
};

//Editor Gizmo 颜色，布局与 C# Orbeden.color4 一致。
struct EditorGizmoColor
{
public:
    float32 r = 1.0f;
    float32 g = 1.0f;
    float32 b = 1.0f;
    float32 a = 1.0f;
};

//Editor Gizmo 原生函数表，传给 C# Editor 保存。
struct EditorGizmoApi
{
public:
    void* Line3D = nullptr;
    void* Label3D = nullptr;
};

//编辑器背景场景，负责相机、选择、轮廓和 Handles 绘制。
class EditorScene
{
private:
    struct IndexRange
    {
    public:
        uint32 start = 0;
        uint32 count = 0;

        /// <summary>判断两个索引范围是否一致。</summary>
        bool operator==(const IndexRange& other) const
        {
            return start == other.start && count == other.count;
        }
    };

    struct TopologyTriangle
    {
    public:
        uint32 a = 0;
        uint32 b = 0;
        uint32 c = 0;
    };

    struct TopologyEdge
    {
    public:
        uint32 a = 0;
        uint32 b = 0;
        uint32 faceOffset = 0;
        uint32 faceCount = 0;
    };

    struct MeshTopology
    {
    public:
        int32 objectId = 0;
        uint64 instanceHash = 0;
        usize vertexCount = 0;
        usize indexCount = 0;
        uint64 lastUsedFrame = 0;
        List<IndexRange> ranges;
        List<vector3> vertices;
        List<TopologyTriangle> triangles;
        List<TopologyEdge> edges;
        List<uint32> edgeFaces;
    };

    struct ClipPoint
    {
    public:
        float32 x = 0.0f;
        float32 y = 0.0f;
        float32 z = 0.0f;
        float32 w = 1.0f;
    };

    Application& app;
    PanelManager& panelManager;
    ManagedEditorBridge& managedBridge;
    List<EnsId> selectedEns;
    EnsId activeEns;
    EditorCameraState cameraState;
    EnsId cameraEns;
    float32 cameraYaw = 35.0f;
    float32 cameraPitch = -22.0f;
    float32 cameraMoveSpeed = 5.0f;
    bool cameraMouseDragging = false;
    int32 cameraMouseMode = 0;
    double previousMouseX = 0.0;
    double previousMouseY = 0.0;
    bool selectionPressed = false;
    bool selectionDragged = false;
    bool selectionCtrl = false;
    vector2 selectionStart = { 0.0f, 0.0f };
    std::unordered_map<Mesh*, List<MeshTopology>> topologyCache;
    List<vector3> worldVerticesScratch;
    List<ClipPoint> clipVerticesScratch;
    List<int8> faceOrientationsScratch;
    uint64 frameIndex = 0;
    matrix4x4 gizmoViewProjection;
    int32 gizmoViewportWidth = 0;
    int32 gizmoViewportHeight = 0;

public:
    /// <summary>创建编辑器背景场景。</summary>
    EditorScene(Application& application, PanelManager& panels, ManagedEditorBridge& bridge);

    /// <summary>更新编辑器观察相机。</summary>
    void Update(World& world, float32 deltaTime);

    /// <summary>绘制场景选择、轮廓和托管 Handles。</summary>
    void DrawBackground();

    /// <summary>取消当前鼠标交互。</summary>
    void CancelInteraction();

    /// <summary>清空选择和场景绘制缓存。</summary>
    void ClearSceneState();

    /// <summary>移除已经失效的选择对象。</summary>
    void PruneSelection(const World& world);

    /// <summary>选择一个 Ens。</summary>
    void SelectEns(EnsId ens);

    /// <summary>切换一个 Ens 的选择状态。</summary>
    void ToggleEns(EnsId ens);

    /// <summary>清空当前选择。</summary>
    void ClearSelection();

    /// <summary>获取当前活动选择。</summary>
    EnsId GetSelectedEns() const;

    /// <summary>判断指定 Ens 是否被选中。</summary>
    bool IsSelected(EnsId ens) const;

    /// <summary>获取当前活动选择的稳定 ID。</summary>
    std::string GetSelectedStableId() const;

    /// <summary>判断 Ens 是否属于编辑器临时场景对象。</summary>
    bool IsTemporaryEns(EnsId ens) const;

    /// <summary>把观察相机状态写入布局。</summary>
    void WriteLayout(EditorLayoutState& layout);

    /// <summary>应用布局中的观察相机状态。</summary>
    void ApplyLayout(const EditorLayoutState& layout, World& world);

    /// <summary>序列化场景前暂时移除编辑器相机。</summary>
    bool RemoveCameraForSerialization(World& world);

    /// <summary>恢复编辑器观察相机。</summary>
    void RestoreCamera(World& world);

    /// <summary>进入 Play 前移除临时相机并清理无效选择。</summary>
    void EnterPlayMode(World& world);

    /// <summary>退出 Play 后重置选择并恢复观察相机。</summary>
    void ExitPlayMode(World& world);

    /// <summary>获取托管 Handles 使用的原生函数表。</summary>
    EditorGizmoApi GetGizmoApi();

    /// <summary>获取当前 Handles 视图投影矩阵。</summary>
    const matrix4x4& GetGizmoViewProjection() const;

    /// <summary>获取当前 Handles 视口宽度。</summary>
    int32 GetGizmoViewportWidth() const;

    /// <summary>获取当前 Handles 视口高度。</summary>
    int32 GetGizmoViewportHeight() const;

private:
    //创建或修复编辑器观察相机。
    void CreateEditorCamera(World& world);

    //记录当前编辑器观察相机状态。
    void CaptureCameraState(World& world);

    //移除当前编辑器观察相机。
    void RemoveCamera(World& world);

    //处理中央工作区鼠标选择。
    void HandleSelection(const RenderScene& scene);

    //拾取鼠标下距离相机最近的场景对象。
    EnsId PickEns(const RenderScene& scene, const vector2& screenPosition) const;

    //绘制当前选择及其后代的屏幕空间轮廓。
    void DrawSelectionOutline(const RenderScene& scene, World& world,
        const vector2& workspacePosition, const vector2& workspaceSize);

    //清空网格拓扑缓存。
    void ClearTopologyCache();

    //获取与网格数据和有效索引范围匹配的拓扑缓存。
    const MeshTopology& GetTopology(Mesh* mesh, const List<IndexRange>& ranges);

    //计算顶点的齐次裁剪空间坐标。
    static ClipPoint TransformClip(const matrix4x4& matrix, const vector3& point);

    //将齐次裁剪空间线段裁剪到六个视锥平面内。
    static bool ClipLine(ClipPoint& a, ClipPoint& b);

    //绘制托管 Scene Handles。
    void DrawManagedGizmos();
};
