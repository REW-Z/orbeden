#pragma once

#include "Runtime/EnsId.h"

class World;

//空间变换监听器，接收局部变换和父级变化通知
class ITransformListener
{
public:
    virtual ~ITransformListener() = default;

    //接收指定空间节点及其子树失效通知
    virtual void OnTransformChanged(World& world, EnsId ens) = 0;
};
