#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/DrawBatchBuilder.h"
#include "Rendering/InstanceDrawData.h"
#include "Rendering/RenderScene.h"

#include <span>

class GpuResourceManager;
struct GpuMesh;

//编辑器网格线框绘制
class EditorSceneWireframe
{
    struct MeshDraw
    {
        Mesh* mesh = nullptr;
        uint32 indexStart = 0;
        uint32 indexCount = 0;
        matrix4x4 model;
    };

    //主渲染后端的非拥有引用
    RenderBackend* backend = nullptr;
    GpuShaderProgramID program;
    GpuShaderProgramID instancedProgram;
    GpuVertexBufferID instanceBuffer;
    usize instanceCapacity = 0;
    List<MeshDraw> draws;
    List<GpuMeshInstance> instances;

public:
    //在相机场景缓冲绘制网格线框
    void Render(RenderBackend& renderBackend, GpuResourceManager& resources, World& world,
        const RenderCamera& camera, const VisibleSet& visibleSet,
        std::span<const InstanceSubmission> submissions, bool overlay);

    //释放线框 Shader、实例缓冲和临时数据
    void ReleaseResources();

private:
    //创建线框 Shader
    bool PrepareResources(RenderBackend& renderBackend);

    //追加一个网格绘制项
    void AppendMeshDraw(Mesh* mesh, uint32 indexStart, uint32 indexCount, const matrix4x4& model);

    //提交同一子网格的实例批次
    void DrawInstances(const GpuMesh& mesh, uint32 indexStart, uint32 indexCount,
        std::span<const MeshDraw> batch);
};
