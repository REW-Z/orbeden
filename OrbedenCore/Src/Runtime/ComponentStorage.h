#pragma once

#include "Runtime/EnsId.h"

#include <type_traits>

//单个Component类型在一个World中的稀疏集索引
class ComponentStorage
{
private:
    //EnsId槽位到紧凑数组位置的映射
    struct SparseSlot
    {
        Component* component = nullptr;
        uint32 ensVersion = 0;
    };

    World* ownerWorld = nullptr;
    Type* componentType = nullptr;
    IChunk* componentChunk = nullptr;
    List<SparseSlot> sparseSlots;
    uint32 componentCount = 0;

    //访问Component槽位
    template<typename TVisitor>
    static void VisitComponentObject(Object* object, void* userData)
    {
        TVisitor* visitor = static_cast<TVisitor*>(userData);
        (*visitor)(static_cast<Component*>(object));
    }

    //访问指定Component槽位
    template<typename TComponent, typename TVisitor>
    static void VisitTypedComponentObject(Object* object, void* userData)
    {
        TVisitor* visitor = static_cast<TVisitor*>(userData);
        (*visitor)(static_cast<TComponent*>(object));
    }

public:
    ComponentStorage(World* world, Type* type);
    ComponentStorage(const ComponentStorage&) = delete;
    ComponentStorage& operator=(const ComponentStorage&) = delete;
    ~ComponentStorage();

    //获取组件类型
    Type* GetType() const;

    //创建Ens拥有的组件
    Component* Create(EnsId owner, const std::string& instancePath);

    //查找Ens拥有的组件
    Component* Get(EnsId owner) const;

    //移除并返回Ens拥有的组件
    Component* Remove(EnsId owner);

    //获取存活组件数量
    uint32 GetCount() const;

    //按Chunk槽位顺序遍历组件
    template<typename TVisitor>
    void ForEach(TVisitor&& visitor) const
    {
        using VisitorType = std::remove_reference_t<TVisitor>;
        componentType->VisitChunkObjects(componentChunk, &ComponentStorage::VisitComponentObject<VisitorType>, &visitor);
    }

    //按Chunk槽位顺序遍历指定类型组件
    template<typename TComponent, typename TVisitor>
    void ForEachTyped(TVisitor&& visitor) const
    {
        using VisitorType = std::remove_reference_t<TVisitor>;
        componentType->VisitChunkObjects(componentChunk, &ComponentStorage::VisitTypedComponentObject<TComponent, VisitorType>, &visitor);
    }
};
