#pragma once

#include "Defines/types.h"

// Ens 脚本绑定函数表。
struct EnsBind
{
public:
    void* IsAlive = nullptr;
    void* GetName = nullptr;
    void* SetName = nullptr;
    void* HasSpaceComponent = nullptr;
    void* HasStaticMeshRenderer = nullptr;

    // 创建 Ens 绑定函数表。
    static EnsBind Create();
};

// SpaceComponent 脚本绑定函数表。
struct SpaceComponentBind
{
public:
    void* GetParent = nullptr;
    void* SetParent = nullptr;
    void* GetLocalPosition = nullptr;
    void* SetLocalPosition = nullptr;
    void* GetLocalRotation = nullptr;
    void* SetLocalRotation = nullptr;
    void* GetLocalScale = nullptr;
    void* SetLocalScale = nullptr;
    void* GetWorldPosition = nullptr;
    void* GetWorldRotation = nullptr;

    // 创建 SpaceComponent 绑定函数表。
    static SpaceComponentBind Create();
};

// StaticMeshRenderer 脚本绑定函数表。
struct StaticMeshRendererBind
{
public:
    void* GetEnabled = nullptr;
    void* SetEnabled = nullptr;
    void* GetCastShadows = nullptr;
    void* SetCastShadows = nullptr;
    void* GetReceiveShadows = nullptr;
    void* SetReceiveShadows = nullptr;

    // 创建 StaticMeshRenderer 绑定函数表。
    static StaticMeshRendererBind Create();
};
