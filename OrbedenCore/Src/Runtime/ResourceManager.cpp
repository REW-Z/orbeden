#include "Runtime/ResourceManager.h"

#include "Log/Log.h"
#include "Runtime/AssetPipeline.h"

#include <algorithm>
#include <unordered_map>

namespace
{
    struct ResourceRuntime
    {
    public:
        std::unordered_map<std::string, ResourceManager::ResourceRecord> records;
    };

    //获取资源运行时表
    ResourceRuntime& GetResourceRuntime()
    {
        static ResourceRuntime runtime;
        return runtime;
    }

    //获取资源总引用数
    uint32 GetTotalRefCount(const ResourceManager::ResourceRecord& record)
    {
        return record.manualRefCount + record.worldRefCount + record.dependencyRefCount;
    }

    //判断字符串前缀
    bool StartsWith(const std::string& text, const std::string& prefix)
    {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    //查找可写资源记录
    ResourceManager::ResourceRecord* FindRecordMutable(const std::string& key)
    {
        std::string resourceKey = ResourceManager::ToResourceKey(key);
        auto& records = GetResourceRuntime().records;
        auto it = records.find(resourceKey);
        return it == records.end() ? nullptr : &it->second;
    }

    //递归增加依赖引用
    void AddDependencyRefs(ResourceManager::ResourceRecord& record)
    {
        for (const std::string& dependencyKey : record.dependencies)
        {
            ResourceManager::ResourceRecord* dependency = FindRecordMutable(dependencyKey);
            if (!dependency) continue;

            dependency->dependencyRefCount++;
            AddDependencyRefs(*dependency);
        }
    }

    //递归释放依赖引用
    void ReleaseDependencyRefs(ResourceManager::ResourceRecord& record)
    {
        for (const std::string& dependencyKey : record.dependencies)
        {
            ResourceManager::ResourceRecord* dependency = FindRecordMutable(dependencyKey);
            if (!dependency || dependency->dependencyRefCount == 0) continue;

            dependency->dependencyRefCount--;
            ReleaseDependencyRefs(*dependency);
        }
    }

    //加载资源 Key 对应的资源记录。
    ResourceManager::ResourceRecord* LoadResourceRecord(Type* type, const std::string& key)
    {
        std::string resourceKey = ResourceManager::ToResourceKey(key);
        if (resourceKey.empty() || StartsWith(resourceKey, "world://") || StartsWith(resourceKey, "orphan://")) return nullptr;

        ResourceManager::ResourceRecord* record = FindRecordMutable(resourceKey);
        if (!record)
        {
            AssetCollection collection = AssetPipeline::ImportSource(ResourceManager::GetSourceKey(resourceKey));
            (void)collection;
            record = FindRecordMutable(resourceKey);
        }

        if (!record)
        {
            Log::Error(("Resource key is not registered: " + resourceKey).c_str());
            return nullptr;
        }

        if (type && record->object && !record->object->Is(type))
        {
            Log::Error(("Resource type mismatch: " + resourceKey).c_str());
            return nullptr;
        }

        return record;
    }
}

//加载资源并增加一次显式引用
Object* ResourceManager::Load(Type* type, const std::string& key)
{
    ResourceRecord* record = LoadResourceRecord(type, key);
    if (!record) return nullptr;

    record->manualRefCount++;
    AddDependencyRefs(*record);
    return record->object;
}

//加载资源并增加一次World引用
Object* ResourceManager::LoadWorldRef(Type* type, const std::string& key)
{
    ResourceRecord* record = LoadResourceRecord(type, key);
    if (!record) return nullptr;

    record->worldRefCount++;
    AddDependencyRefs(*record);
    return record->object;
}

//释放一次显式引用
void ResourceManager::Unload(const std::string& key)
{
    ResourceRecord* record = FindRecordMutable(key);
    if (!record || record->manualRefCount == 0) return;

    record->manualRefCount--;
    ReleaseDependencyRefs(*record);
}

//释放一次World引用
void ResourceManager::ReleaseWorldRef(const std::string& key)
{
    ResourceRecord* record = FindRecordMutable(key);
    if (!record || record->worldRefCount == 0) return;

    record->worldRefCount--;
    ReleaseDependencyRefs(*record);
}

//释放所有零引用资源
void ResourceManager::ReleaseZeroRef()
{
    auto& records = GetResourceRuntime().records;
    bool removed = true;
    while (removed)
    {
        removed = false;
        for (auto it = records.begin(); it != records.end();)
        {
            ResourceRecord& record = it->second;
            if (GetTotalRefCount(record) != 0)
            {
                ++it;
                continue;
            }

            if (record.object)
            {
                record.object->SetOwnership(Object::Ownership::None);
                Object::DestroyDetachedInstance(record.object);
            }
            it = records.erase(it);
            removed = true;
        }
    }
}

//释放所有资源，通常在应用退出时调用
void ResourceManager::Shutdown()
{
    auto& records = GetResourceRuntime().records;
    for (auto& pair : records)
    {
        ResourceRecord& record = pair.second;
        if (GetTotalRefCount(record) > 0)
        {
            Log::Warning(("Resource is still referenced during shutdown: " + record.key).c_str());
        }

        if (record.object)
        {
            record.object->SetOwnership(Object::Ownership::None);
            Object::DestroyDetachedInstance(record.object);
        }
    }

    records.clear();
}

//注册导入出来的资源对象
bool ResourceManager::RegisterObject(const std::string& key, Object* object)
{
    std::string resourceKey = ToResourceKey(key);
    if (resourceKey.empty() || !object) return false;
    if (StartsWith(resourceKey, "world://") || StartsWith(resourceKey, "orphan://")) return false;
    if (object->GetWorld()) return false;
    if (object->GetOwnership() == Object::Ownership::WorldOwned || object->GetOwnership() == Object::Ownership::OrphanOwned)
    {
        Log::Error(("Cannot register runtime-owned object as resource: " + resourceKey).c_str());
        return false;
    }
    if (object->GetInstanceId().GetPath() != resourceKey)
    {
        Log::Error(("Resource object id does not match key: " + resourceKey).c_str());
        return false;
    }

    auto& records = GetResourceRuntime().records;
    auto it = records.find(resourceKey);
    if (it != records.end())
    {
        bool sameObject = it->second.object == object;
        if (sameObject)
        {
            object->SetOwnership(Object::Ownership::ResourceOwned);
        }

        return sameObject;
    }

    object->SetOwnership(Object::Ownership::ResourceOwned);

    ResourceRecord record;
    record.key = resourceKey;
    record.object = object;
    record.type = object->GetType();
    records[resourceKey] = record;
    return true;
}

//注册资源对象之间的依赖关系
bool ResourceManager::RegisterDependency(const std::string& ownerKey, const std::string& dependencyKey)
{
    ResourceRecord* owner = FindRecordMutable(ownerKey);
    ResourceRecord* dependency = FindRecordMutable(dependencyKey);
    if (!owner || !dependency || owner == dependency) return false;

    std::string dependencyResourceKey = ToResourceKey(dependencyKey);
    if (std::find(owner->dependencies.begin(), owner->dependencies.end(), dependencyResourceKey) != owner->dependencies.end())
    {
        return true;
    }

    owner->dependencies.push_back(dependencyResourceKey);
    return true;
}

//查找已注册资源对象
Object* ResourceManager::FindLoaded(const std::string& key)
{
    ResourceRecord* record = FindRecordMutable(key);
    return record ? record->object : nullptr;
}

//查找已注册资源记录
const ResourceManager::ResourceRecord* ResourceManager::FindRecord(const std::string& key)
{
    return FindRecordMutable(key);
}

//获取资源总引用数
uint32 ResourceManager::GetRefCount(const std::string& key)
{
    ResourceRecord* record = FindRecordMutable(key);
    return record ? GetTotalRefCount(*record) : 0;
}

//转换为资源 Key。
std::string ResourceManager::ToResourceKey(const std::string& key)
{
    std::string result = key;
    std::replace(result.begin(), result.end(), '\\', '/');

    while (result.compare(0, 2, "./") == 0)
    {
        result.erase(0, 2);
    }

    return result;
}

//获取Key中的主资源路径
std::string ResourceManager::GetSourceKey(const std::string& key)
{
    std::string resourceKey = ToResourceKey(key);
    usize separator = resourceKey.find("//");
    return separator == std::string::npos ? resourceKey : resourceKey.substr(0, separator);
}

//获取Key中的子资源ID
std::string ResourceManager::GetSubId(const std::string& key)
{
    std::string resourceKey = ToResourceKey(key);
    usize separator = resourceKey.find("//");
    return separator == std::string::npos ? std::string() : resourceKey.substr(separator + 2);
}
