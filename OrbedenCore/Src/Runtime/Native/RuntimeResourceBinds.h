#pragma once

#include "Defines/types.h"
#include "Runtime/Native/NativeApiAbi.h"

#pragma pack(push, 8)

//通用对象函数表。
struct ObjectBind
{
public:
    void* GetInstanceId = nullptr;
    void* IsAlive = nullptr;
    void* GetManagedWrapper = nullptr;
    void* SetManagedWrapper = nullptr;
    void* Destroy = nullptr;
    void* UnloadUnusedObjects = nullptr;

    //创建 Object 函数表。
    static ObjectBind Create();
};

//通用对象增量函数表。
struct ObjectExtensionBind
{
public:
    void* GetResourceKey = nullptr;

    //创建 Object 增量函数表。
    static ObjectExtensionBind Create();
};


#pragma pack(pop)
ORBEDEN_ASSERT_NATIVE_API_TABLE(ObjectBind, 6);
ORBEDEN_ASSERT_NATIVE_API_TABLE(ObjectExtensionBind, 1);
