#include "Runtime/Ens.h"

#include "Runtime/Object/SpaceComponent.h"
#include "Runtime/World.h"

#include <algorithm>
#include <cassert>

//创建World持有的唯一Ens实例
Ens::Ens(World* ownerWorld, EnsId value)
    : world(ownerWorld), ens(value), alive(true)
{
}

//记录组件类型
void Ens::AddComponentType(Type* type)
{
    if (!type) return;
    if (HasComponentType(type)) return;

    componentTypes.push_back(type->GetId());
    componentMask |= type->GetMask();
}

//移除组件类型
void Ens::RemoveComponentType(Type* type)
{
    if (!type) return;

    componentTypes.erase(std::remove(componentTypes.begin(), componentTypes.end(), type->GetId()), componentTypes.end());
    componentMask &= ~type->GetMask();
}

//判断是否记录了组件类型
bool Ens::HasComponentType(Type* type) const
{
    if (!type) return false;
    if ((componentMask & type->GetMask()) == 0) return false;

    return std::find(componentTypes.begin(), componentTypes.end(), type->GetId()) != componentTypes.end();
}

//销毁Ens
void Ens::Destroy()
{
    if (!world) return;
    world->DestroyEns(ens);
}

//判断是否有效
bool Ens::IsValid() const
{
    return alive && world && world->GetEns(ens) == this;
}

//获取所属世界
World* Ens::GetWorld() const
{
    return world;
}

//获取底层ID
EnsId Ens::GetId() const
{
    return ens;
}

//获取空间组件
SpaceComponent* Ens::Space() const
{
    return world ? world->GetSpaceComponent(ens) : nullptr;
}

//获取名称
const std::string& Ens::GetName() const
{
    return name;
}

//设置名称
void Ens::SetName(const std::string& name)
{
    this->name = name;
}


//设置父级
void Ens::SetParent(Ens* parent)
{
    if (!world) return;
    if (parent && parent->GetWorld() != world) return;
    world->SetParent(ens, parent ? parent->GetId() : EnsId());
}

//获取父级
Ens* Ens::GetParent() const
{
    return world ? world->GetParent(ens) : nullptr;
}

//添加组件
Component* Ens::AddComponent(Type* type)
{
    return world ? world->AddComponent(ens, type) : nullptr;
}

//获取组件
Component* Ens::GetComponent(Type* type) const
{
    return world ? world->GetComponent(ens, type) : nullptr;
}

//移除组件
bool Ens::RemoveComponent(Type* type)
{
    return world ? world->RemoveComponent(ens, type) : false;
}

//判断是否拥有组件
bool Ens::HasComponent(Type* type) const
{
    return HasComponentType(type);
}

//获取组件类型列表
const List<TypeId>& Ens::GetComponentTypes() const
{
    return componentTypes;
}
