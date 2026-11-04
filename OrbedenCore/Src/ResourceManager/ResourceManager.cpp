#include "ResourceManager/ResourceManager.h"
#include "Platform/Window.h"

#include "FileSystem/FileSystem.h"
#include "FileSystem/PathDefines.h"
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
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <mutex>
#include <thread>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
    constexpr uint32 MaxLoadingThreads = 4;

    struct ResourceRuntime
    {
    public:
        std::unordered_map<std::string, ResourceManager::ResourceRecord> records;
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<std::shared_ptr<ResourceLoadOperation>> queue;
        std::unordered_map<std::string, std::shared_ptr<ResourceLoadOperation>> loads;
        List<std::shared_ptr<ResourceLoadOperation>> pending;
        List<std::weak_ptr<ResourceLoadOperation>> operations;
        List<std::thread> workers;
#ifdef _WIN32
        uint32 mainThreadId = 0;
#else
        IWindow* window = nullptr;
#endif
        bool stopping = true;
        uint64 revision = 0;
        List<std::tuple<uint64, std::string, bool>> invalidations;
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
        if (auto* state = ResourceLoadOperation::Current())
        {
            auto found = state->records.find(resourceKey);
            return found == state->records.end() ? nullptr : &found->second;
        }
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

    //收集指定范围内已加载资源涉及的源文件 Key 并去重；target 为空表示全部
    List<std::string> CollectMatchingSources(const std::string& target, bool prefix)
    {
        List<std::string> sources;
        for (const auto& pair : GetResourceRuntime().records)
        {
            const std::string source = ResourceManager::GetSourceKey(pair.first);
            if (source.empty()) continue;
            bool matched = target.empty() || source == target
                || (prefix && source.size() > target.size()
                    && source.compare(0, target.size(), target) == 0 && source[target.size()] == '/');
            if (matched && std::find(sources.begin(), sources.end(), source) == sources.end()) sources.push_back(source);
        }
        return sources;
    }

    //正在加载中的资源 Key，防止跨文件引用成环时无限递归
    std::unordered_set<std::string>& GetLoadingAssetKeys()
    {
        if (auto* state = ResourceLoadOperation::Current())
        {
            return state->loadingKeys;
        }
        static std::unordered_set<std::string> loadingAssetKeys;
        return loadingAssetKeys;
    }

    //优先按打包产物加载，缺失或读取失败时返回 false
    bool LoadCookedAsset(const std::string& resourceKey, bool reportErrors);

    //加载资源 Key 对应的资源记录
    ResourceManager::ResourceRecord* LoadResourceRecord(Type* type, const std::string& key, bool reportErrors = true)
    {
        std::string resourceKey = ResourceManager::ToResourceKey(key);
        if (resourceKey.empty() || StartsWith(resourceKey, "world://") || StartsWith(resourceKey, "orphan://"))
        {
            return nullptr;
        }

        ResourceManager::ResourceRecord* record = FindRecordMutable(resourceKey);
        if (!record)
        {
            //打包产物按对象存放，用完整 Key 查找；缺失时回退到源文件导入，两者都没有才报错。
            if (!LoadCookedAsset(resourceKey, reportErrors))
            {
                std::string sourceKey = ResourceManager::GetSourceKey(resourceKey);
                auto* state = ResourceLoadOperation::Current();
                std::string sourcePath = state ? state->GetFilePath(sourceKey) : PathDefines::GetContentFilePath(sourceKey);
                if (!reportErrors && !FileSystem::Exist(sourcePath)
                    && AssetPipeline::SelectImporter(sourceKey) != AssetImporter::Glsl)
                {
                    return nullptr;
                }
                AssetCollection collection = AssetPipeline::ImportSource(sourceKey);
                if (!collection.Succeeded())
                {
                    if (auto* state = ResourceLoadOperation::Current())
                    {
                        state->error = collection.errors.front();
                    }
                }
            }

            record = FindRecordMutable(resourceKey);
        }

        if (!record)
        {
            if (reportErrors)
            {
                Log::Error(("Resource key is not registered: " + resourceKey).c_str());
            }
            return nullptr;
        }

        if (type && record->object && !record->object->Is(type))
        {
            if (reportErrors)
            {
                Log::Error(("Resource type mismatch: " + resourceKey).c_str());
            }
            return nullptr;
        }

        return record;
    }

    //优先按打包产物加载，缺失或读取失败时返回 false
    bool LoadCookedAsset(const std::string& resourceKey, bool reportErrors)
    {
        std::string blobPath = CookedAssetSerializer::GetBlobPath(resourceKey);
        if (!FileSystem::Exist(blobPath))
        {
            return false;
        }

        if (!GetLoadingAssetKeys().insert(resourceKey).second)
        {
            return false;
        }

        List<std::string> externalRefs;
        std::string error;
        bool loaded = CookedAssetSerializer::Read(blobPath, externalRefs, error);
        if (!loaded)
        {
            if (reportErrors)
            {
                Log::Error(error.c_str());
            }
            else
            {
                Log::Warning(error.c_str());
            }
        }
        else
        {
            //补齐跨文件引用，与导入期的即时导入行为保持一致。
            for (const std::string& referenceKey : externalRefs)
            {
                if (!LoadResourceRecord(nullptr, referenceKey, reportErrors) && !reportErrors)
                {
                    Log::Warning(("Unresolved cooked resource reference: " + resourceKey + " -> " + referenceKey).c_str());
                }
                ResourceManager::RegisterDependency(resourceKey, referenceKey);
            }
        }

        GetLoadingAssetKeys().erase(resourceKey);
        return loaded;
    }
}

//建立资源系统所需的文件系统依赖
bool ResourceManager::OnInitialize(Application& app)
{
    if (!app.GetSystem<FileSystem>())
    {
        return false;
    }
    auto& runtime = GetResourceRuntime();
#ifdef _WIN32
    runtime.mainThreadId = GetCurrentThreadId();
#else
    runtime.window = app.GetWindow();
#endif
    runtime.stopping = false;
    return true;
}

//关闭应用时释放全部资源
void ResourceManager::OnShutdown()
{
    StopLoading();
    Shutdown();
}

//加载资源对象
Object* ResourceManager::Load(Type* type, const std::string& key)
{
    //完成已准备的资源登记
    if (!ResourceLoadOperation::Current())
    {
        ProcessLoads(std::numeric_limits<float64>::max());
    }
    ResourceRecord* record = LoadResourceRecord(type, key);
    return record ? record->object : nullptr;
}

//尝试加载字段引用
Object* ResourceManager::TryLoad(Type* type, const std::string& key)
{
    //完成已准备的资源登记
    if (!ResourceLoadOperation::Current())
    {
        ProcessLoads(std::numeric_limits<float64>::max());
    }
    ResourceRecord* record = LoadResourceRecord(type, key, false);
    return record ? record->object : nullptr;
}

//释放所有资源
void ResourceManager::Shutdown()
{
    InvalidateLoads("", true);
    DrainLoads();
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
    InvalidateLoads(oldKey, prefix);
    InvalidateLoads(newKey, prefix);
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
    InvalidateLoads(key, false);
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
    InvalidateLoads(key, prefix);
    const std::string target = key.empty() ? std::string() : GetSourceKey(ToResourceKey(key));

    //先收集命中的源文件 Key：边遍历边导入会让记录表迭代器失效
    List<std::string> sources = CollectMatchingSources(target, prefix);

    //重新导入读源文件并原地写入原对象，各资源按自身脏标记重建 GPU 资源。
    //设置表按源文件 Key 逐个取用，因此整目录重导也能各自带上自己的导入设置。
    for (const std::string& source : sources)
    {
        AssetCollection collection = AssetPipeline::ImportSource(source, AssetImportSettings::Lookup(settingsTable, source));
        (void)collection;
    }
    return static_cast<uint32>(sources.size());
}

//收集已加载资源涉及的源文件 Key，去重后按序数升序排列；调用方按此逐个判定是否需要重导
List<std::string> ResourceManager::CollectLoadedSources()
{
    List<std::string> sources = CollectMatchingSources(std::string(), false);
    std::sort(sources.begin(), sources.end());
    return sources;
}

//注册导入出来的资源对象
bool ResourceManager::RegisterObject(const std::string& key, Object* object)
{
    std::string resourceKey = ToResourceKey(key);
    if (resourceKey.empty() || !object)
    {
        return false;
    }
    if (StartsWith(resourceKey, "world://") || StartsWith(resourceKey, "orphan://"))
    {
        return false;
    }
    if (object->GetWorld())
    {
        return false;
    }
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

    auto* state = ResourceLoadOperation::Current();
    auto& records = state ? state->records : GetResourceRuntime().records;
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
    std::string dependencyResourceKey = ToResourceKey(dependencyKey);
    if (!owner || owner->key == dependencyResourceKey || dependencyResourceKey.empty())
    {
        return false;
    }
    if (!ResourceLoadOperation::Current() && !FindRecordMutable(dependencyResourceKey))
    {
        return false;
    }
    if (std::find(owner->dependencies.begin(), owner->dependencies.end(), dependencyResourceKey) == owner->dependencies.end())
    {
        owner->dependencies.push_back(dependencyResourceKey);
    }
    return true;
}

//移除指定资源依赖
bool ResourceManager::UnregisterDependency(const std::string& ownerKey, const std::string& dependencyKey)
{
    ResourceRecord* owner = FindRecordMutable(ownerKey);
    return owner && std::erase(owner->dependencies, ToResourceKey(dependencyKey)) != 0;
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
    if (!object->IsPublished())
    {
        GetResourceRuntime().records.erase(key);
        Object::DestroyUnpublishedResource(object);
        return true;
    }
    InvalidateLoads(key, false);
    record->object->SetOwnership(Object::Ownership::None);
    bool destroyed = Object::DestroyDetachedInstance(record->object);
    GetResourceRuntime().records.erase(key);
    return destroyed;
}

//查找已注册资源对象
Object* ResourceManager::FindLoaded(const std::string& key)
{
    ResourceRecord* record = FindRecordMutable(key);
    return record && (ResourceLoadOperation::Current() || record->object->IsPublished()) ? record->object : nullptr;
}

//查找已注册资源记录
const ResourceManager::ResourceRecord* ResourceManager::FindRecord(const std::string& key)
{
    ResourceRecord* record = FindRecordMutable(key);
    return record && (ResourceLoadOperation::Current() || record->object->IsPublished()) ? record : nullptr;
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

//创建或复用资源加载请求
void ResourceManager::StartLoad(const std::shared_ptr<ResourceLoadOperation>& operation)
{
    auto& runtime = GetResourceRuntime();
    runtime.operations.push_back(operation);
    if (runtime.stopping)
    {
        operation->error = "Resource loading is not running.";
        operation->finished = true;
        return;
    }
    if (Object* object = FindLoaded(operation->key))
    {
        operation->finished = true;
        if (!operation->type || object->Is(operation->type))
        {
            operation->resultId = object->GetObjectId();
        }
        else
        {
            operation->error = "Resource type mismatch: " + operation->key;
        }
        return;
    }

    //复用同一资源或源文件的后台构建
    std::string jobKey = FileSystem::Exist(CookedAssetSerializer::GetBlobPath(operation->key))
        ? "object:" + operation->key : "source:" + GetSourceKey(operation->key);
    auto found = runtime.loads.find(jobKey);
    if (found != runtime.loads.end())
    {
        auto state = found->second->state;
        if (!state->invalidated
            && (!state->prepared.load(std::memory_order_acquire) || state->error.empty()))
        {
            operation->state = std::move(state);
        }
    }
    if (!operation->state)
    {
        //按需启动有限数量的加载线程
        if (runtime.workers.empty())
        {
            uint32 count = std::clamp(std::thread::hardware_concurrency(), 1u, MaxLoadingThreads);
            runtime.workers.reserve(count);
            try
            {
                for (uint32 index = 0; index < count; ++index)
                {
                    runtime.workers.emplace_back(&ResourceManager::LoadingLoop);
                }
            }
            catch (...)
            {
                StopLoading();
                runtime.stopping = false;
                throw;
            }
        }

        operation->state = std::make_shared<ResourceLoadOperation::State>(operation->key);
        operation->state->revision = runtime.revision;
        runtime.loads[jobKey] = operation;
        {
            std::lock_guard lock(runtime.mutex);
            runtime.queue.push_back(operation);
        }
        runtime.changed.notify_one();
    }
    runtime.pending.push_back(operation);
}

//异步加载指定类型的资源
std::shared_ptr<ResourceLoadOperation> ResourceManager::LoadAsync(Type* type, const std::string& key)
{
    auto operation = std::shared_ptr<ResourceLoadOperation>(new ResourceLoadOperation(key, type));
    StartLoad(operation);
    return operation;
}

//消费后台资源加载队列
void ResourceManager::LoadingLoop()
{
    auto& runtime = GetResourceRuntime();
    for (;;)
    {
        std::shared_ptr<ResourceLoadOperation> operation;
        {
            std::unique_lock lock(runtime.mutex);
            runtime.changed.wait(lock, [&runtime] { return runtime.stopping || !runtime.queue.empty(); });
            if (runtime.queue.empty() && runtime.stopping)
            {
                break;
            }
            operation = std::move(runtime.queue.front());
            runtime.queue.pop_front();
        }

        //交接后台构建结果
        operation->state->Prepare();
        {
            std::lock_guard lock(runtime.mutex);
            operation->state->prepared.store(true, std::memory_order_release);
        }
        runtime.changed.notify_all();
#ifdef _WIN32
        PostThreadMessageW(runtime.mainThreadId, WM_NULL, 0, 0);
#else
        if (runtime.window)
        {
            runtime.window->WakeEventLoop();
        }
#endif
    }
}

//推进主线程资源发布
void ResourceManager::ProcessLoads(float64 budgetMilliseconds)
{
    auto& runtime = GetResourceRuntime();

    //优先完成已经开始的资源登记
    auto committing = std::find_if(runtime.pending.begin(), runtime.pending.end(), [](const auto& operation)
    {
        auto state = operation->state;
        return state->finalized && !state->committed && !state->invalidated;
    });
    if (committing != runtime.pending.end())
    {
        std::rotate(runtime.pending.begin(), committing, std::next(committing));
    }

    auto start = std::chrono::steady_clock::now();
    for (const auto& operation : runtime.pending)
    {
        auto state = operation->state;
        if (!state->prepared.load(std::memory_order_acquire))
        {
            continue;
        }

        //检查构建期间发生的资源变更
        for (const auto& [revision, source, prefix] : runtime.invalidations)
        {
            if (revision <= state->revision)
            {
                continue;
            }
            for (const auto& [key, record] : state->records)
            {
                std::string candidate = GetSourceKey(key);
                if (source.empty() || candidate == source || (prefix && StartsWith(candidate, source + "/")))
                {
                    state->invalidated = true;
                    break;
                }
            }
        }
        state->revision = runtime.revision;
        if (state->invalidated)
        {
            state->error = "Resource request was invalidated: " + operation->key;
        }

        //提交已经准备完成的资源
        if (state->error.empty() && !state->committed)
        {
            float64 elapsed = std::chrono::duration<float64, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (elapsed >= budgetMilliseconds)
            {
                break;
            }
            try
            {
                if (!state->Commit(budgetMilliseconds - elapsed))
                {
                    break;
                }
            }
            catch (const std::exception& exception)
            {
                state->error = exception.what();
            }
            catch (...)
            {
                state->error = "Resource publication failed with a non-standard exception.";
            }
        }

        //完成各请求的类型检查
        operation->error = state->error;
        if (operation->error.empty())
        {
            Object* object = FindLoaded(operation->key);
            if (object && (!operation->type || object->Is(operation->type)))
            {
                operation->resultId = object->GetObjectId();
            }
            else
            {
                operation->error = "Resource is missing or has a different type: " + operation->key;
            }
        }
        else
        {
            state->Discard();
        }
        operation->finished = true;
        operation->state.reset();
    }

    //释放完成请求的调度引用
    std::erase_if(runtime.pending, [](const auto& operation) { return operation->finished; });
    std::erase_if(runtime.loads, [](const auto& pair) { return pair.second->finished; });
    std::erase_if(runtime.operations, [](const auto& operation) { return operation.expired(); });
    uint64 oldest = runtime.revision;
    for (const auto& operation : runtime.pending)
    {
        oldest = std::min(oldest, operation->state->revision);
    }
    std::erase_if(runtime.invalidations, [oldest](const auto& entry) { return std::get<0>(entry) <= oldest; });
}

//等待并完成已有请求
void ResourceManager::CompleteLoad(const std::shared_ptr<ResourceLoadOperation>& operation)
{
    auto& runtime = GetResourceRuntime();
    while (!operation->finished)
    {
        if (!operation->state->prepared.load(std::memory_order_acquire))
        {
            std::unique_lock lock(runtime.mutex);
            auto queued = std::find_if(runtime.queue.begin(), runtime.queue.end(), [&operation](const auto& loading)
            {
                return loading->state == operation->state;
            });
            if (queued != runtime.queue.end() && queued != runtime.queue.begin())
            {
                auto loading = *queued;
                runtime.queue.erase(queued);
                runtime.queue.push_front(std::move(loading));
            }
            runtime.changed.wait(lock, [&operation] { return operation->state->prepared.load(std::memory_order_acquire); });
        }
        ProcessLoads(std::numeric_limits<float64>::max());
    }
}

//等待后台准备结束并处理结果
void ResourceManager::DrainLoads()
{
    auto& runtime = GetResourceRuntime();
    while (!runtime.pending.empty())
    {
        auto operation = runtime.pending.front();
        CompleteLoad(operation);
    }
}

//使指定源路径的旧请求失效
void ResourceManager::InvalidateLoads(const std::string& key, bool prefix)
{
    auto& runtime = GetResourceRuntime();
    std::string source = GetSourceKey(ToResourceKey(key));
    runtime.invalidations.emplace_back(++runtime.revision, source, prefix);
    for (const auto& operation : runtime.pending)
    {
        bool matched = source.empty() || GetSourceKey(operation->key) == source
            || (prefix && StartsWith(GetSourceKey(operation->key), source + "/"));
        auto state = operation->state;
        bool prepared = state->prepared.load(std::memory_order_acquire);
        if (prepared)
        {
            for (const auto& [key, record] : state->records)
            {
                std::string candidate = GetSourceKey(key);
                matched |= candidate == source || (prefix && StartsWith(candidate, source + "/"));
            }
        }
        if (matched)
        {
            state->invalidated = true;
            if (prepared)
            {
                state->Discard();
            }
        }
    }
}

//标记仍由加载句柄持有的资源结果
void ResourceManager::MarkLoadResults(std::unordered_set<int32>& marked)
{
    for (const auto& weak : GetResourceRuntime().operations)
    {
        if (auto operation = weak.lock())
        {
            MarkObjectGraph(Object::FindObjectById(operation->resultId), marked);
            auto state = operation->state;
            if (state && state->prepared.load(std::memory_order_acquire)
                && !state->committed && !state->invalidated)
            {
                for (const auto& [key, record] : state->records)
                {
                    MarkObjectGraph(record.object, marked);
                }
            }
        }
    }
}

//关闭资源加载线程
void ResourceManager::StopLoading()
{
    auto& runtime = GetResourceRuntime();
    InvalidateLoads("", true);
    {
        std::lock_guard lock(runtime.mutex);
        runtime.stopping = true;
        for (const auto& operation : runtime.queue)
        {
            operation->state->error = "Resource loading was stopped.";
            operation->state->prepared.store(true, std::memory_order_release);
        }
        runtime.queue.clear();
    }
    runtime.changed.notify_all();
    for (auto& worker : runtime.workers)
    {
        worker.join();
    }
    runtime.workers.clear();
    DrainLoads();
    runtime.loads.clear();
#ifndef _WIN32
    runtime.window = nullptr;
#endif
}
