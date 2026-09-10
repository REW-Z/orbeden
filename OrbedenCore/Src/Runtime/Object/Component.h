#pragma once

#include "Runtime/EnsId.h"
#include "Runtime/Object/Object.h"

//组件基类
class Component : public Object
{
    OBJECT_TYPE_DECLARE_BASE(Component)

private:
    friend class World;
    friend class ComponentStorage;

    EnsId owner;

    //设置所属句柄
    void SetEnsId(EnsId value);

public:
    //获取所属Ens
    ORBEDEN_BIND_IGNORE
    Ens* GetEns() const;

    //获取所属句柄
    EnsId GetEnsId() const;

    //挂载时调用
    virtual void OnAttach() {}

    //卸载时调用
    virtual void OnDetach() {}

    //所属 Ens 的 worldActive 变化时调用
    virtual void OnWorldActiveChanged(bool worldActive) { (void)worldActive; }
};
