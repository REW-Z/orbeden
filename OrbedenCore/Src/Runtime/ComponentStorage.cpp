#include "Runtime/ComponentStorage.h"

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
    if (slot.component) return nullptr;

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
    slot.component = component;
    slot.ensVersion = owner.version;
    componentCount++;
    return component;
}

//查找Ens拥有的组件
Component* ComponentStorage::Get(EnsId owner) const
{
    if (owner.IsNull() || owner.id >= sparseSlots.size()) return nullptr;

    const SparseSlot& slot = sparseSlots[owner.id];
    if (!slot.component) return nullptr;
    if (slot.ensVersion != owner.version) return nullptr;

    return slot.component;
}

//移除并返回Ens拥有的组件
Component* ComponentStorage::Remove(EnsId owner)
{
    Component* component = Get(owner);
    if (!component) return nullptr;

    SparseSlot& slot = sparseSlots[owner.id];
    slot.component = nullptr;
    slot.ensVersion = 0;
    assert(componentCount > 0);
    componentCount--;
    return component;
}

//获取存活组件数量
uint32 ComponentStorage::GetCount() const
{
    return componentCount;
}

//遍历当前Chunk中的组件
void ComponentStorage::VisitComponents(ComponentVisitorFunction visitor, void* userData) const
{
    if (!visitor) return;

    struct VisitorContext
    {
        ComponentVisitorFunction callback = nullptr;
        void* data = nullptr;
    };

    VisitorContext context{ visitor, userData };
    componentType->VisitChunkObjects(componentChunk, [](Object* object, void* contextData)
    {
        VisitorContext* visitorContext = static_cast<VisitorContext*>(contextData);
        Component* component = object ? object->Cast<Component>() : nullptr;
        if (!component) return;

        visitorContext->callback(component, visitorContext->data);
    }, &context);
}
