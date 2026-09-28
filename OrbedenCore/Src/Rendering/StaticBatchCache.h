#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/DrawBatchBuilder.h"
#include "Runtime/EngineTypes.h"
#include "Runtime/EnsId.h"

class GpuResourceManager;
class Mesh;
class World;

//静态几何缓存里的一条成员：某个渲染器的一个子网格在组内的顶点与索引区间
struct StaticBatchMember
{
public:
    int32 rendererId = 0;
    EnsId owner;
    uint32 subMeshIndex = 0;
    int32 sourceMeshId = 0;
    matrix4x4 model;
    bounds3 worldBounds;
    //成员展开结果在组缓冲区里的区间
    uint32 vertexStart = 0;
    uint32 vertexCount = 0;
    uint32 indexStart = 0;
    uint32 indexCount = 0;
};

//一组兼容的静态几何，共用一份展开后的顶点与索引
struct StaticBatchGroup
{
public:
    //分组键：材质、Pass program、队列与解析后的固定状态，再加阴影与层
    int32 materialObjectId = 0;
    uint32 programId = 0;
    uint32 queue = 0;
    uint32 depthTest = 0;
    uint32 depthWrite = 0;
    uint32 blend = 0;
    uint32 cull = 0;
    bool receiveShadows = true;
    bool castShadows = false;
    uint32 drawLayer = 1u;

    List<StaticBatchMember> members;
    List<GpuExpandedVertex> vertices;
    List<uint32> indices;
    GpuVertexBufferID vertexBuffer;
    GpuIndexBufferID indexBuffer;
    GpuVertexInputID vertexInput;

    //CPU 侧资源引用快照，提交绘制时重新解析
    Ref<Material> material;
    Ref<Mesh> firstMesh;
    //已上传几何对应的成员签名，一致就不重建
    uint64 builtSignature = 0;
};

//静态几何缓存：把满足条件的 static 不透明网格合并成持久展开批，
//运行时每帧刷新受影响的组，绘制时按成员包围盒裁剪出可见索引范围。
class StaticBatchCache
{
private:
    RenderBackend* backend = nullptr;
    List<StaticBatchGroup> groups;
    //世界内容整体替换序号，变化时整批重建
    uint64 boundContentRevision = 0;
    World* boundWorld = nullptr;

    //本帧被静态批收编的渲染器编号，普通项构建时据此跳过
    List<int32> cachedRenderers;

    //释放一个组的 GPU 资源并清空 CPU 数据
    void ReleaseGroup(StaticBatchGroup& group);

    //展开并上传一个组的顶点与索引；成功后替换旧组
    bool BuildGroup(StaticBatchGroup& group, uint64 signature);

public:
    /// <summary>绑定渲染后端；不创建 GPU 资源。</summary>
    bool Initialize(RenderBackend* value);

    /// <summary>释放全部组的 GPU 资源，保留容器容量。</summary>
    void Shutdown();

    /// <summary>每帧刷新：重新收集候选、比对成员与内容版本，重建受影响的组并删除空组。</summary>
    void Refresh(World& world, GpuResourceManager& resources);

    /// <summary>某个渲染器是否已经被静态批收编。</summary>
    bool ContainsRenderer(int32 rendererId) const;

    /// <summary>按视锥与层掩码追加持久批的可见绘制项。</summary>
    void AppendVisibleItems(const frustum& viewFrustum, uint32 drawLayerMask, List<DrawItem>& items) const;

    /// <summary>按级联视锥追加投影用的持久批绘制项；只收开启投影的组。</summary>
    void AppendShadowItems(const frustum& lightFrustum, uint32 drawLayerMask, List<DrawItem>& items) const;

    /// <summary>读取指定下标的组，越界返回空。</summary>
    const StaticBatchGroup* GetGroup(usize index) const;

    /// <summary>按视锥与层掩码收集一个组的可见索引范围，成对写入 (indexStart,indexCount)。</summary>
    void CollectVisibleRanges(const StaticBatchGroup& group, const frustum& viewFrustum, uint32 drawLayerMask,
        List<uint32>& ranges) const;
};
