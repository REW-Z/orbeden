#pragma once

#include "Runtime/Object/Object.h"

class Ens;
class World;

//底层对象句柄
struct EnsId
{
public:
    static constexpr uint32 InvalidId = 0xFFFFFFFFu;

    uint32 id = InvalidId;
    uint32 version = 0;

    //判断句柄是否为空
    bool IsNull() const;

    bool operator==(const EnsId& other) const;
    bool operator!=(const EnsId& other) const;
};

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
