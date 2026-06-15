#pragma once

#include "Runtime/Object/Object.h"

#include <string>

//运行时资源管理器，按ObjectKey管理引用计数和释放
class ResourceManager
{
public:
    struct ResourceRecord
    {
    public:
        std::string key;
        Object* object = nullptr;
        Type* type = nullptr;
        uint32 manualRefCount = 0;
        uint32 sceneRefCount = 0;
        uint32 dependencyRefCount = 0;
        List<std::string> dependencies;
    };

    //加载资源并增加一次显式引用
    static Object* Load(Type* type, const std::string& key);

    //加载资源并增加一次显式引用
    template<typename T>
    static T* Load(const std::string& key)
    {
        static_assert(std::is_base_of_v<Object, T>);
        Object* object = Load(T::StaticType(), key);
        return object ? object->Cast<T>() : nullptr;
    }

    //加载资源并增加一次场景引用
    static Object* LoadSceneRef(Type* type, const std::string& key);

    //释放一次显式引用
    static void Unload(const std::string& key);

    //释放一次场景引用
    static void ReleaseSceneRef(const std::string& key);

    //释放所有零引用资源
    static void ReleaseZeroRef();

    //释放所有资源，通常在应用退出时调用
    static void Shutdown();

    //注册导入出来的资源对象
    static bool RegisterObject(const std::string& key, Object* object);

    //注册资源对象之间的依赖关系
    static bool RegisterDependency(const std::string& ownerKey, const std::string& dependencyKey);

    //查找已注册资源对象
    static Object* FindLoaded(const std::string& key);

    //查找已注册资源记录
    static const ResourceRecord* FindRecord(const std::string& key);

    //获取资源总引用数
    static uint32 GetRefCount(const std::string& key);

    //规范化资源Key
    static std::string NormalizeKey(const std::string& key);

    //获取Key中的主资源路径
    static std::string GetSourceKey(const std::string& key);

    //获取Key中的子资源ID
    static std::string GetSubId(const std::string& key);
};
