#include "Runtime/Actor.h"

#include "Runtime/ObjectSystem.h"

#include <algorithm>

OBJECT_TYPE_IMPLEMENT(ActorComponent, Component)

//判断是否拥有组件类型
bool ActorComponent::HasComponentType(Type* type) const
{
    if (!type) return false;

    return std::find(componentTypes.begin(), componentTypes.end(), type->GetId()) != componentTypes.end();
}

//记录组件类型
void ActorComponent::AddComponentType(Type* type)
{
    if (!type || HasComponentType(type)) return;

    componentTypes.push_back(type->GetId());
    componentMask |= type->GetMask();
}

//移除组件类型
void ActorComponent::RemoveComponentType(Type* type)
{
    if (!type) return;

    componentTypes.erase(std::remove(componentTypes.begin(), componentTypes.end(), type->GetId()), componentTypes.end());
    componentMask &= ~type->GetMask();
}

//创建Actor封装
Actor::Actor(Ens value)
    : ens(value)
{
}

//创建Actor
Actor Actor::Create(const std::string& name)
{
    ObjectSystem& system = GetObjectSystem();

    Ens value;
    if (!system.freeActorIds.empty())
    {
        value.id = system.freeActorIds.back();
        system.freeActorIds.pop_back();

        ActorRecord& record = system.actors[value.id];
        record.version++;
        record.alive = true;
        value.version = record.version;
    }
    else
    {
        value.id = static_cast<uint32>(system.actors.size());
        value.version = 1;
        system.actors.push_back(ActorRecord());
        system.actors[value.id].alive = true;
    }

    ActorComponent* basic = Object::CreateInstance<ActorComponent>();
    basic->SetOwner(value);
    basic->name = name;
    basic->AddComponentType(ActorComponent::StaticType());

    system.actors[value.id].basic = basic;
    RegisterComponent(value, basic);

    return Actor(value);
}

//从句柄创建Actor封装
Actor Actor::FromEns(Ens value)
{
    return Actor(value);
}

//销毁Actor
void Actor::Destroy()
{
    if (!IsValid()) return;

    ActorComponent* basic = Basic();

    //先解除子级关系
    Ens child = basic->firstChild;
    while (!child.IsNull())
    {
        ActorComponent* childBasic = GetBasicComponent(child);
        Ens nextChild = childBasic ? childBasic->nextSibling : Ens();
        Actor(child).SetParent(Actor());
        child = nextChild;
    }

    SetParent(Actor());

    std::vector<TypeId> componentTypes = basic->componentTypes;
    for (TypeId typeId : componentTypes)
    {
        Type* type = Object::FindType(typeId);
        if (type && type != ActorComponent::StaticType())
        {
            RemoveComponent(type);
        }
    }

    UnregisterComponent(ens, ActorComponent::StaticType());
    Object::DeleteInstance(basic);

    ObjectSystem& system = GetObjectSystem();
    ActorRecord& record = system.actors[ens.id];
    record.basic = nullptr;
    record.alive = false;
    system.freeActorIds.push_back(ens.id);

    ens = Ens();
}

//判断是否有效
bool Actor::IsValid() const
{
    return IsEnsAlive(ens);
}

//获取底层句柄
Ens Actor::GetEns() const
{
    return ens;
}

//获取基础组件
ActorComponent* Actor::Basic() const
{
    return GetBasicComponent(ens);
}

//获取名称
const std::string& Actor::GetName() const
{
    static const std::string emptyName;

    ActorComponent* basic = Basic();
    return basic ? basic->name : emptyName;
}

//设置名称
void Actor::SetName(const std::string& name)
{
    ActorComponent* basic = Basic();
    if (!basic) return;

    basic->name = name;
}

//获取位置
vector3 Actor::GetLocalPosition() const
{
    ActorComponent* basic = Basic();
    return basic ? basic->localPosition : vector3();
}

//设置位置
void Actor::SetLocalPosition(const vector3& position)
{
    ActorComponent* basic = Basic();
    if (!basic) return;

    basic->localPosition = position;
}

//设置父级
void Actor::SetParent(Actor parent)
{
    ActorComponent* basic = Basic();
    if (!basic) return;
    if (parent.ens == ens) return;

    //避免形成循环
    Actor current = parent;
    while (current.IsValid())
    {
        if (current.ens == ens) return;
        current = current.GetParent();
    }

    //从旧父级摘除
    ActorComponent* oldParent = GetBasicComponent(basic->parent);
    ActorComponent* previous = GetBasicComponent(basic->previousSibling);
    ActorComponent* next = GetBasicComponent(basic->nextSibling);

    if (oldParent && oldParent->firstChild == ens) oldParent->firstChild = basic->nextSibling;
    if (oldParent && oldParent->lastChild == ens) oldParent->lastChild = basic->previousSibling;
    if (previous) previous->nextSibling = basic->nextSibling;
    if (next) next->previousSibling = basic->previousSibling;

    basic->parent = Ens();
    basic->previousSibling = Ens();
    basic->nextSibling = Ens();

    if (!parent.IsValid()) return;

    //挂到新父级末尾
    ActorComponent* parentBasic = parent.Basic();
    ActorComponent* lastChild = GetBasicComponent(parentBasic->lastChild);

    basic->parent = parent.ens;
    basic->previousSibling = parentBasic->lastChild;

    if (lastChild)
    {
        lastChild->nextSibling = ens;
    }
    else
    {
        parentBasic->firstChild = ens;
    }

    parentBasic->lastChild = ens;
}

//获取父级
Actor Actor::GetParent() const
{
    ActorComponent* basic = Basic();
    return basic ? Actor(basic->parent) : Actor();
}

//添加组件
Component* Actor::AddComponent(Type* type)
{
    ActorComponent* basic = Basic();
    if (!basic || !type || !type->Is(Component::StaticType())) return nullptr;
    if (type == ActorComponent::StaticType()) return basic;

    Component* oldComponent = GetComponent(type);
    if (oldComponent) return oldComponent;

    Object* object = Object::CreateInstance(type);
    Component* component = object ? object->Cast<Component>() : nullptr;
    if (!component)
    {
        Object::DeleteInstance(object);
        return nullptr;
    }

    component->SetOwner(ens);
    RegisterComponent(ens, component);
    basic->AddComponentType(type);
    component->OnAttach();
    return component;
}

//获取组件
Component* Actor::GetComponent(Type* type) const
{
    if (!IsValid() || !type) return nullptr;
    if (type == ActorComponent::StaticType()) return Basic();

    ObjectSystem& system = GetObjectSystem();
    auto it = system.componentByActorAndType.find(GetComponentKey(ens, type));
    if (it == system.componentByActorAndType.end()) return nullptr;

    return it->second;
}

//移除组件
bool Actor::RemoveComponent(Type* type)
{
    ActorComponent* basic = Basic();
    if (!basic || !type || type == ActorComponent::StaticType()) return false;

    Component* component = GetComponent(type);
    if (!component) return false;

    component->OnDetach();
    UnregisterComponent(ens, type);
    basic->RemoveComponentType(type);
    Object::DeleteInstance(component);
    return true;
}

//判断是否拥有组件
bool Actor::HasComponent(Type* type) const
{
    ActorComponent* basic = Basic();
    return basic && basic->HasComponentType(type);
}
