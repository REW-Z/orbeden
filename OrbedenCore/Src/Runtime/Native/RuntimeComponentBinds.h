#pragma once

#include "Defines/types.h"
#include "Runtime/EnsId.h"

//World 原生函数表。
struct WorldBind
{
public:
    void* CreateEns = nullptr;
    void* CreateEnsWithStableId = nullptr;
    void* FindEns = nullptr;
    void* DestroyEns = nullptr;

    //创建 World 函数表。
    static WorldBind Create();
};

//PathDefines 原生函数表。
struct PathDefinesBind
{
public:
    void* GetProjectRoot = nullptr;
    void* GetProjectFilePath = nullptr;

    //创建 PathDefines 函数表。
    static PathDefinesBind Create();
};

//Ens 原生函数表。
struct EnsBind
{
public:
    void* IsAlive = nullptr;
    void* GetName = nullptr;
    void* SetName = nullptr;
    void* HasSpaceComponent = nullptr;
    void* HasStaticMeshRenderer = nullptr;
    void* AddStaticMeshRenderer = nullptr;

    //创建 Ens 函数表。
    static EnsBind Create();
};

//SpaceComponent 原生函数表。
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

    //创建 SpaceComponent 函数表。
    static SpaceComponentBind Create();
};

//StaticMeshRenderer 原生函数表。
struct StaticMeshRendererBind
{
public:
    void* GetEnabled = nullptr;
    void* SetEnabled = nullptr;
    void* GetMesh = nullptr;
    void* SetMesh = nullptr;
    void* GetCastShadows = nullptr;
    void* SetCastShadows = nullptr;
    void* GetReceiveShadows = nullptr;
    void* SetReceiveShadows = nullptr;

    //创建 StaticMeshRenderer 函数表。
    static StaticMeshRendererBind Create();
};
