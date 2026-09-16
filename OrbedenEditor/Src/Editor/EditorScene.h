#pragma once

#include "Editor/EditorLayoutState.h"
#include "Rendering/Backend/GpuResourceIDs.h"
#include "Rendering/RenderScene.h"
#include "Rendering/RenderTypes.h"
#include "Runtime/EnsId.h"
#include "Runtime/Native/NativeApiAbi.h"

#include <string>
#include <unordered_map>

class Application;
class ManagedEditorBridge;
class Mesh;
class World;

#pragma pack(push, 4)

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

#pragma pack(pop)

static_assert(std::is_standard_layout_v<EditorGizmoVector3> && std::is_trivially_copyable_v<EditorGizmoVector3>);
static_assert(sizeof(EditorGizmoVector3) == sizeof(float32) * 3 && alignof(EditorGizmoVector3) <= 4);
static_assert(offsetof(EditorGizmoVector3, x) == 0 && offsetof(EditorGizmoVector3, y) == sizeof(float32) && offsetof(EditorGizmoVector3, z) == sizeof(float32) * 2);

static_assert(std::is_standard_layout_v<EditorGizmoColor> && std::is_trivially_copyable_v<EditorGizmoColor>);
static_assert(sizeof(EditorGizmoColor) == sizeof(float32) * 4 && alignof(EditorGizmoColor) <= 4);
static_assert(offsetof(EditorGizmoColor, r) == 0 && offsetof(EditorGizmoColor, g) == sizeof(float32) && offsetof(EditorGizmoColor, b) == sizeof(float32) * 2 && offsetof(EditorGizmoColor, a) == sizeof(float32) * 3);

//Editor Gizmo 原生函数表，传给 C# Editor 保存。
#pragma pack(push, 8)
struct EditorGizmoApi
{
public:
    void* Line3D = nullptr;
    void* Label3D = nullptr;
};
#pragma pack(pop)

ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorGizmoApi, 2);

//场景视口的渲染矩形、可交互内容区与离屏目标像素尺寸。
struct EditorSceneViewState
{
public:
    vector2 renderPosition = { 0.0f, 0.0f };
    vector2 renderSize = { 0.0f, 0.0f };
    vector2 interactPosition = { 0.0f, 0.0f };
    vector2 interactSize = { 0.0f, 0.0f };
    int32 pixelWidth = 0;
    int32 pixelHeight = 0;
    bool visible = false;
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

    static EditorScene* activeScene;

    Application& app;
    ManagedEditorBridge& managedBridge;
    RenderTargetID sceneTarget;
    GpuTextureID sceneTargetTexture;
    int32 sceneTargetWidth = 0;
    int32 sceneTargetHeight = 0;
    EditorSceneViewState sceneView;
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

public:
    /// <summary>创建编辑器背景场景。</summary>
    EditorScene(Application& application, ManagedEditorBridge& bridge);

    /// <summary>释放场景视口的离屏目标。</summary>
    ~EditorScene();

    /// <summary>获取当前活动的编辑器场景。</summary>
    static EditorScene* GetActiveScene();

    /// <summary>更新编辑器观察相机。</summary>
    void Update(World& world, float32 deltaTime, float32 mouseWheel);

    /// <summary>按场景视口可见性维护离屏目标并绑定编辑相机。</summary>
    void RefreshSceneViewTarget(World& world);

    /// <summary>绘制原生场景视口图像并叠加轮廓与 Handles。</summary>
    void DrawSceneView();

    /// <summary>获取本帧场景视口矩形与像素尺寸。</summary>
    const EditorSceneViewState& GetSceneViewState() const;

    /// <summary>解析场景投放点：优先表面命中，其次地面交点，最后相机前方。</summary>
    bool ResolveSceneDropPosition(vector3& position) const;

    /// <summary>取消当前鼠标交互。</summary>
    void CancelInteraction();

    /// <summary>清空选择和场景绘制缓存。</summary>
    void ClearSceneState();

    /// <summary>移除已经失效的选择对象。</summary>
    void PruneSelection(const World& world);

    /// <summary>选择一个 Ens。</summary>
    void SelectEns(EnsId ens);

    //移动节点并按需保持世界变换
    bool MoveEns(World& world, EnsId child, EnsId parent, EnsId before, bool preserveWorld);

    /// <summary>切换一个 Ens 的选择状态。</summary>
    void ToggleEns(EnsId ens);

    /// <summary>清空当前选择。</summary>
    void ClearSelection();

    /// <summary>获取当前活动选择。</summary>
    EnsId GetSelectedEns() const;

    /// <summary>获取完整选择列表，顺序与用户选择顺序一致。</summary>
    const List<EnsId>& GetSelectedEnsList() const;

    /// <summary>判断指定 Ens 是否被选中。</summary>
    bool IsSelected(EnsId ens) const;

    /// <summary>获取当前活动选择的稳定 ID。</summary>
    std::string GetSelectedStableId() const;

    /// <summary>获取指定 Ens 的稳定 ID。</summary>
    std::string GetStableId(EnsId ens) const;

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

private:
    //创建或修复编辑器观察相机。
    void CreateEditorCamera(World& world);

    //在当前上下文的绘制列表上提交选择轮廓与托管 Handles。
    void DrawSceneOverlay();

    //判断鼠标是否位于场景视口矩形内。
    bool IsMouseOverSceneView() const;

    //释放场景视口的离屏目标。
    void ReleaseSceneViewTarget();

    //记录当前编辑器观察相机状态。
    void CaptureCameraState(World& world);

    //移除当前编辑器观察相机。
    void RemoveCamera(World& world);

    //处理中央工作区鼠标选择。
    void HandleSelection(const RenderScene& scene);

    //投射鼠标射线并返回最近命中的对象与命中点。
    bool RaycastScene(const RenderScene& scene, const vector2& screenPosition,
        EnsId& hitEns, vector3& hitPosition) const;

    //绘制当前选择及其后代的屏幕空间轮廓。
    void DrawSelectionOutline(const RenderScene& scene, World& world,
        const vector2& viewPosition, const vector2& viewSize);

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
