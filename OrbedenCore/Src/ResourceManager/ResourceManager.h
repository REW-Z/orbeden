#pragma once

#include "Application.h"
#include "Runtime/Object/Object.h"

#include <string>
#include <unordered_set>

//运行时资源管理器，按资源 Key 保存已加载对象
class ResourceManager : public IEngineSystem
{
public:
    struct ResourceRecord
    {
    public:
        std::string key;
        Object* object = nullptr;
        Type* type = nullptr;
        List<std::string> dependencies;
    };

    //建立资源系统所需的文件系统依赖
    bool OnInitialize(Application& app) override;

    //关闭应用时释放全部资源
    void OnShutdown() override;

    //加载资源对象
    static Object* Load(Type* type, const std::string& key);

    //加载资源对象
    template<typename T>
    static T* Load(const std::string& key)
    {
        static_assert(std::is_base_of_v<Object, T>);
        Object* object = Load(T::StaticType(), key);
        return object ? object->Cast<T>() : nullptr;
    }

    //释放所有资源，通常在应用退出时调用
    static void Shutdown();

    //释放指定资源
    static bool Unload(const std::string& key);

    //强制重新导入已加载资源；key 为空时覆盖全部，prefix 为真时连带子路径
    //settingsTable 是 "源Key\t设置名\t值" 行表；为空时全部按语义自动推断
    static uint32 Reimport(const std::string& key, bool prefix, const std::string& settingsTable = "");

    //把已加载资源的 Key 迁移到新路径，对象身份保持不变，返回迁移的记录数
    static uint32 RemapKeys(const std::string& oldKey, const std::string& newKey, bool prefix);

    //注册导入出来的资源对象
    static bool RegisterObject(const std::string& key, Object* object);

    //注册资源对象之间的依赖关系
    static bool RegisterDependency(const std::string& ownerKey, const std::string& dependencyKey);

    //递归标记对象依赖
    static void MarkObjectGraph(Object* object, std::unordered_set<int32>& marked);

    //释放未被标记的资源对象
    static uint32 ReleaseUnmarkedObjects(const std::unordered_set<int32>& marked);

    //销毁已加载资源对象
    static bool DestroyObject(Object* object);

    //查找已注册资源对象
    static Object* FindLoaded(const std::string& key);

    //查找已注册资源记录
    static const ResourceRecord* FindRecord(const std::string& key);

    //规范化资源Key
    static std::string ToResourceKey(const std::string& key);

    //获取Key中的主资源路径
    static std::string GetSourceKey(const std::string& key);

    //获取Key中的子资源ID
    static std::string GetSubId(const std::string& key);
};
