#pragma once

#include "Runtime/EnsId.h"

//单个Component类型在一个World中的稀疏集索引
class ComponentStorage
{
private:
    //EnsId槽位到紧凑数组位置的映射
    struct SparseSlot
    {
        uint32 denseIndex = EnsId::InvalidId;
        uint32 ensVersion = 0;
    };

    World* ownerWorld = nullptr;
    Type* componentType = nullptr;
    List<SparseSlot> sparseSlots;
    List<EnsId> owners;
    List<Component*> components;

public:
    ComponentStorage(World* world, Type* type);
    ComponentStorage(const ComponentStorage&) = delete;
    ComponentStorage& operator=(const ComponentStorage&) = delete;

    //获取组件类型
    Type* GetType() const;

    //登记Ens拥有的组件
    bool Add(EnsId owner, Component* component);

    //查找Ens拥有的组件
    Component* Get(EnsId owner) const;

    //移除并返回Ens拥有的组件
    Component* Remove(EnsId owner);

    //获取存活组件数量
    uint32 GetCount() const;

    //按紧凑数组顺序遍历组件
    template<typename TVisitor>
    void ForEach(TVisitor&& visitor) const
    {
        for (Component* component : components)
        {
            visitor(component);
        }
    }
};
