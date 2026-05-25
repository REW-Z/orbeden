#pragma once

#include "Runtime/EnsId.h"
#include "Runtime/EngineTypes.h"

#include <string>
#include <type_traits>

class World;

//Ens基础组件
class EnsComponent : public Component
{
    OBJECT_TYPE_DECLARE(EnsComponent)

public:
    std::string name;

    EnsId parent;
    EnsId firstChild;
    EnsId lastChild;
    EnsId prev;
    EnsId next;

    vector3 localPosition;
    quaternion localRotation;
    vector3 localScale = { 1.0f, 1.0f, 1.0f };

    uint64 componentMask = 0;
    List<TypeId> componentTypes;

    //判断是否拥有组件类型
    bool HasComponentType(Type* type) const;

    //记录组件类型
    void AddComponentType(Type* type);

    //移除组件类型
    void RemoveComponentType(Type* type);
};

//Ens对象封装
class Ens
{
private:
    World* world = nullptr;
    EnsId ens;

public:
    Ens() = default;
    Ens(World* ownerWorld, EnsId value);

    //创建Ens
    static Ens Create(const std::string& name = "Ens");

    //从句柄创建Ens封装
    static Ens FromEns(EnsId value);

    /// <summary> 从指定世界和句柄创建Ens封装。 </summary>
    static Ens FromEns(World* world, EnsId value);

    //销毁Ens
    void Destroy();

    //判断是否有效
    bool IsValid() const;

    /// <summary> 获取所属世界。 </summary>
    World* GetWorld() const;

    //获取底层句柄
    EnsId GetEns() const;

    //获取基础组件
    EnsComponent* Basic() const;

    //获取名称
    const std::string& GetName() const;

    //设置名称
    void SetName(const std::string& name);

    //获取位置
    vector3 GetLocalPosition() const;

    //设置位置
    void SetLocalPosition(const vector3& position);

    //设置父级
    void SetParent(Ens parent);

    //获取父级
    Ens GetParent() const;

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
    T* AddComponent()
    {
        static_assert(std::is_base_of_v<Component, T>);
        return static_cast<T*>(AddComponent(T::StaticType()));
    }

    //获取组件
    template<typename T>
    T* GetComponent() const
    {
        static_assert(std::is_base_of_v<Component, T>);
        return static_cast<T*>(GetComponent(T::StaticType()));
    }

    //移除组件
    template<typename T>
    bool RemoveComponent()
    {
        static_assert(std::is_base_of_v<Component, T>);
        return RemoveComponent(T::StaticType());
    }

    //判断是否拥有组件
    template<typename T>
    bool HasComponent() const
    {
        static_assert(std::is_base_of_v<Component, T>);
        return HasComponent(T::StaticType());
    }
};
