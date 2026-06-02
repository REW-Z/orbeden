#include "Runtime/World.h"

#include "Runtime/ResourceManager.h"
#include "Runtime/SpaceComponent.h"

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

    //生成组件映射Key
    uint64 GetComponentKey(EnsId ens, Type* type)
    {
        return (static_cast<uint64>(ens.id) << 32) | static_cast<uint64>(type->GetId());
    }
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

    for (uint32 index = 0; index < enses.size(); ++index)
    {
        const Ens& storedEns = enses[index];
        if (!storedEns.alive) continue;

        DestroyEns(storedEns.ens);
    }

    //再销毁独立世界对象
    while (!ownedObjects.empty())
    {
        Object* object = ownedObjects.back();
        ownedObjects.pop_back();
        Object::DeleteInstance(object);
    }

    enses.clear();
    freeEnsIds.clear();
    componentByEnsAndType.clear();
    componentsByType.clear();
    nextEnsIndex = 1;
    nextRuntimeObjectIndex = 1;
}

//创建Ens
Ens World::CreateEns(const std::string& name)
{
    std::string instancePath;
    do
    {
        instancePath = "world://ens/" + std::to_string(nextEnsIndex++);
    } while (Object::FindObject(StringId(instancePath)));

    return CreateEnsInternal(name, instancePath);
}

//使用稳定ID创建Ens
Ens World::CreateEnsWithStableId(const std::string& stableId, const std::string& name)
{
    if (stableId.empty()) return CreateEns(name);
    if (Object::FindObject(StringId(stableId))) return Ens();

    return CreateEnsInternal(name, stableId);
}

//使用指定稳定ID创建Ens
Ens World::CreateEnsInternal(const std::string& name, const std::string& stableId)
{
    //分配Ens句柄
    EnsId value;
    Ens* storedEns = nullptr;
    if (!freeEnsIds.empty())
    {
        value.id = freeEnsIds.back();
        freeEnsIds.pop_back();

        value.version = enses[value.id].ens.version + 1;
        enses[value.id] = Ens(this, value);
        storedEns = &enses[value.id];
    }
    else
    {
        value.id = static_cast<uint32>(enses.size());
        value.version = 1;
        enses.push_back(Ens(this, value));
        storedEns = &enses.back();
    }

    storedEns->alive = true;

    Object* object = Object::CreateInstance(SpaceComponent::StaticType(), stableId);
    SpaceComponent* space = object ? object->Cast<SpaceComponent>() : nullptr;
    if (!space)
    {
        storedEns->alive = false;
        storedEns->name.clear();
        storedEns->componentMask = 0;
        storedEns->componentTypes.clear();
        storedEns->space = nullptr;
        freeEnsIds.push_back(value.id);
        if (object)
        {
            Object::DeleteInstance(object);
        }
        return Ens();
    }

    space->SetWorld(this);
    space->SetEnsId(value);

    storedEns->name = name;
    storedEns->space = space;
    storedEns->AddComponentType(SpaceComponent::StaticType());
    componentByEnsAndType[GetComponentKey(value, SpaceComponent::StaticType())] = space;
    componentsByType[SpaceComponent::StaticType()->GetId()].push_back(space);
    return Ens(this, value);
}

//销毁Ens
bool World::DestroyEns(EnsId ens)
{
    Ens* storedEns = GetEns(ens);
    if (!storedEns) return false;

    SpaceComponent* space = storedEns->space;
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

    //注销空间组件
    componentByEnsAndType.erase(GetComponentKey(ens, SpaceComponent::StaticType()));

    auto componentListIt = componentsByType.find(SpaceComponent::StaticType()->GetId());
    if (componentListIt != componentsByType.end())
    {
        List<Component*>& componentList = componentListIt->second;
        componentList.erase(std::remove(componentList.begin(), componentList.end(), space), componentList.end());
        if (componentList.empty())
        {
            componentsByType.erase(componentListIt);
        }
    }

    Object::DeleteInstance(space);

    storedEns->name.clear();
    storedEns->componentMask = 0;
    storedEns->componentTypes.clear();
    storedEns->space = nullptr;
    storedEns->alive = false;
    freeEnsIds.push_back(ens.id);
    return true;
}

//获取World内部Ens数据
Ens* World::GetEns(EnsId ens)
{
    if (ens.IsNull()) return nullptr;
    if (ens.id >= enses.size()) return nullptr;

    Ens& storedEns = enses[ens.id];
    if (!storedEns.alive) return nullptr;
    if (storedEns.ens.version != ens.version) return nullptr;

    return &storedEns;
}

//获取World内部Ens数据
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
    const Ens* storedEns = GetEns(ens);
    return storedEns ? storedEns->space : nullptr;
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
Ens World::GetParent(EnsId child) const
{
    SpaceComponent* space = GetSpaceComponent(child);
    return space ? Ens(const_cast<World*>(this), space->parent) : Ens();
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
    Object* object = Object::CreateInstance(type, instancePath);
    Component* component = object ? object->Cast<Component>() : nullptr;
    if (!component)
    {
        if (object)
        {
            Object::DeleteInstance(object);
        }
        return nullptr;
    }

    component->SetWorld(this);
    component->SetEnsId(ens);

    componentByEnsAndType[GetComponentKey(ens, type)] = component;
    componentsByType[type->GetId()].push_back(component);
    Ens* storedEns = GetEns(ens);
    if (storedEns) storedEns->AddComponentType(type);
    component->OnAttach();
    return component;
}

//获取组件
Component* World::GetComponent(EnsId ens, Type* type) const
{
    if (!type) return nullptr;
    if (type == SpaceComponent::StaticType()) return GetSpaceComponent(ens);
    if (!IsAlive(ens)) return nullptr;

    auto it = componentByEnsAndType.find(GetComponentKey(ens, type));
    if (it == componentByEnsAndType.end()) return nullptr;

    return it->second;
}

//移除组件
bool World::RemoveComponent(EnsId ens, Type* type)
{
    SpaceComponent* space = GetSpaceComponent(ens);
    if (!space || !type || type == SpaceComponent::StaticType()) return false;

    auto componentIt = componentByEnsAndType.find(GetComponentKey(ens, type));
    if (componentIt == componentByEnsAndType.end()) return false;

    //先执行卸载回调
    Component* component = componentIt->second;
    component->OnDetach();

    //再移除索引和对象
    componentByEnsAndType.erase(componentIt);

    auto typeListIt = componentsByType.find(type->GetId());
    if (typeListIt != componentsByType.end())
    {
        List<Component*>& componentList = typeListIt->second;
        componentList.erase(std::remove(componentList.begin(), componentList.end(), component), componentList.end());
        if (componentList.empty())
        {
            componentsByType.erase(typeListIt);
        }
    }

    Ens* storedEns = GetEns(ens);
    if (storedEns) storedEns->RemoveComponentType(type);
    Object::DeleteInstance(component);
    return true;
}

//创建世界内对象
Object* World::CreateObject(Type* type, const std::string& stableId)
{
    if (!type) return nullptr;
    if (type->Is(Component::StaticType())) return nullptr;

    //确定稳定ID
    std::string instancePath = stableId;
    if (instancePath.empty())
    {
        do
        {
            instancePath = "world://runtime/" + std::to_string(nextRuntimeObjectIndex++);
        } while (Object::FindObject(StringId(instancePath)));
    }
    else if (Object::FindObject(StringId(instancePath)))
    {
        return nullptr;
    }

    //创建并登记对象
    Object* object = Object::CreateInstance(type, instancePath);
    if (!object) return nullptr;

    object->SetWorld(this);
    ownedObjects.push_back(object);
    return object;
}

//销毁世界内对象
bool World::DestroyObject(Object* object)
{
    if (!object || object->GetWorld() != this) return false;
    if (object->Is(Component::StaticType())) return false;

    auto it = std::find(ownedObjects.begin(), ownedObjects.end(), object);
    if (it == ownedObjects.end()) return false;

    ownedObjects.erase(it);
    return Object::DeleteInstance(object);
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
Ens World::FindEns(const StringId& id) const
{
    Object* object = Object::FindObject(id);
    SpaceComponent* space = object ? object->Cast<SpaceComponent>() : nullptr;
    if (!space) return Ens();
    if (space->GetWorld() != this) return Ens();
    if (!IsAlive(space->GetEnsId())) return Ens();

    return Ens(const_cast<World*>(this), space->GetEnsId());
}
