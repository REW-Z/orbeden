#include "Runtime/Ens.h"

#include "Runtime/SpaceComponent.h"
#include "Runtime/World.h"

#include <algorithm>
#include <cassert>

//创建Ens封装
Ens::Ens(World* ownerWorld, EnsId value)
    : world(ownerWorld), ens(value)
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

//创建Ens
Ens Ens::Create(const std::string& name)
{
    World* currentWorld = World::CurrentWorld();
    assert(currentWorld);
    return currentWorld ? currentWorld->CreateEns(name) : Ens();
}

//从句柄创建Ens封装
Ens Ens::FromEns(EnsId value)
{
    return FromEns(World::CurrentWorld(), value);
}

//从指定世界和句柄创建Ens封装
Ens Ens::FromEns(World* ownerWorld, EnsId value)
{
    return Ens(ownerWorld, value);
}

//销毁Ens
void Ens::Destroy()
{
    if (!world) return;
    world->DestroyEns(ens);
    world = nullptr;
    ens = EnsId();
}

//判断是否有效
bool Ens::IsValid() const
{
    return world && world->IsAlive(ens);
}

//获取所属世界
World* Ens::GetWorld() const
{
    return world;
}

//获取底层句柄
EnsId Ens::GetEns() const
{
    return ens;
}

//获取空间组件
SpaceComponent* Ens::Space() const
{
    const Ens* storedEns = world ? world->GetEns(ens) : nullptr;
    return storedEns ? storedEns->space : nullptr;
}

//获取名称
const std::string& Ens::GetName() const
{
    static const std::string emptyName;
    const Ens* storedEns = world ? world->GetEns(ens) : nullptr;

    return storedEns ? storedEns->name : emptyName;
}

//设置名称
void Ens::SetName(const std::string& name)
{
    Ens* storedEns = world ? world->GetEns(ens) : nullptr;
    if (storedEns) storedEns->name = name;
}

//获取位置
vector3 Ens::GetLocalPosition() const
{
    SpaceComponent* space = Space();
    return space ? space->localPosition : vector3();
}

//设置位置
void Ens::SetLocalPosition(const vector3& position)
{
    SpaceComponent* space = Space();
    if (!space) return;

    space->localPosition = position;
}

//设置父级
void Ens::SetParent(Ens parent)
{
    if (!world) return;
    if (parent.GetWorld() && parent.GetWorld() != world) return;
    world->SetParent(ens, parent.GetEns());
}

//获取父级
Ens Ens::GetParent() const
{
    return world ? world->GetParent(ens) : Ens();
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
    const Ens* storedEns = world ? world->GetEns(ens) : nullptr;
    return storedEns && storedEns->HasComponentType(type);
}

//获取组件类型列表
const List<TypeId>& Ens::GetComponentTypes() const
{
    static const List<TypeId> emptyTypes;

    const Ens* storedEns = world ? world->GetEns(ens) : nullptr;
    return storedEns ? storedEns->componentTypes : emptyTypes;
}
