#pragma once

#include "Runtime/Ens.h"
#include "Runtime/RenderSettings.h"

#include <string>
#include <type_traits>
#include <unordered_map>

//运行时世界
class World
{
    friend class Ens;

private:
    List<Ens> enses;
    List<uint32> freeEnsIds;
    std::unordered_map<uint64, Component*> componentByEnsAndType;
    std::unordered_map<TypeId, List<Component*>> componentsByType;
    List<Object*> ownedObjects;
    List<std::string> sceneResourceRefs;
    uint64 nextEnsIndex = 1;
    uint64 nextRuntimeObjectIndex = 1;

    //使用指定稳定ID创建Ens
    Ens CreateEnsInternal(const std::string& name, const std::string& stableId);

    //获取World内部Ens数据
    Ens* GetEns(EnsId ens);

    //获取World内部Ens数据
    const Ens* GetEns(EnsId ens) const;

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
    Ens CreateEns(const std::string& name = "Ens");

    //使用稳定ID创建Ens
    Ens CreateEnsWithStableId(const std::string& stableId, const std::string& name = "Ens");

    //清空世界运行时对象
    void Clear();

    //销毁Ens
    bool DestroyEns(EnsId ens);

    //判断Ens是否存活
    bool IsAlive(EnsId ens) const;

    //获取空间组件
    SpaceComponent* GetSpaceComponent(EnsId ens) const;

    //设置父级
    void SetParent(EnsId child, EnsId parent);

    //获取父级
    Ens GetParent(EnsId child) const;

    //添加组件
    Component* AddComponent(EnsId ens, Type* type);

    //获取组件
    Component* GetComponent(EnsId ens, Type* type) const;

    //移除组件
    bool RemoveComponent(EnsId ens, Type* type);

    //创建世界内对象
    Object* CreateObject(Type* type, const std::string& stableId = "");

    //创建世界内对象
    template<typename T>
    T* CreateObject(const std::string& stableId = "")
    {
        static_assert(std::is_base_of_v<Object, T>);
        return static_cast<T*>(CreateObject(T::StaticType(), stableId));
    }

    //销毁世界内对象
    bool DestroyObject(Object* object);

    //增加一个场景资源引用
    bool AddSceneResourceRef(Type* type, const std::string& key);

    //释放当前World持有的所有场景资源引用
    void ReleaseSceneResourceRefs();

    //按稳定ID查找Ens
    Ens FindEns(const StringId& id) const;

    //遍历所有存活的Ens
    template<typename TVisitor>
    void ForEachEns(TVisitor&& visitor) const
    {
        for (uint32 index = 0; index < enses.size(); ++index)
        {
            const Ens& storedEns = enses[index];
            if (!storedEns.alive) continue;

            visitor(Ens(const_cast<World*>(this), storedEns.ens));
        }
    }

    //按精确类型遍历组件
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
