#pragma once

#include "Defines/types.h"
#include "Runtime/EnsId.h"
#include "Runtime/Native/NativeApiAbi.h"

#pragma pack(push, 8)

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
    void* GetContentRoot = nullptr;
    void* GetContentFilePath = nullptr;

    //创建 PathDefines 函数表。
    static PathDefinesBind Create();
};

//Ens 原生函数表。
struct EnsBind
{
public:
    void* IsAlive = nullptr;
    void* GetLocalActive = nullptr;
    void* GetWorldActive = nullptr;
    void* SetLocalActive = nullptr;
    void* GetName = nullptr;
    void* SetName = nullptr;

    //创建 Ens 函数表。
    static EnsBind Create();
};


#pragma pack(pop)
ORBEDEN_ASSERT_NATIVE_API_TABLE(WorldBind, 4);
ORBEDEN_ASSERT_NATIVE_API_TABLE(PathDefinesBind, 2);
ORBEDEN_ASSERT_NATIVE_API_TABLE(EnsBind, 6);
