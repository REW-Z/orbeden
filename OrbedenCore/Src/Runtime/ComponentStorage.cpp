#include "Runtime/ComponentStorage.h"

#include <algorithm>
#include <cassert>

//创建指定类型的World组件稀疏集
ComponentStorage::ComponentStorage(World* world, Type* type)
    : ownerWorld(world), componentType(type)
{
    assert(ownerWorld);
    assert(componentType);
    assert(componentType->Is(Component::StaticType()));
    componentChunk = componentType->CreateWorldObjectChunk();
    assert(componentChunk);
}

//释放当前组件稀疏集
ComponentStorage::~ComponentStorage()
{
    assert(componentCount == 0);
    if (componentType)
    {
        componentType->DestroyWorldObjectChunk(componentChunk);
    }
}

//获取组件类型
Type* ComponentStorage::GetType() const
{
    return componentType;
}

//创建Ens拥有的组件
Component* ComponentStorage::Create(EnsId owner, const std::string& instancePath)
{
    if (owner.IsNull() || instancePath.empty()) return nullptr;

    if (owner.id >= sparseSlots.size())
    {
        sparseSlots.resize(static_cast<usize>(owner.id) + 1);
    }

    SparseSlot& slot = sparseSlots[owner.id];
    if (slot.ensVersion != 0 && slot.ensVersion != owner.version)
    {
        assert(slot.components.empty());
        slot.ensVersion = 0;
    }

    Object* object = Object::CreateRawInstance(componentType, instancePath, componentChunk);
    Component* component = object ? object->Cast<Component>() : nullptr;
    if (!component)
    {
        if (object)
        {
            Object::DestroyDetachedInstance(object);
        }

        return nullptr;
    }

    component->SetWorld(ownerWorld);
    component->SetOwnership(Object::Ownership::WorldOwned);
    component->SetEnsId(owner);
    slot.components.push_back(component);
    slot.ensVersion = owner.version;
    componentCount++;
    return component;
}

//查找Ens拥有的组件
Component* ComponentStorage::Get(EnsId owner) const
{
    if (owner.IsNull() || owner.id >= sparseSlots.size()) return nullptr;

    const SparseSlot& slot = sparseSlots[owner.id];
    if (slot.components.empty()) return nullptr;
    if (slot.ensVersion != owner.version) return nullptr;

    return slot.components.front();
}

//获取Ens拥有的全部组件
void ComponentStorage::GetAll(EnsId owner, List<Component*>& output) const
{
    output.clear();
    if (owner.IsNull() || owner.id >= sparseSlots.size()) return;

    const SparseSlot& slot = sparseSlots[owner.id];
    if (slot.ensVersion != owner.version) return;
    output = slot.components;
}

//移除并返回Ens拥有的首个组件
Component* ComponentStorage::Remove(EnsId owner)
{
    Component* component = Get(owner);
    return Remove(component);
}

//移除并返回指定组件实例
Component* ComponentStorage::Remove(Component* component)
{
    if (!component) return nullptr;
    EnsId owner = component->GetEnsId();
    if (owner.IsNull() || owner.id >= sparseSlots.size()) return nullptr;

    SparseSlot& slot = sparseSlots[owner.id];
    if (slot.ensVersion != owner.version) return nullptr;

    auto found = std::find(slot.components.begin(), slot.components.end(), component);
    if (found == slot.components.end()) return nullptr;

    slot.components.erase(found);
    if (slot.components.empty()) slot.ensVersion = 0;
    assert(componentCount > 0);
    componentCount--;
    return component;
}

//获取存活组件数量
uint32 ComponentStorage::GetCount() const
{
    return componentCount;
}
