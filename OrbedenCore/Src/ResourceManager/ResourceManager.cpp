#include "ResourceManager/ResourceManager.h"

#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Runtime/AssetPipeline.h"
#include "Runtime/CookedAssetSerializer.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Skybox.h"
#include "Runtime/Object/Texture2D.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

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

    //按对象查找资源记录
    ResourceManager::ResourceRecord* FindRecordByObject(Object* object)
    {
        if (!object) return nullptr;

        auto& records = GetResourceRuntime().records;
        for (auto& pair : records)
        {
            if (pair.second.object == object) return &pair.second;
        }

        return nullptr;
    }

    //正在加载中的资源 Key，防止跨文件引用成环时无限递归
    std::unordered_set<std::string>& GetLoadingAssetKeys()
    {
        static std::unordered_set<std::string> loadingAssetKeys;
        return loadingAssetKeys;
    }

    //优先按打包产物加载，缺失或读取失败时返回 false
    bool LoadCookedAsset(const std::string& resourceKey);

    //加载资源 Key 对应的资源记录
    ResourceManager::ResourceRecord* LoadResourceRecord(Type* type, const std::string& key)
    {
        std::string resourceKey = ResourceManager::ToResourceKey(key);
        if (resourceKey.empty() || StartsWith(resourceKey, "world://") || StartsWith(resourceKey, "orphan://")) return nullptr;

        ResourceManager::ResourceRecord* record = FindRecordMutable(resourceKey);
        if (!record)
        {
            //打包产物按对象存放，用完整 Key 查找；缺失时回退到源文件导入，两者都没有才报错。
            if (!LoadCookedAsset(resourceKey))
            {
                AssetCollection collection = AssetPipeline::ImportSource(ResourceManager::GetSourceKey(resourceKey));
                (void)collection;
            }

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

    //优先按打包产物加载，缺失或读取失败时返回 false
    bool LoadCookedAsset(const std::string& resourceKey)
    {
        std::string blobPath = CookedAssetSerializer::GetBlobPath(resourceKey);
        if (!FileSystem::Exist(blobPath)) return false;

        if (!GetLoadingAssetKeys().insert(resourceKey).second) return false;

        List<std::string> externalRefs;
        std::string error;
        bool loaded = CookedAssetSerializer::Read(blobPath, externalRefs, error);
        if (!loaded)
        {
            Log::Error(error.c_str());
        }
        else
        {
            //补齐跨文件引用，与导入期的即时导入行为保持一致。
            for (const std::string& referenceKey : externalRefs)
            {
                LoadResourceRecord(nullptr, referenceKey);
            }
        }

        GetLoadingAssetKeys().erase(resourceKey);
        return loaded;
    }
}

//建立资源系统所需的文件系统依赖
bool ResourceManager::OnInitialize(Application& app)
{
    return app.GetSystem<FileSystem>() != nullptr;
}

//关闭应用时释放全部资源
void ResourceManager::OnShutdown()
{
    Shutdown();
}

//加载资源对象
Object* ResourceManager::Load(Type* type, const std::string& key)
{
    ResourceRecord* record = LoadResourceRecord(type, key);
    return record ? record->object : nullptr;
}

//释放所有资源
void ResourceManager::Shutdown()
{
    auto& records = GetResourceRuntime().records;
    for (auto& pair : records)
    {
        ResourceRecord& record = pair.second;
        if (record.object)
        {
            record.object->SetOwnership(Object::Ownership::None);
            Object::DestroyDetachedInstance(record.object);
        }
    }

    records.clear();
}

//把已加载资源的 Key 迁移到新路径，对象身份保持不变
uint32 ResourceManager::RemapKeys(const std::string& oldKey, const std::string& newKey, bool prefix)
{
    std::string source = ToResourceKey(oldKey);
    std::string target = ToResourceKey(newKey);
    if (source.empty() || target.empty() || source == target) return 0;

    //判断某个 Key 是否落在被迁移的路径下
    auto matches = [&](const std::string& key)
    {
        std::string keySource = GetSourceKey(key);
        if (keySource == source) return true;
        return prefix && keySource.size() > source.size()
            && keySource.compare(0, source.size(), source) == 0 && keySource[source.size()] == '/';
    };

    auto& records = GetResourceRuntime().records;
    //先收集命中的记录：边遍历边改哈希表会让迭代器失效
    List<std::string> matched;
    for (const auto& pair : records)
    {
        if (matches(pair.first)) matched.push_back(pair.first);
    }

    uint32 moved = 0;
    for (const std::string& key : matched)
    {
        ResourceRecord record = records[key];
        std::string mapped = target + key.substr(source.size());
        //对象按新 Key 重新登记：场景里已存在的引用仍然指向同一个实例，不能销毁重建
        if (record.object) record.object->ChangeInstancePath(StringId(mapped));
        record.key = mapped;
        records.erase(key);
        records[mapped] = std::move(record);
        ++moved;
    }

    //依赖表里记录的旧 Key 一并改到新 Key
    for (auto& pair : records)
    {
        for (std::string& dependency : pair.second.dependencies)
        {
            if (matches(dependency)) dependency = target + dependency.substr(source.size());
        }
    }

    return moved;
}

//释放指定资源
bool ResourceManager::Unload(const std::string& key)
{
    std::string resourceKey = ToResourceKey(key);
    auto& records = GetResourceRuntime().records;
    auto it = records.find(resourceKey);
    if (it == records.end()) return false;

    //清理其它资源记录中的依赖关系。
    for (auto& pair : records)
    {
        List<std::string>& dependencies = pair.second.dependencies;
        dependencies.erase(std::remove(dependencies.begin(), dependencies.end(), resourceKey), dependencies.end());
    }

    ResourceRecord& record = it->second;
    if (record.object)
    {
        record.object->SetOwnership(Object::Ownership::None);
        Object::DestroyDetachedInstance(record.object);
    }

    records.erase(it);
    return true;
}

//强制重新导入已加载资源，导入器复用原对象，因此对象身份与引用都不变
uint32 ResourceManager::Reimport(const std::string& key, bool prefix, const std::string& settingsTable)
{
    const std::string target = key.empty() ? std::string() : GetSourceKey(ToResourceKey(key));

    //先收集命中的源文件 Key：边遍历边导入会让记录表迭代器失效
    List<std::string> sources;
    for (const auto& pair : GetResourceRuntime().records)
    {
        const std::string source = GetSourceKey(pair.first);
        if (source.empty()) continue;
        bool matched = target.empty() || source == target
            || (prefix && source.size() > target.size()
                && source.compare(0, target.size(), target) == 0 && source[target.size()] == '/');
        if (matched && std::find(sources.begin(), sources.end(), source) == sources.end()) sources.push_back(source);
    }

    //重新导入读源文件并原地写入原对象，各资源按自身脏标记重建 GPU 资源。
    //设置表按源文件 Key 逐个取用，因此整目录重导也能各自带上自己的导入设置。
    for (const std::string& source : sources)
    {
        AssetCollection collection = AssetPipeline::ImportSource(source, AssetImportSettings::Lookup(settingsTable, source));
        (void)collection;
    }
    return static_cast<uint32>(sources.size());
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
        if (sameObject) object->SetOwnership(Object::Ownership::ResourceOwned);
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

//递归标记对象依赖
void ResourceManager::MarkObjectGraph(Object* object, std::unordered_set<int32>& marked)
{
    if (!object || object->Is(Component::StaticType())) return;
    if (!marked.insert(object->GetObjectId()).second) return;

    //网格只持有几何，材质由渲染器引用，不再进入网格的资源依赖图
    if (Material* material = object->Cast<Material>())
    {
        MarkObjectGraph(material->shader.Get(), marked);
        for (const MaterialTextureSlot& slot : material->textureSlots)
        {
            MarkObjectGraph(slot.texture.Get(), marked);
        }
    }
    else if (Skybox* skybox = object->Cast<Skybox>())
    {
        MarkObjectGraph(skybox->right.Get(), marked);
        MarkObjectGraph(skybox->left.Get(), marked);
        MarkObjectGraph(skybox->top.Get(), marked);
        MarkObjectGraph(skybox->bottom.Get(), marked);
        MarkObjectGraph(skybox->front.Get(), marked);
        MarkObjectGraph(skybox->back.Get(), marked);
    }

    ResourceRecord* record = FindRecordByObject(object);
    if (!record) return;

    for (const std::string& dependencyKey : record->dependencies)
    {
        MarkObjectGraph(FindLoaded(dependencyKey), marked);
    }
}

//释放未被标记的资源对象
uint32 ResourceManager::ReleaseUnmarkedObjects(const std::unordered_set<int32>& marked)
{
    auto& records = GetResourceRuntime().records;
    uint32 removedCount = 0;
    bool removed = true;

    while (removed)
    {
        removed = false;
        for (auto it = records.begin(); it != records.end();)
        {
            ResourceRecord& record = it->second;
            Object* object = record.object;
            if (object && marked.find(object->GetObjectId()) != marked.end())
            {
                ++it;
                continue;
            }

            if (object)
            {
                object->SetOwnership(Object::Ownership::None);
                Object::DestroyDetachedInstance(object);
                removedCount++;
            }

            it = records.erase(it);
            removed = true;
        }
    }

    return removedCount;
}

//销毁已加载资源对象
bool ResourceManager::DestroyObject(Object* object)
{
    ResourceRecord* record = FindRecordByObject(object);
    if (!record || !record->object) return false;

    std::string key = record->key;
    record->object->SetOwnership(Object::Ownership::None);
    bool destroyed = Object::DestroyDetachedInstance(record->object);
    GetResourceRuntime().records.erase(key);
    return destroyed;
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

//转换为资源 Key
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
