#pragma once

#include "Runtime/EnsId.h"
#include "Runtime/EngineTypes.h"

#include <string>
#include <type_traits>

class Ens;
class World;
class TransformComponent;

typedef void (*EnsVisitorFunction)(Ens* ens, void* userData);

class Ens
{
    friend class World;

private:
    World* world = nullptr;
    EnsId ens;
    bool alive = false;
    bool localActive = true;
    bool worldActive = true;
    std::string name;
    uint64 componentMask = 0;
    List<TypeId> componentTypes;
    List<Component*> componentInstances;

    //记录组件类型
    void AddComponentType(Type* type);

    //移除组件类型
    void RemoveComponentType(Type* type);

    //判断是否记录了组件类型
    bool HasComponentType(Type* type) const;

    //记录一个已挂载组件实例
    void AddComponentInstance(Component* component);

    //移除一个已挂载组件实例
    void RemoveComponentInstance(Component* component);

    Ens() = default;
    Ens(World* ownerWorld, EnsId value);
    ~Ens() = default;

public:
    Ens(const Ens&) = delete;
    Ens& operator=(const Ens&) = delete;
    Ens(Ens&&) = delete;
    Ens& operator=(Ens&&) = delete;

    //销毁当前Ens
    void Destroy();

    //判断是否有效
    bool IsValid() const;

    //获取所属世界
    World* GetWorld() const;

    //获取底层ID
    EnsId GetId() const;

    //获取变换组件
    TransformComponent* Transform() const;

    //获取名称
    const std::string& GetName() const;

    //设置名称
    void SetName(const std::string& name);

    //获取自身激活状态
    bool GetLocalActive() const;

    //获取层级计算后的激活状态
    bool GetWorldActive() const;

    //设置自身激活状态
    void SetLocalActive(bool value);

    //设置父级
    void SetParent(Ens* parent);

    //获取父级
    Ens* GetParent() const;

    //添加组件
    Component* AddComponent(Type* type);

    //添加同类型的独立组件实例
    Component* AddComponentInstance(Type* type);

    //获取组件
    Component* GetComponent(Type* type) const;

    //获取指定类型的全部组件实例
    void GetComponentInstances(Type* type, List<Component*>& output) const;

    //移除组件
    bool RemoveComponent(Type* type);

    //移除指定组件实例
    bool RemoveComponent(Component* component);

    //判断是否拥有组件
    bool HasComponent(Type* type) const;

    //获取组件类型列表
    const List<TypeId>& GetComponentTypes() const;

    //获取按挂载顺序排列的所有组件实例
    const List<Component*>& GetComponents() const;

    //添加组件
    template<typename T>
    T* AddComponent()
    {
        static_assert(std::is_base_of_v<Component, T>);
        return static_cast<T*>(AddComponent(T::StaticType()));
    }

    //添加同类型的独立组件实例
    template<typename T>
    T* AddComponentInstance()
    {
        static_assert(std::is_base_of_v<Component, T>);
        return static_cast<T*>(AddComponentInstance(T::StaticType()));
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

    //获取指定类型的全部组件实例
    template<typename T>
    void GetComponentInstances(List<T*>& output) const
    {
        static_assert(std::is_base_of_v<Component, T>);
        List<Component*> components;
        GetComponentInstances(T::StaticType(), components);
        output.clear();
        output.reserve(components.size());
        for (Component* component : components)
        {
            if (T* value = component ? component->Cast<T>() : nullptr) output.push_back(value);
        }
    }

    //移除指定组件实例
    template<typename T>
    bool RemoveComponent(T* component)
    {
        static_assert(std::is_base_of_v<Component, T>);
        return RemoveComponent(static_cast<Component*>(component));
    }

    //判断是否拥有组件
    template<typename T>
    bool HasComponent() const
    {
        static_assert(std::is_base_of_v<Component, T>);
        return HasComponent(T::StaticType());
    }
};
