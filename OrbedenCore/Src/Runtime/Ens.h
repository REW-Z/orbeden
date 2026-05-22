#pragma once

#include "Runtime/Object.h"

class Actor;

//底层对象句柄
struct Ens
{
public:
    static constexpr uint32 InvalidId = 0xFFFFFFFFu;

    uint32 id = InvalidId;
    uint32 version = 0;

    //判断句柄是否为空
    bool IsNull() const;

    bool operator==(const Ens& other) const;
    bool operator!=(const Ens& other) const;
};

//组件基类
class Component : public Object
{
    OBJECT_TYPE_DECLARE(Component)

private:
    Ens owner;

public:
    Component() = default;
    virtual ~Component() = default;

    //获取所属Actor
    Actor GetActor() const;

    //获取所属句柄
    Ens GetOwner() const;

    //设置所属句柄
    void SetOwner(Ens value);

    //挂载时调用
    virtual void OnAttach() {}

    //卸载时调用
    virtual void OnDetach() {}
};
