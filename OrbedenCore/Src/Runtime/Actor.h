#pragma once

#include "Runtime/Ens.h"
#include "Runtime/EngineTypes.h"

#include <string>
#include <type_traits>
#include <vector>

//Actor基础组件
class ActorComponent : public Component
{
    OBJECT_TYPE_DECLARE(ActorComponent)

public:
    std::string name;

    Ens parent;
    Ens firstChild;
    Ens lastChild;
    Ens previousSibling;
    Ens nextSibling;

    vector3 localPosition;
    quaternion localRotation;
    vector3 localScale = { 1.0f, 1.0f, 1.0f };

    uint64 componentMask = 0;
    std::vector<TypeId> componentTypes;

    //判断是否拥有组件类型
    bool HasComponentType(Type* type) const;

    //记录组件类型
    void AddComponentType(Type* type);

    //移除组件类型
    void RemoveComponentType(Type* type);
};

//Actor对象封装
class Actor
{
private:
    Ens ens;

public:
    Actor() = default;
    explicit Actor(Ens value);

    //创建Actor
    static Actor Create(const std::string& name = "Actor");

    //从句柄创建Actor封装
    static Actor FromEns(Ens value);

    //销毁Actor
    void Destroy();

    //判断是否有效
    bool IsValid() const;

    //获取底层句柄
    Ens GetEns() const;

    //获取基础组件
    ActorComponent* Basic() const;

    //获取名称
    const std::string& GetName() const;

    //设置名称
    void SetName(const std::string& name);

    //获取位置
    vector3 GetLocalPosition() const;

    //设置位置
    void SetLocalPosition(const vector3& position);

    //设置父级
    void SetParent(Actor parent);

    //获取父级
    Actor GetParent() const;

    //添加组件
    Component* AddComponent(Type* type);

    //获取组件
    Component* GetComponent(Type* type) const;

    //移除组件
    bool RemoveComponent(Type* type);

    //判断是否拥有组件
    bool HasComponent(Type* type) const;

    //添加组件
    template<typename T>
    T* AddComponent();

    //获取组件
    template<typename T>
    T* GetComponent() const;

    //移除组件
    template<typename T>
    bool RemoveComponent();

    //判断是否拥有组件
    template<typename T>
    bool HasComponent() const;
};

template<typename T>
T* Actor::AddComponent()
{
    static_assert(std::is_base_of_v<Component, T>);
    return static_cast<T*>(AddComponent(T::StaticType()));
}

template<typename T>
T* Actor::GetComponent() const
{
    static_assert(std::is_base_of_v<Component, T>);
    return static_cast<T*>(GetComponent(T::StaticType()));
}

template<typename T>
bool Actor::RemoveComponent()
{
    static_assert(std::is_base_of_v<Component, T>);
    return RemoveComponent(T::StaticType());
}

template<typename T>
bool Actor::HasComponent() const
{
    static_assert(std::is_base_of_v<Component, T>);
    return HasComponent(T::StaticType());
}
