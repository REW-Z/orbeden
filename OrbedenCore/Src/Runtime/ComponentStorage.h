#pragma once

#include "Runtime/EnsId.h"

typedef void (*ComponentVisitorFunction)(Component* component, void* userData);

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

    //遍历当前Chunk中的组件
    void VisitComponents(ComponentVisitorFunction visitor, void* userData) const;

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

    //按紧凑数组顺序遍历组件
    template<typename TVisitor>
    void ForEach(TVisitor&& visitor) const
    {
        struct VisitorContext
        {
            TVisitor& callback;
        };

        VisitorContext context{ visitor };
        VisitComponents([](Component* component, void* userData)
        {
            VisitorContext* visitorContext = static_cast<VisitorContext*>(userData);
            visitorContext->callback(component);
        }, &context);
    }
};
