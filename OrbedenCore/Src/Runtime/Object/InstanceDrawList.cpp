#include "Runtime/Object/InstanceDrawList.h"

#include "Log/Log.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/World.h"

#include <cmath>

OBJECT_TYPE_IMPLEMENT(InstanceDrawList, Object)

namespace
{
    //单次提交的实例数量上限，与 GPU 单批上限是两件事
    constexpr uint32 MaximumSubmittedInstances = 1048576u;

    //判断浮点数组是否全部有限
    bool IsFiniteVector3(const vector3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool IsFiniteColor(const color& value)
    {
        return std::isfinite(value.r) && std::isfinite(value.g) && std::isfinite(value.b) && std::isfinite(value.a);
    }

    //判断实例记录是否可用：数值有限、四元数非零、缩放非奇异
    bool IsValidInstanceData(const MeshInstanceData& instance)
    {
        if (!IsFiniteVector3(instance.position) || !IsFiniteVector3(instance.scale)) return false;
        if (!IsFiniteColor(instance.tint) || !IsFiniteColor(instance.uvRect)) return false;
        if (!std::isfinite(instance.rotation.x) || !std::isfinite(instance.rotation.y) ||
            !std::isfinite(instance.rotation.z) || !std::isfinite(instance.rotation.w)) return false;

        float32 rotationLengthSquared = instance.rotation.x * instance.rotation.x + instance.rotation.y * instance.rotation.y +
            instance.rotation.z * instance.rotation.z + instance.rotation.w * instance.rotation.w;
        if (!(rotationLengthSquared > 1.0e-8f)) return false;

        //任一轴接近零都会让世界矩阵不可逆，法线矩阵也就无从求起
        return std::fabs(instance.scale.x) > 1.0e-8f
            && std::fabs(instance.scale.y) > 1.0e-8f
            && std::fabs(instance.scale.z) > 1.0e-8f;
    }
}

//配置绘制目标
bool InstanceDrawList::Configure(Mesh* targetMesh, uint32 targetSubMeshIndex, Material* targetMaterial, const InstanceDrawOptions& drawOptions)
{
    if (!targetMesh || !targetMaterial) return false;
    if (targetSubMeshIndex >= targetMesh->subMeshes.size()) return false;

    const SubMesh& subMesh = targetMesh->subMeshes[targetSubMeshIndex];
    usize start = static_cast<usize>(subMesh.indexStart);
    usize count = static_cast<usize>(subMesh.indexCount);
    if (count == 0 || start > targetMesh->indices.size() || count > targetMesh->indices.size() - start) return false;

    //显式实例提交不支持多 Pass，也不支持没有几何 ABI 的 Legacy Shader
    Shader* shader = targetMaterial->shader.Get();
    if (!shader || shader->passes.size() != 1) return false;
    if (shader->passes[0].geometryContract == ShaderGeometryContract::Legacy) return false;

    mesh.Set(targetMesh);
    material.Set(targetMaterial);
    subMeshIndex = targetSubMeshIndex;
    options = drawOptions;
    return true;
}

//替换全部实例数据
bool InstanceDrawList::SetInstances(const MeshInstanceData* data, int32 count)
{
    if (count < 0 || static_cast<uint32>(count) > MaximumSubmittedInstances) return false;
    if (count > 0 && !data) return false;

    if (count == 0)
    {
        instances.clear();
        return true;
    }

    //先全量校验再复制，失败时不改动旧数据
    for (int32 index = 0; index < count; ++index)
    {
        if (!IsValidInstanceData(data[index])) return false;
    }

    instances.assign(data, data + count);
    //四元数在此归一化，之后的转换与提交都按单位四元数处理
    for (MeshInstanceData& instance : instances)
    {
        float32 length = std::sqrt(instance.rotation.x * instance.rotation.x + instance.rotation.y * instance.rotation.y +
            instance.rotation.z * instance.rotation.z + instance.rotation.w * instance.rotation.w);
        float32 inverse = 1.0f / length;
        instance.rotation.x *= inverse;
        instance.rotation.y *= inverse;
        instance.rotation.z *= inverse;
        instance.rotation.w *= inverse;
    }

    return true;
}

//读取当前实例数量
int32 InstanceDrawList::GetInstanceCount() const
{
    return static_cast<int32>(instances.size());
}

//把配置与实例复制到下一次 Render 的提交列表
bool InstanceDrawList::Submit()
{
    if (instances.empty()) return false;

    RenderSystem* renderSystem = RenderSystem::Current();
    if (!renderSystem) return false;

    //孤立对象没有所属世界，采用应用当前世界
    World* owner = GetWorld() ? GetWorld() : World::CurrentWorld();
    if (!owner || owner->IsPreparing()) return false;

    return renderSystem->SubmitInstances(*owner, options, mesh.Get(), subMeshIndex, material.Get(), instances, GetObjectId());
}

//清空对象内实例
void InstanceDrawList::Clear()
{
    instances.clear();
}
