#include "Runtime/World.h"

#include "Memory/MemoryManager.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/Object/SpaceComponent.h"

#include <algorithm>
#include <cassert>

namespace
{
    //获取当前活动世界指针
    World*& GetCurrentWorldStorage()
    {
        static World* currentWorld = nullptr;
        return currentWorld;
    }
}

//查找组件稀疏集
ComponentStorage* World::FindComponentStorage(Type* type) const
{
    if (!type || type->GetId() >= componentStorages.size()) return nullptr;

    ComponentStorage* storage = componentStorages[type->GetId()];
    return storage && storage->GetType() == type ? storage : nullptr;
}

//获取或创建组件稀疏集
ComponentStorage* World::GetOrCreateComponentStorage(Type* type)
{
    if (!type || !type->Is(Component::StaticType())) return nullptr;

    TypeId typeId = type->GetId();
    if (typeId >= componentStorages.size())
    {
        componentStorages.resize(static_cast<usize>(typeId) + 1, nullptr);
    }

    ComponentStorage*& storage = componentStorages[typeId];
    if (!storage)
    {
        storage = NEW(ComponentStorage)ComponentStorage(this, type);
    }

    return storage;
}

//生成Ens对象ID
std::string World::AllocateEnsObjectPath()
{
    std::string instancePath;
    do
    {
        instancePath = "world://ens/" + Object::GenerateUuidText();
    } while (Object::FindObject(StringId(instancePath)));

    return instancePath;
}

//生成未命名Ens的名称
std::string World::ResolveEnsName(const std::string& name) const
{
    if (!name.empty()) return name;

    auto nameExists = [this](const std::string& value)
        {
            for (Ens* ens : liveEns)
            {
                if (ens && ens->alive && ens->name == value) return true;
            }

            return false;
        };

    constexpr const char* baseName = "Unnamed";
    if (!nameExists(baseName)) return baseName;

    uint32 index = 1;
    std::string resolvedName;
    do
    {
        resolvedName = std::string(baseName) + "(" + std::to_string(index++) + ")";
    } while (nameExists(resolvedName));

    return resolvedName;
}

//生成世界运行时对象ID
std::string World::AllocateRuntimeObjectPath(Type* type)
{
    return Object::CreateRuntimeInstancePath("world://runtime", type);
}

//接收世界拥有的运行时对象
bool World::AddOwnedObject(Object* object)
{
    if (!object) return false;
    if (object->GetWorld() != this) return false;
    if (object->GetOwnership() != Object::Ownership::WorldOwned) return false;
    if (object->Is(Component::StaticType())) return false;
    if (std::find(ownedObjects.begin(), ownedObjects.end(), object) != ownedObjects.end()) return true;

    ownedObjects.push_back(object);
    return true;
}

//摘除世界拥有的运行时对象
bool World::RemoveOwnedObject(Object* object)
{
    auto it = std::find(ownedObjects.begin(), ownedObjects.end(), object);
    if (it == ownedObjects.end()) return false;

    ownedObjects.erase(it);
    return true;
}

//获取当前活动世界
World* World::CurrentWorld()
{
    return GetCurrentWorldStorage();
}

//设置当前活动世界
void World::SetCurrentWorld(World* world)
{
    GetCurrentWorldStorage() = world;
}

//销毁世界及其运行时对象
World::~World()
{
    Clear();

    if (CurrentWorld() == this)
    {
        SetCurrentWorld(nullptr);
    }
}

//清空世界运行时对象
void World::Clear()
{
    ReleaseSceneResourceRefs();

    while (!liveEns.empty())
    {
        DestroyEns(liveEns.back()->GetId());
    }

    //再销毁独立世界对象
    while (!ownedObjects.empty())
    {
        Object* object = ownedObjects.back();
        ownedObjects.pop_back();
        object->SetWorld(nullptr);
        object->SetOwnership(Object::Ownership::None);
        Object::DestroyDetachedInstance(object);
    }

    //所有Ens销毁后释放空的组件稀疏集
    for (ComponentStorage*& storage : componentStorages)
    {
        assert(!storage || storage->GetCount() == 0);
        DELETE(storage);
    }

    ensSlots.clear();
    liveEns.clear();
    freeEnsIds.clear();
    componentStorages.clear();
    renderSettings = RenderSettings();
}

//创建Ens
Ens* World::CreateEns(const std::string& name)
{
    return CreateEnsInternal(ResolveEnsName(name), AllocateEnsObjectPath());
}

//使用稳定ID创建Ens
Ens* World::CreateEnsWithStableId(const std::string& stableId, const std::string& name)
{
    if (stableId.empty()) return CreateEns(name);
    if (Object::FindObject(StringId(stableId))) return nullptr;

    return CreateEnsInternal(ResolveEnsName(name), stableId);
}

//使用指定稳定ID创建Ens
Ens* World::CreateEnsInternal(const std::string& name, const std::string& stableId)
{
    //分配Ens句柄
    EnsId value;
    EnsSlot* slot = nullptr;
    Ens* storedEns = nullptr;
    if (!freeEnsIds.empty())
    {
        value.id = freeEnsIds.back();
        freeEnsIds.pop_back();

        slot = &ensSlots[value.id];
        value.version = slot->version + 1;
        if (value.version == 0) value.version = 1;
        slot->version = value.version;
    }
    else
    {
        value.id = static_cast<uint32>(ensSlots.size());
        value.version = 1;
        ensSlots.push_back(EnsSlot());
        slot = &ensSlots.back();
        slot->version = value.version;
    }

    storedEns = NEW(Ens)Ens(this, value);
    slot->value = storedEns;
    slot->denseIndex = static_cast<uint32>(liveEns.size());
    liveEns.push_back(storedEns);

    Object* object = Object::CreateRawInstance(SpaceComponent::StaticType(), stableId);
    SpaceComponent* space = object ? object->Cast<SpaceComponent>() : nullptr;
    if (!space)
    {
        storedEns->alive = false;
        liveEns.pop_back();
        slot->value = nullptr;
        slot->denseIndex = EnsId::InvalidId;
        storedEns->~Ens();
        Memory::GetHeapAllocator()->Deallocate(reinterpret_cast<std::byte*>(storedEns));
        freeEnsIds.push_back(value.id);
        if (object)
        {
            Object::DestroyDetachedInstance(object);
        }
        return nullptr;
    }

    space->SetWorld(this);
    space->SetOwnership(Object::Ownership::WorldOwned);
    space->SetEnsId(value);

    ComponentStorage* spaceStorage = GetOrCreateComponentStorage(SpaceComponent::StaticType());
    bool spaceAdded = spaceStorage && spaceStorage->Add(value, space);
    assert(spaceAdded);
    if (!spaceAdded)
    {
        space->SetEnsId(EnsId());
        space->SetWorld(nullptr);
        space->SetOwnership(Object::Ownership::None);
        Object::DestroyDetachedInstance(space);
        storedEns->alive = false;
        liveEns.pop_back();
        slot->value = nullptr;
        slot->denseIndex = EnsId::InvalidId;
        storedEns->~Ens();
        Memory::GetHeapAllocator()->Deallocate(reinterpret_cast<std::byte*>(storedEns));
        freeEnsIds.push_back(value.id);
        return nullptr;
    }

    storedEns->name = name;
    storedEns->AddComponentType(SpaceComponent::StaticType());
    return storedEns;
}

//销毁Ens
bool World::DestroyEns(EnsId ens)
{
    Ens* storedEns = GetEns(ens);
    if (!storedEns) return false;

    SpaceComponent* space = GetSpaceComponent(ens);
    if (!space) return false;

    //先解除子级关系
    EnsId child = space->firstChild;
    while (!child.IsNull())
    {
        SpaceComponent* childSpace = GetSpaceComponent(child);
        EnsId nextChild = childSpace ? childSpace->next : EnsId();
        SetParent(child, EnsId());
        child = nextChild;
    }

    //再从父级摘除
    SetParent(ens, EnsId());

    //销毁额外组件
    List<TypeId> componentTypes = storedEns->componentTypes;
    for (TypeId typeId : componentTypes)
    {
        Type* type = Object::FindType(typeId);
        if (type && type != SpaceComponent::StaticType())
        {
            RemoveComponent(ens, type);
        }
    }

    //注销并销毁空间组件
    ComponentStorage* spaceStorage = FindComponentStorage(SpaceComponent::StaticType());
    Component* removedSpace = spaceStorage ? spaceStorage->Remove(ens) : nullptr;
    assert(removedSpace == space);
    space->SetEnsId(EnsId());
    space->SetWorld(nullptr);
    space->SetOwnership(Object::Ownership::None);
    bool spaceDeleted = Object::DestroyDetachedInstance(space);
    assert(spaceDeleted);

    storedEns->alive = false;

    EnsSlot& slot = ensSlots[ens.id];
    assert(slot.denseIndex < liveEns.size());
    assert(liveEns[slot.denseIndex] == storedEns);

    //从紧凑存活列表中移除，并修正换入Ens的索引
    Ens* movedEns = liveEns.back();
    liveEns[slot.denseIndex] = movedEns;
    liveEns.pop_back();
    if (movedEns != storedEns)
    {
        ensSlots[movedEns->GetId().id].denseIndex = slot.denseIndex;
    }

    slot.value = nullptr;
    slot.denseIndex = EnsId::InvalidId;
    storedEns->~Ens();
    Memory::GetHeapAllocator()->Deallocate(reinterpret_cast<std::byte*>(storedEns));

    freeEnsIds.push_back(ens.id);
    return true;
}

//获取World持有的唯一Ens实例
Ens* World::GetEns(EnsId ens)
{
    if (ens.IsNull()) return nullptr;
    if (ens.id >= ensSlots.size()) return nullptr;

    EnsSlot& slot = ensSlots[ens.id];
    if (!slot.value) return nullptr;
    if (slot.version != ens.version) return nullptr;
    if (!slot.value->alive) return nullptr;

    return slot.value;
}

//获取World持有的唯一Ens实例
const Ens* World::GetEns(EnsId ens) const
{
    return const_cast<World*>(this)->GetEns(ens);
}

//判断Ens是否存活
bool World::IsAlive(EnsId ens) const
{
    return GetEns(ens) != nullptr;
}

//获取空间组件
SpaceComponent* World::GetSpaceComponent(EnsId ens) const
{
    if (!IsAlive(ens)) return nullptr;

    ComponentStorage* storage = FindComponentStorage(SpaceComponent::StaticType());
    Component* component = storage ? storage->Get(ens) : nullptr;
    return component ? component->Cast<SpaceComponent>() : nullptr;
}

//设置父级
void World::SetParent(EnsId child, EnsId parent)
{
    SpaceComponent* space = GetSpaceComponent(child);
    if (!space) return;
    if (child == parent) return;
    if (!parent.IsNull() && !IsAlive(parent)) return;

    //避免形成循环
    EnsId current = parent;
    while (!current.IsNull())
    {
        if (current == child) return;

        SpaceComponent* currentSpace = GetSpaceComponent(current);
        current = currentSpace ? currentSpace->parent : EnsId();
    }

    //从旧父级摘除
    SpaceComponent* oldParent = GetSpaceComponent(space->parent);
    SpaceComponent* previous = GetSpaceComponent(space->prev);
    SpaceComponent* next = GetSpaceComponent(space->next);

    if (oldParent && oldParent->firstChild == child) oldParent->firstChild = space->next;
    if (oldParent && oldParent->lastChild == child) oldParent->lastChild = space->prev;
    if (previous) previous->next = space->next;
    if (next) next->prev = space->prev;

    space->parent = EnsId();
    space->prev = EnsId();
    space->next = EnsId();

    if (parent.IsNull()) return;

    //挂到新父级末尾
    SpaceComponent* parentSpace = GetSpaceComponent(parent);
    if (!parentSpace) return;

    SpaceComponent* lastChild = GetSpaceComponent(parentSpace->lastChild);
    space->parent = parent;
    space->prev = parentSpace->lastChild;

    if (lastChild)
    {
        lastChild->next = child;
    }
    else
    {
        parentSpace->firstChild = child;
    }

    parentSpace->lastChild = child;
}

//获取父级
Ens* World::GetParent(EnsId child) const
{
    SpaceComponent* space = GetSpaceComponent(child);
    return space ? const_cast<World*>(this)->GetEns(space->parent) : nullptr;
}

//添加组件
Component* World::AddComponent(EnsId ens, Type* type)
{
    SpaceComponent* space = GetSpaceComponent(ens);
    if (!space || !type || !type->Is(Component::StaticType())) return nullptr;
    if (type == SpaceComponent::StaticType()) return space;

    Component* oldComponent = GetComponent(ens, type);
    if (oldComponent) return oldComponent;

    //创建并注册组件
    std::string instancePath = space->GetInstanceId().GetPath() + "/" + type->GetName();
    Object* object = Object::CreateRawInstance(type, instancePath);
    Component* component = object ? object->Cast<Component>() : nullptr;
    if (!component)
    {
        if (object)
        {
            Object::DestroyDetachedInstance(object);
        }
        return nullptr;
    }

    component->SetWorld(this);
    component->SetOwnership(Object::Ownership::WorldOwned);
    component->SetEnsId(ens);

    ComponentStorage* storage = GetOrCreateComponentStorage(type);
    bool added = storage && storage->Add(ens, component);
    assert(added);
    if (!added)
    {
        component->SetEnsId(EnsId());
        component->SetWorld(nullptr);
        component->SetOwnership(Object::Ownership::None);
        Object::DestroyDetachedInstance(component);
        return nullptr;
    }

    Ens* storedEns = GetEns(ens);
    if (storedEns) storedEns->AddComponentType(type);
    component->OnAttach();
    return component;
}

//获取组件
Component* World::GetComponent(EnsId ens, Type* type) const
{
    if (!type || !IsAlive(ens)) return nullptr;

    ComponentStorage* storage = FindComponentStorage(type);
    return storage ? storage->Get(ens) : nullptr;
}

//移除组件
bool World::RemoveComponent(EnsId ens, Type* type)
{
    SpaceComponent* space = GetSpaceComponent(ens);
    if (!space || !type || type == SpaceComponent::StaticType()) return false;

    ComponentStorage* storage = FindComponentStorage(type);
    Component* component = storage ? storage->Get(ens) : nullptr;
    if (!component) return false;

    //先执行卸载回调
    component->OnDetach();

    //再移除索引和对象
    Component* removedComponent = storage->Remove(ens);
    assert(removedComponent == component);

    Ens* storedEns = GetEns(ens);
    if (storedEns) storedEns->RemoveComponentType(type);
    component->SetEnsId(EnsId());
    component->SetWorld(nullptr);
    component->SetOwnership(Object::Ownership::None);
    bool deleted = Object::DestroyDetachedInstance(component);
    assert(deleted);
    return deleted;
}

//增加一个场景资源引用
bool World::AddSceneResourceRef(Type* type, const std::string& key)
{
    Object* object = ResourceManager::LoadSceneRef(type, key);
    if (!object) return false;

    sceneResourceRefs.push_back(ResourceManager::NormalizeKey(key));
    return true;
}

//释放当前World持有的所有场景资源引用
void World::ReleaseSceneResourceRefs()
{
    for (const std::string& key : sceneResourceRefs)
    {
        ResourceManager::ReleaseSceneRef(key);
    }

    sceneResourceRefs.clear();
}

//按稳定ID查找Ens
Ens* World::FindEns(const StringId& id) const
{
    Object* object = Object::FindObject(id);
    SpaceComponent* space = object ? object->Cast<SpaceComponent>() : nullptr;
    if (!space) return nullptr;
    if (space->GetWorld() != this) return nullptr;

    return const_cast<World*>(this)->GetEns(space->GetEnsId());
}

//遍历所有存活的Ens
void World::VisitEns(EnsVisitorFunction visitor, void* userData) const
{
    if (!visitor) return;

    for (Ens* ens : liveEns)
    {
        if (!ens || !ens->alive) continue;

        visitor(ens, userData);
    }
}
