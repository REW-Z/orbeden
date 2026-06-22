#include "Runtime/ComponentStorage.h"

#include <cassert>

//创建指定类型的World组件稀疏集
ComponentStorage::ComponentStorage(World* world, Type* type)
    : ownerWorld(world), componentType(type)
{
    assert(ownerWorld);
    assert(componentType);
    assert(componentType->Is(Component::StaticType()));
}

//获取组件类型
Type* ComponentStorage::GetType() const
{
    return componentType;
}

//登记Ens拥有的组件
bool ComponentStorage::Add(EnsId owner, Component* component)
{
    if (owner.IsNull() || !component) return false;
    if (component->GetType() != componentType) return false;
    if (component->GetWorld() != ownerWorld) return false;
    if (component->GetEnsId() != owner) return false;

    if (owner.id >= sparseSlots.size())
    {
        sparseSlots.resize(static_cast<usize>(owner.id) + 1);
    }

    SparseSlot& slot = sparseSlots[owner.id];
    if (slot.denseIndex != EnsId::InvalidId) return false;

    slot.denseIndex = static_cast<uint32>(components.size());
    slot.ensVersion = owner.version;
    owners.push_back(owner);
    components.push_back(component);
    return true;
}

//查找Ens拥有的组件
Component* ComponentStorage::Get(EnsId owner) const
{
    if (owner.IsNull() || owner.id >= sparseSlots.size()) return nullptr;

    const SparseSlot& slot = sparseSlots[owner.id];
    if (slot.denseIndex == EnsId::InvalidId) return nullptr;
    if (slot.ensVersion != owner.version) return nullptr;
    if (slot.denseIndex >= components.size()) return nullptr;
    if (owners[slot.denseIndex] != owner) return nullptr;

    return components[slot.denseIndex];
}

//移除并返回Ens拥有的组件
Component* ComponentStorage::Remove(EnsId owner)
{
    Component* component = Get(owner);
    if (!component) return nullptr;

    SparseSlot& slot = sparseSlots[owner.id];
    uint32 removeIndex = slot.denseIndex;
    uint32 lastIndex = static_cast<uint32>(components.size() - 1);

    //用末尾元素填补空位并修正其稀疏索引
    if (removeIndex != lastIndex)
    {
        EnsId movedOwner = owners[lastIndex];
        owners[removeIndex] = movedOwner;
        components[removeIndex] = components[lastIndex];
        sparseSlots[movedOwner.id].denseIndex = removeIndex;
    }

    owners.pop_back();
    components.pop_back();
    slot.denseIndex = EnsId::InvalidId;
    slot.ensVersion = 0;
    return component;
}

//获取存活组件数量
uint32 ComponentStorage::GetCount() const
{
    assert(owners.size() == components.size());
    return static_cast<uint32>(components.size());
}
