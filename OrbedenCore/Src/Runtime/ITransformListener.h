#pragma once

#include "Runtime/EnsId.h"

class World;

//变换变更的来源。派生写入来自布局等运行时驱动，不是用户编辑，
//接收方据此避免把它当成需要回写的改动（否则会形成重建反馈）。
enum class TransformChangeSource : uint32
{
    Author = 0,
    Derived = 1,
};

//变换监听器，接收局部变换和父级变化通知
class ITransformListener
{
public:
    virtual ~ITransformListener() = default;

    //接收指定空间节点及其子树失效通知，并给出变更来源
    virtual void OnTransformChanged(World& world, EnsId ens, TransformChangeSource source) = 0;
};
