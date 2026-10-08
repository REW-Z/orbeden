#pragma once

#include "Rendering/DrawBatchBuilder.h"

class Mesh;

//公共的网格展开：把子网格按世界矩阵写成世界空间顶点与重定位索引，
//供普通动态合批、静态几何缓存与粒子 Mesh 路径共用。Billboard 与拖尾各有自己的几何生成。
namespace GeometryExpander
{
    //统计一个索引区间去重后的引用顶点数量，用于动态批预算
    uint32 CountReferencedVertices(const Mesh& mesh, uint32 indexStart, uint32 indexCount);

    //追加一个子网格的展开几何。索引、矩阵与容量任一不合法就整体失败，不留下部分追加数据。
    bool AppendExpandedMesh(const Mesh& mesh, uint32 subMeshIndex, const matrix4x4& model, const color& tint,
        const color& uvRect, List<GpuExpandedVertex>& vertices, List<uint32>& indices);
}
