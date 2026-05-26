#include "Runtime/World.h"

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
    for (uint32 index = 0; index < enses.size(); ++index)
    {
        const EnsRecord& record = enses[index];
        if (!record.alive) continue;

        EnsId value;
        value.id = index;
        value.version = record.version;
        DestroyEns(value);
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
    EnsRecord* record = nullptr;
    if (!freeEnsIds.empty())
    {
        value.id = freeEnsIds.back();
        freeEnsIds.pop_back();

        record = &enses[value.id];
        record->version++;
        record->alive = true;
        record->basic = nullptr;
        value.version = record->version;
    }
    else
    {
        value.id = static_cast<uint32>(enses.size());
        enses.push_back(EnsRecord());
        record = &enses.back();
        record->alive = true;
        value.version = record->version;
    }

    Object* object = Object::CreateInstance(EnsComponent::StaticType(), stableId);
    EnsComponent* basic = object ? object->Cast<EnsComponent>() : nullptr;
    if (!basic)
    {
        record->alive = false;
        freeEnsIds.push_back(value.id);
        if (object)
        {
            Object::DeleteInstance(object);
        }
        return Ens();
    }

    basic->SetWorld(this);
    basic->SetEnsId(value);
    basic->name = name;
    basic->AddComponentType(EnsComponent::StaticType());

    record->basic = basic;
    componentByEnsAndType[GetComponentKey(value, EnsComponent::StaticType())] = basic;
    componentsByType[EnsComponent::StaticType()->GetId()].push_back(basic);
    return Ens(this, value);
}

//销毁Ens
bool World::DestroyEns(EnsId ens)
{
    if (!IsAlive(ens)) return false;

    EnsComponent* basic = GetBasicComponent(ens);
    if (!basic) return false;

    //先解除子级关系
    EnsId child = basic->firstChild;
    while (!child.IsNull())
    {
        EnsComponent* childBasic = GetBasicComponent(child);
        EnsId nextChild = childBasic ? childBasic->next : EnsId();
        SetParent(child, EnsId());
        child = nextChild;
    }

    //再从父级摘除
    SetParent(ens, EnsId());

    //销毁额外组件
    List<TypeId> componentTypes = basic->componentTypes;
    for (TypeId typeId : componentTypes)
    {
        Type* type = Object::FindType(typeId);
        if (type && type != EnsComponent::StaticType())
        {
            RemoveComponent(ens, type);
        }
    }

    //注销基础组件
    componentByEnsAndType.erase(GetComponentKey(ens, EnsComponent::StaticType()));

    auto componentListIt = componentsByType.find(EnsComponent::StaticType()->GetId());
    if (componentListIt != componentsByType.end())
    {
        List<Component*>& componentList = componentListIt->second;
        componentList.erase(std::remove(componentList.begin(), componentList.end(), basic), componentList.end());
        if (componentList.empty())
        {
            componentsByType.erase(componentListIt);
        }
    }

    Object::DeleteInstance(basic);

    EnsRecord& record = enses[ens.id];
    record.basic = nullptr;
    record.alive = false;
    freeEnsIds.push_back(ens.id);
    return true;
}

//判断Ens是否存活
bool World::IsAlive(EnsId ens) const
{
    if (ens.IsNull()) return false;
    if (ens.id >= enses.size()) return false;

    const EnsRecord& record = enses[ens.id];
    return record.alive && record.version == ens.version;
}

//获取基础组件
EnsComponent* World::GetBasicComponent(EnsId ens) const
{
    if (!IsAlive(ens)) return nullptr;
    return enses[ens.id].basic;
}

//设置父级
void World::SetParent(EnsId child, EnsId parent)
{
    EnsComponent* basic = GetBasicComponent(child);
    if (!basic) return;
    if (child == parent) return;
    if (!parent.IsNull() && !IsAlive(parent)) return;

    //避免形成循环
    EnsId current = parent;
    while (!current.IsNull())
    {
        if (current == child) return;

        EnsComponent* currentBasic = GetBasicComponent(current);
        current = currentBasic ? currentBasic->parent : EnsId();
    }

    //从旧父级摘除
    EnsComponent* oldParent = GetBasicComponent(basic->parent);
    EnsComponent* previous = GetBasicComponent(basic->prev);
    EnsComponent* next = GetBasicComponent(basic->next);

    if (oldParent && oldParent->firstChild == child) oldParent->firstChild = basic->next;
    if (oldParent && oldParent->lastChild == child) oldParent->lastChild = basic->prev;
    if (previous) previous->next = basic->next;
    if (next) next->prev = basic->prev;

    basic->parent = EnsId();
    basic->prev = EnsId();
    basic->next = EnsId();

    if (parent.IsNull()) return;

    //挂到新父级末尾
    EnsComponent* parentBasic = GetBasicComponent(parent);
    if (!parentBasic) return;

    EnsComponent* lastChild = GetBasicComponent(parentBasic->lastChild);
    basic->parent = parent;
    basic->prev = parentBasic->lastChild;

    if (lastChild)
    {
        lastChild->next = child;
    }
    else
    {
        parentBasic->firstChild = child;
    }

    parentBasic->lastChild = child;
}

//获取父级
Ens World::GetParent(EnsId child) const
{
    EnsComponent* basic = GetBasicComponent(child);
    return basic ? Ens(const_cast<World*>(this), basic->parent) : Ens();
}

//添加组件
Component* World::AddComponent(EnsId ens, Type* type)
{
    EnsComponent* basic = GetBasicComponent(ens);
    if (!basic || !type || !type->Is(Component::StaticType())) return nullptr;
    if (type == EnsComponent::StaticType()) return basic;

    Component* oldComponent = GetComponent(ens, type);
    if (oldComponent) return oldComponent;

    //创建并注册组件
    std::string instancePath = basic->GetInstanceId().GetPath() + "/" + type->GetName();
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
    basic->AddComponentType(type);
    component->OnAttach();
    return component;
}

//获取组件
Component* World::GetComponent(EnsId ens, Type* type) const
{
    if (!type) return nullptr;
    if (type == EnsComponent::StaticType()) return GetBasicComponent(ens);
    if (!IsAlive(ens)) return nullptr;

    auto it = componentByEnsAndType.find(GetComponentKey(ens, type));
    if (it == componentByEnsAndType.end()) return nullptr;

    return it->second;
}

//移除组件
bool World::RemoveComponent(EnsId ens, Type* type)
{
    EnsComponent* basic = GetBasicComponent(ens);
    if (!basic || !type || type == EnsComponent::StaticType()) return false;

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

    basic->RemoveComponentType(type);
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

//按稳定ID查找Ens
Ens World::FindEns(const StringId& id) const
{
    Object* object = Object::FindObject(id);
    EnsComponent* basic = object ? object->Cast<EnsComponent>() : nullptr;
    if (!basic) return Ens();
    if (basic->GetWorld() != this) return Ens();
    if (!IsAlive(basic->GetEnsId())) return Ens();

    return Ens(const_cast<World*>(this), basic->GetEnsId());
}
