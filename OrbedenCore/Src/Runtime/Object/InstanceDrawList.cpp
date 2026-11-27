#include "Runtime/Object/InstanceDrawList.h"

#include "Log/Log.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/World.h"

#include <cmath>

OBJECT_TYPE_IMPLEMENT(InstanceDrawList, Object)

namespace
{
    //判断浮点数组是否全部有限
    bool IsFiniteVector3(const vector3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool IsFiniteColor(const color& value)
    {
        return std::isfinite(value.r) && std::isfinite(value.g) && std::isfinite(value.b) && std::isfinite(value.a);
    }
}

//检查有限数值、旋转长度和缩放
bool MeshInstanceData::IsValid() const
{
    float32 rotationLengthSquared = rotation.x * rotation.x + rotation.y * rotation.y
        + rotation.z * rotation.z + rotation.w * rotation.w;
    return IsFiniteVector3(position) && IsFiniteVector3(scale) && IsFiniteColor(tint) && IsFiniteColor(uvRect)
        && std::isfinite(rotationLengthSquared) && rotationLengthSquared > 1.0e-8f
        && std::fabs(scale.x) > 1.0e-8f && std::fabs(scale.y) > 1.0e-8f && std::fabs(scale.z) > 1.0e-8f;
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

    //显式实例提交不支持多 Pass
    Shader* shader = targetMaterial->shader.Get();
    if (!shader || shader->passes.size() != 1) return false;

    mesh.Set(targetMesh);
    material.Set(targetMaterial);
    subMeshIndex = targetSubMeshIndex;
    options = drawOptions;
    drawQueue = targetMaterial->GetDrawQueue();
    drawSubQueue = targetMaterial->drawSubQueue;
    return true;
}

//替换全部实例数据
bool InstanceDrawList::SetInstances(const MeshInstanceData* data, int32 count)
{
    if (count < 0 || static_cast<uint32>(count) > MaximumSubmittedInstances) return false;
    if (count > 0 && !data) return false;

    if (count == 0)
    {
        Clear();
        return true;
    }

    const bool countChanged = instances.size() != static_cast<usize>(count);
    instances.assign(data, data + count);
    if (countChanged)
    {
        drawParameters.clear();
    }

    return true;
}

//替换实例列表的着色参数
bool InstanceDrawList::SetDrawParameters(const float32* data, int32 count)
{
    if (count == 0)
    {
        drawParameters.clear();
        return true;
    }
    if (!data || count < 0 || static_cast<usize>(count) != instances.size() * DrawParameterFloatCount)
    {
        return false;
    }

    drawParameters.assign(data, data + count);
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

    return renderSystem->SubmitInstances(*owner, options, mesh.Get(), subMeshIndex, material.Get(), instances,
        GetObjectId(), drawParameters, drawQueue, drawSubQueue, true);
}

//清空对象内实例
void InstanceDrawList::Clear()
{
    instances.clear();
    drawParameters.clear();
}
