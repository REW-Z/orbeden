#include "Runtime/Native/RuntimeResourceBinds.h"

#include "Runtime/Native/NativeCall.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Texture2D.h"
#include "ResourceManager/ResourceManager.h"
#include "Profiler/Profiler.h"

#include <chrono>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>

namespace
{
    std::atomic<IWindow*> wakeWindow = nullptr;

    //开始托管代码段的 Core 性能采样
    void ORBEDEN_NATIVE_CALL NativeProfilerBeginSample(const uint8* name, int32 length, ProfileScopeAbi* scope)
    {
        if (!scope) return;
        *scope = {};
        if (!name || length <= 0 || !Profiler::IsCapturing()) return;

        std::string text(reinterpret_cast<const char*>(name), static_cast<usize>(length));
        scope->sample = Profiler::StartSample(text.c_str());
        scope->startedAtNanoseconds = static_cast<int64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    //结束托管代码段的 Core 性能采样
    void ORBEDEN_NATIVE_CALL NativeProfilerEndSample(ProfileScopeAbi* scope)
    {
        if (!scope || !scope->sample) return;
        int64 endedAtNanoseconds = static_cast<int64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        Profiler::StoreSample(nullptr, (endedAtNanoseconds - scope->startedAtNanoseconds) / 1000,
            static_cast<ProfileSample*>(scope->sample));
        *scope = {};
    }

    //唤醒主线程事件循环
    void ORBEDEN_NATIVE_CALL NativeWakeMainThread()
    {
        if (IWindow* window = wakeWindow.load(std::memory_order_acquire))
        {
            window->WakeEventLoop();
        }
    }

    //从 UTF-8 字节创建字符串
    std::string ReadUtf8Text(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return std::string();
        return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(length));
    }

    //把 UTF-8 字符串写入 C# 缓冲区
    int32 CopyText(const std::string& text, uint8* buffer, int32 bufferSize)
    {
        int32 byteCount = static_cast<int32>(text.size());
        if (buffer && bufferSize > 0 && byteCount > 0)
        {
            int32 copyCount = std::min(byteCount, bufferSize);
            std::memcpy(buffer, text.data(), static_cast<size_t>(copyCount));
        }

        return byteCount;
    }

    //获取仍然存活的原生对象
    Object* GetBoundObject(void* pointer)
    {
        Object* object = static_cast<Object*>(pointer);
        if (!object) return nullptr;

        Object* current = Object::FindObjectById(object->GetObjectId());
        return current == object ? object : nullptr;
    }

    //获取对象运行时 ID
    int32 ORBEDEN_NATIVE_CALL NativeObjectGetInstanceId(void* pointer)
    {
        Object* object = GetBoundObject(pointer);
        return object ? object->GetObjectId() : 0;
    }

    //读取对象持有的稳定资源 Key。
    int32 ORBEDEN_NATIVE_CALL NativeObjectGetResourceKey(void* pointer, uint8* buffer, int32 bufferSize)
    {
        Object* object = GetBoundObject(pointer);
        return object ? CopyText(object->GetInstanceId().GetPath(), buffer, bufferSize) : 0;
    }

    //判断对象是否存活
    uint8 ORBEDEN_NATIVE_CALL NativeObjectIsAlive(int32 instanceId)
    {
        return Object::IsObjectAlive(instanceId) ? 1 : 0;
    }

    //读取托管包装缓存
    void* ORBEDEN_NATIVE_CALL NativeObjectGetManagedWrapper(void* pointer)
    {
        Object* object = GetBoundObject(pointer);
        return object ? object->GetManagedWrapper() : nullptr;
    }

    //写入托管包装缓存
    void ORBEDEN_NATIVE_CALL NativeObjectSetManagedWrapper(void* pointer, void* wrapper, ManagedWrapperReleaseCallback release)
    {
        Object* object = GetBoundObject(pointer);
        if (object) object->SetManagedWrapper(wrapper, release);
    }

    //销毁对象
    uint8 ORBEDEN_NATIVE_CALL NativeObjectDestroy(void* pointer)
    {
        Object* object = GetBoundObject(pointer);
        return Object::DestroyObjectFromBinding(object) ? 1 : 0;
    }

    //释放未使用对象
    uint32 ORBEDEN_NATIVE_CALL NativeObjectUnloadUnusedObjects(const int32* roots, int32 count)
    {
        return Object::UnloadUnusedObjects(roots, count);
    }

    //判断托管包装是否参与未使用对象回收
    uint8 ORBEDEN_NATIVE_CALL NativeObjectCanCollectManagedWrapper(int32 objectId)
    {
        Object* object = Object::FindObjectById(objectId);
        return object && object->CanCollectManagedWrapper() ? 1 : 0;
    }

}

ObjectBind ObjectBind::Create()
{
    ObjectBind bind;
    bind.GetInstanceId = reinterpret_cast<void*>(&NativeObjectGetInstanceId);
    bind.IsAlive = reinterpret_cast<void*>(&NativeObjectIsAlive);
    bind.GetManagedWrapper = reinterpret_cast<void*>(&NativeObjectGetManagedWrapper);
    bind.SetManagedWrapper = reinterpret_cast<void*>(&NativeObjectSetManagedWrapper);
    bind.Destroy = reinterpret_cast<void*>(&NativeObjectDestroy);
    bind.UnloadUnusedObjects = reinterpret_cast<void*>(&NativeObjectUnloadUnusedObjects);
    bind.CanCollectManagedWrapper = reinterpret_cast<void*>(&NativeObjectCanCollectManagedWrapper);
    return bind;
}

ObjectExtensionBind ObjectExtensionBind::Create()
{
    ObjectExtensionBind bind;
    bind.GetResourceKey = reinterpret_cast<void*>(&NativeObjectGetResourceKey);
    return bind;
}


//创建异步操作函数表
AsyncOperationBindApi AsyncOperationBindApi::Create()
{
    wakeWindow.store(Application::Current() ? Application::Current()->GetWindow() : nullptr, std::memory_order_release);
    AsyncOperationBindApi api;
    api.WakeMainThread = reinterpret_cast<void*>(&NativeWakeMainThread);
    return api;
}

//创建 Core 性能采样函数表
ProfilerBindApi ProfilerBindApi::Create()
{
    ProfilerBindApi api;
    api.BeginSample = reinterpret_cast<void*>(&NativeProfilerBeginSample);
    api.EndSample = reinterpret_cast<void*>(&NativeProfilerEndSample);
    return api;
}
