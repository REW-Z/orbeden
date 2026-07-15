#pragma once

#include "Runtime/ComponentStorage.h"
#include "Runtime/Ens.h"
#include "Runtime/RenderSettings.h"

#include <string>
#include <type_traits>

//运行时世界
class World
{
    friend class Ens;
    friend class Object;

private:
    //Ens ID槽位，保存版本和紧凑列表索引
    struct EnsSlot
    {
        Ens* value = nullptr;
        uint32 version = 0;
        uint32 denseIndex = EnsId::InvalidId;
    };

    List<EnsSlot> ensSlots;//按EnsId索引的稀疏槽位表
    List<Ens*> liveEns;//所有存活Ens指针
    List<uint32> freeEnsIds;//等待复用的EnsId槽位

    List<ComponentStorage*> componentStorages;//按TypeId索引的组件稀疏集
	List<Object*> ownedObjects;//world拥有的运行时对象

    //使用指定稳定ID创建Ens
    Ens* CreateEnsInternal(const std::string& name, const std::string& stableId);

    //查找组件稀疏集
    ComponentStorage* FindComponentStorage(Type* type) const;

    //获取或创建组件稀疏集
    ComponentStorage* GetOrCreateComponentStorage(Type* type);

    //遍历所有存活的Ens
    void VisitEns(EnsVisitorFunction visitor, void* userData) const;

    //生成Ens对象ID
    std::string AllocateEnsObjectPath();

    //生成未命名Ens的名称
    std::string GetEnsName(const std::string& name) const;

    //生成世界运行时对象ID
    std::string AllocateRuntimeObjectPath(Type* type);

    //接收世界拥有的运行时对象
    bool AddOwnedObject(Object* object);

    //摘除世界拥有的运行时对象
    bool RemoveOwnedObject(Object* object);

public:
    RenderSettings renderSettings;

    World() = default;

    //销毁世界及其运行时对象
    ~World();

    //获取当前活动世界
    static World* CurrentWorld();

    //设置当前活动世界
    static void SetCurrentWorld(World* world);

    //创建Ens
    Ens* CreateEns(const std::string& name = "");

    //使用稳定ID创建Ens
    Ens* CreateEnsWithStableId(const std::string& stableId, const std::string& name = "");

    //清空世界运行时对象
    void Clear();

    //销毁Ens
    bool DestroyEns(EnsId ens);

    //判断Ens是否存活
    bool IsAlive(EnsId ens) const;

    //获取World持有的唯一Ens实例
    Ens* GetEns(EnsId ens);

    //获取World持有的唯一Ens实例
    const Ens* GetEns(EnsId ens) const;

    //获取空间组件
    SpaceComponent* GetSpaceComponent(EnsId ens) const;

    //设置父级
    void SetParent(EnsId child, EnsId parent);

    //移动Ens到指定父级，并插入到同级目标之前；目标为空时放到末尾
    bool MoveEns(EnsId child, EnsId parent, EnsId beforeSibling = EnsId());

    //获取父级
    Ens* GetParent(EnsId child) const;

    //添加组件
    Component* AddComponent(EnsId ens, Type* type);

    //获取组件
    Component* GetComponent(EnsId ens, Type* type) const;

    //移除组件
    bool RemoveComponent(EnsId ens, Type* type);

    //按稳定ID查找Ens
    Ens* FindEns(const StringId& id) const;

    //遍历所有存活的Ens
    template<typename TVisitor>
    void ForEachEns(TVisitor&& visitor) const
    {
        struct VisitorContext
        {
            TVisitor& callback;
        };

        VisitorContext context{ visitor };
        VisitEns([](Ens* ens, void* userData)
        {
            VisitorContext* visitorContext = static_cast<VisitorContext*>(userData);
            visitorContext->callback(*ens);
        }, &context);
    }

    //按精确类型遍历组件
    template<typename TVisitor>
    void ForEachComponent(Type* type, TVisitor&& visitor) const
    {
        ComponentStorage* storage = FindComponentStorage(type);
        if (!storage) return;

        storage->ForEach(visitor);
    }

    //按编译期精确类型遍历组件
    template<typename TComponent, typename TVisitor>
    void ForEachComponent(TVisitor&& visitor) const
    {
        static_assert(std::is_base_of_v<Component, TComponent>);

        ComponentStorage* storage = FindComponentStorage(TComponent::StaticType());
        if (!storage) return;

        storage->ForEachTyped<TComponent>(visitor);
    }
};
