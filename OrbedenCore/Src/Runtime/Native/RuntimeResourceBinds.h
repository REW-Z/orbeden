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
    void* CanCollectManagedWrapper = nullptr;

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


//后台函数的主线程唤醒接口
struct AsyncOperationBindApi
{
    void* WakeMainThread = nullptr;

    //创建异步操作函数表
    static AsyncOperationBindApi Create();
};

//游戏输入查询函数表
struct InputBindApi
{
    void* Key = nullptr;
    void* KeyDown = nullptr;
    void* KeyUp = nullptr;
    void* MousePos = nullptr;
    void* MouseMov = nullptr;
    void* MouseWheel = nullptr;

    //创建输入查询函数表
    static InputBindApi Create();
};

#pragma pack(pop)
ORBEDEN_ASSERT_NATIVE_API_TABLE(InputBindApi, 6);
ORBEDEN_ASSERT_NATIVE_API_TABLE(AsyncOperationBindApi, 1);
ORBEDEN_ASSERT_NATIVE_API_TABLE(ObjectBind, 7);
ORBEDEN_ASSERT_NATIVE_API_TABLE(ObjectExtensionBind, 1);
