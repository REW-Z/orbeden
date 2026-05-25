#pragma once

#include "Runtime/Ens.h"

#include <string>
#include <type_traits>
#include <unordered_map>

//运行时世界
class World
{
private:
    struct EnsRecord
    {
    public:
        uint32 version = 1;
        bool alive = false;
        EnsComponent* basic = nullptr;
    };

    List<EnsRecord> enses;
    List<uint32> freeEnsIds;
    std::unordered_map<uint64, Component*> componentByEnsAndType;
    std::unordered_map<TypeId, List<Component*>> componentsByType;
    List<Object*> ownedObjects;
    uint64 nextEnsIndex = 1;
    uint64 nextRuntimeObjectIndex = 1;

public:
    World() = default;

    /// <summary> 销毁世界及其运行时对象。 </summary>
    ~World();

    /// <summary> 获取当前活动世界。 </summary>
    static World* CurrentWorld();

    /// <summary> 设置当前活动世界。 </summary>
    static void SetCurrentWorld(World* world);

    /// <summary> 创建Ens。 </summary>
    Ens CreateEns(const std::string& name = "Ens");

    /// <summary> 销毁Ens。 </summary>
    bool DestroyEns(EnsId ens);

    /// <summary> 判断Ens是否存活。 </summary>
    bool IsAlive(EnsId ens) const;

    /// <summary> 获取基础组件。 </summary>
    EnsComponent* GetBasicComponent(EnsId ens) const;

    /// <summary> 设置父级。 </summary>
    void SetParent(EnsId child, EnsId parent);

    /// <summary> 获取父级。 </summary>
    Ens GetParent(EnsId child) const;

    /// <summary> 添加组件。 </summary>
    Component* AddComponent(EnsId ens, Type* type);

    /// <summary> 获取组件。 </summary>
    Component* GetComponent(EnsId ens, Type* type) const;

    /// <summary> 移除组件。 </summary>
    bool RemoveComponent(EnsId ens, Type* type);

    /// <summary> 创建世界内对象。 </summary>
    Object* CreateObject(Type* type, const std::string& stableId = "");

    /// <summary> 创建世界内对象。 </summary>
    template<typename T>
    T* CreateObject(const std::string& stableId = "")
    {
        static_assert(std::is_base_of_v<Object, T>);
        return static_cast<T*>(CreateObject(T::StaticType(), stableId));
    }

    /// <summary> 销毁世界内对象。 </summary>
    bool DestroyObject(Object* object);

    /// <summary> 按稳定ID查找Ens。 </summary>
    Ens FindEns(const StringId& id) const;

    /// <summary> 遍历所有存活的Ens。 </summary>
    template<typename TVisitor>
    void ForEachEns(TVisitor&& visitor) const
    {
        for (uint32 index = 0; index < enses.size(); ++index)
        {
            const EnsRecord& record = enses[index];
            if (!record.alive) continue;

            EnsId value;
            value.id = index;
            value.version = record.version;
            visitor(Ens(const_cast<World*>(this), value));
        }
    }

    /// <summary> 按精确类型遍历组件。 </summary>
    template<typename TVisitor>
    void ForEachComponent(Type* type, TVisitor&& visitor) const
    {
        if (!type) return;

        auto it = componentsByType.find(type->GetId());
        if (it == componentsByType.end()) return;

        for (Component* component : it->second)
        {
            visitor(component);
        }
    }
};
