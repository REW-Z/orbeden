#pragma once

#include "Rendering/InstanceDrawData.h"
#include "Runtime/Object/Object.h"

//显式实例提交列表：把一批 TRS 记录交给下一次 Render 绘制。
//它不是组件也不是资源，用 Object.CreateInstance<InstanceDrawList>() 创建。
class InstanceDrawList : public Object
{
    OBJECT_TYPE_DECLARE(InstanceDrawList)

private:
    //以下字段全部只存在于运行时，不参与序列化，也不进 Prefab 与场景文件
    Ref<Mesh> mesh;
    Ref<Material> material;
    uint32 subMeshIndex = 0;
    InstanceDrawOptions options;
    List<MeshInstanceData> instances;

public:
    /// <summary>配置绘制目标；校验失败时保留旧配置，且不清空已设置的实例。</summary>
    bool Configure(Mesh* targetMesh, uint32 targetSubMeshIndex, Material* targetMaterial, const InstanceDrawOptions& drawOptions);

    /// <summary>替换全部实例数据；任一条非法则整体拒绝并保留旧值。</summary>
    ORBEDEN_BIND_BUFFER(data, count)
    bool SetInstances(const MeshInstanceData* data, int32 count);

    /// <summary>读取当前实例数量。</summary>
    int32 GetInstanceCount() const;

    /// <summary>把配置与实例复制到下一次 Render 的提交列表。</summary>
    bool Submit();

    /// <summary>清空对象内实例，不撤回已经提交的快照。</summary>
    void Clear();
};
