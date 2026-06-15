#pragma once

#include "Runtime/Object/Object.h"

class Ens;

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
    OBJECT_TYPE_DECLARE(Component)

private:
    EnsId owner;

public:
    Component() = default;
    virtual ~Component() = default;

    //获取所属Ens
    Ens GetEns() const;

    //获取所属句柄
    EnsId GetEnsId() const;

    //设置所属句柄
    void SetEnsId(EnsId value);

    //挂载时调用
    virtual void OnAttach() {}

    //卸载时调用
    virtual void OnDetach() {}
};
