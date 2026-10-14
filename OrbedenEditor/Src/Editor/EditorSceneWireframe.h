#pragma once

#include "Editor/EditorSelectionOutline.h"
#include "Rendering/Backend/OpenGLRenderBackend.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/OutputPass.h"
#include "Rendering/ParticleRenderer.h"

#include <unordered_map>

class RenderSystem;

//编辑器线框画面与统一几何提交
class EditorSceneWireframe
{
    struct OutlineMesh
    {
        Ref<Mesh> source;
        Ref<Mesh> copy;
        uint64 version = 0;
    };

    OpenGLRenderBackend backend;
    GpuResourceManager resources;
    OutputPass output;
    EditorSelectionOutline outline;
    ParticleRenderer particles;
    GpuShaderProgramID program;
    GpuVertexBufferID vertexBuffer;
    GpuIndexBufferID indexBuffer;
    GpuVertexInputID vertexInput;
    GpuRenderTargetID sceneTarget;
    GpuRenderTargetID displayTarget;
    GpuDepthTextureID depthTexture;
    int32 width = 0;
    int32 height = 0;
    bool initialized = false;
    List<vector3> vertices;
    List<EditorSelectionOutline::SelectionHighlight> selectionHighlights;
    std::unordered_map<int32, OutlineMesh> outlineMeshes;

public:
    //绘制独立线框或叠加线框并返回显示纹理
    GpuTextureID Render(IWindow* window, World& world, RenderSystem& renderSystem,
        RenderCamera& camera, bool overlay);

    //设置独立线框画面的选中描边
    void SetHighlights(const List<EditorSelectionOutline::SelectionHighlight>& highlights);

    //释放编辑器线框资源
    void ReleaseResources();

private:
    //创建硬编码 Shader 和统一绘制缓冲
    bool PrepareResources(IWindow* window);

    //提交一块统一线框几何
    void ExecuteBatch();

    //追加一组世界空间三角形
    void AppendMesh(const Mesh& mesh, uint32 indexStart, uint32 indexCount, const matrix4x4& model);

    //追加一个世界空间三角形
    void AppendTriangle(const vector3& first, const vector3& second, const vector3& third);

    //获取描边专用的网格副本
    Mesh* GetOutlineMesh(Mesh* source);
};
