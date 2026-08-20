#include "Runtime/Ens.h"

#include "Runtime/Object/TransformComponent.h"
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

//获取变换组件
TransformComponent* Ens::Transform() const
{
    return world ? world->GetTransformComponent(ens) : nullptr;
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

//记录一个已挂载组件实例
void Ens::AddComponentInstance(Component* component)
{
    if (!component) return;
    componentInstances.push_back(component);
    AddComponentType(component->GetType());
}

//移除一个已挂载组件实例
void Ens::RemoveComponentInstance(Component* component)
{
    if (!component) return;
    componentInstances.erase(std::remove(componentInstances.begin(), componentInstances.end(), component), componentInstances.end());

    Type* type = component->GetType();
    bool hasSameType = std::any_of(componentInstances.begin(), componentInstances.end(), [type](Component* value)
        {
            return value && value->GetType() == type;
        });
    if (!hasSameType) RemoveComponentType(type);
}

//获取自身激活状态
bool Ens::GetLocalActive() const
{
    return localActive;
}

//获取层级计算后的激活状态
bool Ens::GetWorldActive() const
{
    return worldActive;
}

//设置自身激活状态
void Ens::SetLocalActive(bool value)
{
    if (!world) return;
    world->SetEnsLocalActive(ens, value);
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

//添加同类型的独立组件实例
Component* Ens::AddComponentInstance(Type* type)
{
    return world ? world->AddComponentInstance(ens, type) : nullptr;
}

//获取组件
Component* Ens::GetComponent(Type* type) const
{
    return world ? world->GetComponent(ens, type) : nullptr;
}

//获取指定类型的全部组件实例
void Ens::GetComponentInstances(Type* type, List<Component*>& output) const
{
    output.clear();
    if (world) world->GetComponentInstances(ens, type, output);
}

//移除组件
bool Ens::RemoveComponent(Type* type)
{
    return world ? world->RemoveComponent(ens, type) : false;
}

//移除指定组件实例
bool Ens::RemoveComponent(Component* component)
{
    return world ? world->RemoveComponent(component) : false;
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

//获取按挂载顺序排列的所有组件实例
const List<Component*>& Ens::GetComponents() const
{
    return componentInstances;
}
