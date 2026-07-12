#pragma once

#include "Editor/EditorSelection.h"
#include "Rendering/RenderScene.h"
#include "Runtime/World.h"

#include <unordered_map>

//编辑器选中对象的纯 ImGui 屏幕空间轮廓。
class EditorSelectionOutline
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
        uint64 revision = 0;
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

    std::unordered_map<Mesh*, List<MeshTopology>> topologyCache;
    List<vector3> worldVerticesScratch;
    List<ClipPoint> clipVerticesScratch;
    List<int8> faceOrientationsScratch;
    uint64 frameIndex = 0;

public:
    /// <summary>绘制当前选择及其后代的屏幕空间轮廓。</summary>
    void Draw(const RenderScene& scene, World& world, EnsId cameraEns, const EditorSelection& selection,
        const vector2& workspacePosition, const vector2& workspaceSize);

    /// <summary>清空网格拓扑缓存。</summary>
    void Clear();

private:
    /// <summary>获取与网格版本和有效索引范围匹配的拓扑缓存。</summary>
    const MeshTopology& GetTopology(Mesh* mesh, const List<IndexRange>& ranges);

    /// <summary>计算顶点的齐次裁剪空间坐标。</summary>
    static ClipPoint TransformClip(const matrix4x4& matrix, const vector3& point);

    /// <summary>将齐次裁剪空间线段裁剪到六个视锥平面内。</summary>
    static bool ClipLine(ClipPoint& a, ClipPoint& b);
};
