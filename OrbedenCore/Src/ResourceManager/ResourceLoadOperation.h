#pragma once

#include "ResourceManager/ResourceManager.h"
#include "Runtime/Coroutines/CoOperation.h"

#include <atomic>
#include <unordered_map>

//资源加载等待项及共享构建状态
ORBEDEN_BIND_HANDLE
class ResourceLoadOperation final : public CoOperation
{
    friend class ResourceManager;
    friend class Orbeden::Object;

private:
    struct State
    {
        std::string key;
        std::string contentRoot;
        std::string error;
        std::unordered_map<std::string, Object*> objects;
        std::unordered_map<Type*, IChunk*> chunks;
        std::unordered_map<std::string, Type*> types;
        std::unordered_map<std::string, ResourceManager::ResourceRecord> records;
        std::unordered_set<std::string> loadingKeys;
        List<Object*> ownedObjects;
        List<Object*> publishObjects;
        usize commitIndex = 0;
        bool finalized = false;
        std::atomic_bool prepared = false;
        bool invalidated = false;
        bool committed = false;
        uint64 revision = 0;

        //保存内容根和资源类型快照
        explicit State(const std::string& key);

        //释放尚未移交的对象和分配区
        ~State();

        //在后台加载资源及其依赖
        void Prepare();

        //取得内容根下的文件路径
        std::string GetFilePath(const std::string& path) const;

        //复用主线程资源并校验依赖
        bool Finalize();

        //按时间预算登记并发布资源
        bool Commit(float64 budgetMilliseconds);

        //释放失败或失效的加载结果
        void Discard();
    };

    static thread_local State* current;
    std::string key;
    Type* type;
    std::shared_ptr<State> state;
    int32 resultId = 0;
    bool finished = false;
    std::string error;

    //保存资源路径和请求类型
    explicit ResourceLoadOperation(const std::string& key, Type* type = nullptr);

    //加载请求通过共享句柄持有
    ResourceLoadOperation(const ResourceLoadOperation&) = delete;
    ResourceLoadOperation& operator=(const ResourceLoadOperation&) = delete;

public:
    //按原生资源类型编号启动加载
    static std::shared_ptr<ResourceLoadOperation> Load(uint32 typeId, const std::string& key);

    //取得当前线程正在构建的资源状态
    ORBEDEN_BIND_IGNORE
    static State* Current();

    //查询请求是否结束
    ORBEDEN_BIND_PROPERTY(IsFinished)
    bool IsFinished() const { return finished; }

    //查询请求是否成功
    ORBEDEN_BIND_PROPERTY(Succeeded)
    bool Succeeded() const { return finished && error.empty(); }

    //读取加载错误
    ORBEDEN_BIND_PROPERTY(Error)
    const std::string& GetError() const { return error; }

    //取得仍然存活的加载结果
    ORBEDEN_BIND_PROPERTY(Result)
    Object* GetResult() const;

    //等待资源完成发布
    bool ShouldWait(float64 elapsedSeconds) override { return !finished; }
};

//异步加载指定类型的资源
template<typename T>
std::shared_ptr<ResourceLoadOperation> ResourceManager::LoadAsync(const std::string& key)
{
    static_assert(std::is_base_of_v<Object, T>);
    return LoadAsync(T::StaticType(), key);
}
