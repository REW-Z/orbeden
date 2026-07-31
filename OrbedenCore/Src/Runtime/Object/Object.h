#pragma once

#include "Defines/types.h"

#include <string>
#include <type_traits>

class Object;
class Component;
class Type;
class IChunk;
class World;
class AssetPipelineObjectFactory;
class ComponentStorage;
namespace Reflection
{
	struct FieldInfo;
	struct MethodInfo;
}
typedef uint32 TypeId;
typedef Object* (*ObjectConstructorFunction)(IChunk* chunk);
typedef void (*ObjectDestructorFunction)(Object* object);
typedef void (*ObjectVisitorFunction)(Object* object, void* userData);

//对象销毁监听器，在对象身份失效前接收通知。
class IObjectDestroyListener
{
public:
    virtual ~IObjectDestroyListener() = default;

    //接收即将销毁的对象
    virtual void OnObjectDestroyed(Object* object) = 0;
};

//字符串ID
class StringId
{
private:
    std::string path;
    uint64 pathHash = 0;

public:
    StringId() = default;
    explicit StringId(const std::string& text);
    explicit StringId(const char* text);

    //计算稳定Hash
    static uint64 CalculateHash(const std::string& text);

    //判断是否有效
    bool IsValid() const;

    //获取Hash
    uint64 GetHash() const;

    //获取路径文本
    const std::string& GetPath() const;

    bool operator==(const StringId& other) const;
    bool operator!=(const StringId& other) const;
};

//运行时类型信息
class Type
{
    friend class Object;
    friend class ComponentStorage;

private:
    TypeId id = 0;//运行时临时ID（依赖静态初始化顺序）
    uint64 mask = 0;
    const char* name = nullptr;
    Type* baseType = nullptr;
    uint32 objectSize = 0;
    uint32 objectAlignment = 0;
    ObjectConstructorFunction constructor = nullptr;
    ObjectDestructorFunction destructor = nullptr;
    IChunk* commonChunk = nullptr;
    List<IChunk*> worldChunks;

    //访问当前类型的所有存活对象
    void VisitLiveObjects(ObjectVisitorFunction visitor, void* userData) const;

    //获取或创建通用对象Chunk
    IChunk* GetOrCreateCommonChunk();

    //创建World拥有的对象Chunk
    IChunk* CreateWorldObjectChunk();

    //销毁World拥有的对象Chunk
    void DestroyWorldObjectChunk(IChunk*& chunk);

    //访问指定Chunk中的存活对象
    void VisitChunkObjects(IChunk* chunk, ObjectVisitorFunction visitor, void* userData) const;

public:
    Type(const char* typeName, Type* base, uint32 size, uint32 alignment, ObjectConstructorFunction ctor, ObjectDestructorFunction dtor);
    Type(const Type&) = delete;
    Type& operator=(const Type&) = delete;

    //获取类型ID
    TypeId GetId() const;

    //获取类型Mask
    uint64 GetMask() const;

    //获取类型名
    const char* GetName() const;

    //获取基类型
    Type* GetBaseType() const;

    //获取对象大小
    uint32 GetObjectSize() const;

    //获取对象对齐
    uint32 GetObjectAlignment() const;

    //判断继承关系
    bool Is(Type* type) const;

    //获取字段元数据
    const List<Reflection::FieldInfo>& GetFields() const;

    //查找字段元数据
    const Reflection::FieldInfo* GetField(const std::string& fieldName) const;

    //获取方法元数据
    const List<Reflection::MethodInfo>& GetMethods() const;

    //查找方法元数据
    const Reflection::MethodInfo* GetMethod(const std::string& methodName) const;

    //创建对象实例
    Object* CreateObject(IChunk* chunk = nullptr);

    //销毁对象实例
    void DestroyObject(Object* object);

    //分配对象内存
    void* AllocateMemory(IChunk* chunk);

    //释放对象内存
    void DeallocateMemory(IChunk* chunk, std::byte* address);

    //释放当前类型对象存储
    void ReleaseStorage();

    //遍历当前类型的所有存活对象
    template<typename TVisitor>
    void ForEachLiveObject(TVisitor&& visitor) const
    {
        struct VisitorContext
        {
            TVisitor& callback;
        };

        VisitorContext context{ visitor };
        VisitLiveObjects([](Object* object, void* userData)
        {
            VisitorContext* visitorContext = static_cast<VisitorContext*>(userData);
            visitorContext->callback(object);
        }, &context);
    }
};

//声明对象类型
#define OBJECT_TYPE_DECLARE_ROOT(CLASS) \
    friend class ReflectionGeneratedAccess; \
protected: \
    CLASS() = default; \
    virtual ~CLASS() = default; \
public: \
    static Type type; \
    static Type* StaticType(); \
    virtual Type* GetType() const; \
    static Object* ConstructObject(IChunk* chunk); \
    static void DestructObject(Object* object);

//声明可派生对象类型
#define OBJECT_TYPE_DECLARE_BASE(CLASS) \
    friend class ReflectionGeneratedAccess; \
protected: \
    CLASS() = default; \
    ~CLASS() override = default; \
public: \
    static Type type; \
    static Type* StaticType(); \
    virtual Type* GetType() const; \
    static Object* ConstructObject(IChunk* chunk); \
    static void DestructObject(Object* object);

//声明叶子对象类型
#define OBJECT_TYPE_DECLARE(CLASS) \
    friend class ReflectionGeneratedAccess; \
private: \
    CLASS() = default; \
    ~CLASS() override = default; \
public: \
    static Type type; \
    static Type* StaticType(); \
    virtual Type* GetType() const; \
    static Object* ConstructObject(IChunk* chunk); \
    static void DestructObject(Object* object);

//实现根对象类型
#define OBJECT_TYPE_IMPLEMENT_ROOT(CLASS) \
    Type CLASS::type(#CLASS, nullptr, sizeof(CLASS), alignof(CLASS), CLASS::ConstructObject, CLASS::DestructObject); \
    Type* CLASS::StaticType() { return &CLASS::type; } \
    Type* CLASS::GetType() const { return &CLASS::type; } \
    Object* CLASS::ConstructObject(IChunk* chunk) { return new (CLASS::type.AllocateMemory(chunk)) CLASS(); } \
    void CLASS::DestructObject(Object* object) { CLASS* instance = static_cast<CLASS*>(object); instance->~CLASS(); }

//实现派生对象类型
#define OBJECT_TYPE_IMPLEMENT(CLASS, BASE) \
    Type CLASS::type(#CLASS, BASE::StaticType(), sizeof(CLASS), alignof(CLASS), CLASS::ConstructObject, CLASS::DestructObject); \
    Type* CLASS::StaticType() { return &CLASS::type; } \
    Type* CLASS::GetType() const { return &CLASS::type; } \
    Object* CLASS::ConstructObject(IChunk* chunk) { return new (CLASS::type.AllocateMemory(chunk)) CLASS(); } \
    void CLASS::DestructObject(Object* object) { CLASS* instance = static_cast<CLASS*>(object); instance->~CLASS(); }

//对象类型宏  
#define is_type(CLASS) ->Is(CLASS::StaticType())
#define as_type(CLASS) ->Cast<CLASS>()


//基础对象
class Object
{
    OBJECT_TYPE_DECLARE_ROOT(Object)

private:
    enum class Ownership
    {
        None,
        WorldOwned,
        OrphanOwned,
        ResourceOwned,
    };

    StringId instanceId;
    int32 objectId = 0;
    void* managedWrapper = nullptr;
    World* ownerWorld = nullptr;
    Ownership ownership = Ownership::None;
    IChunk* allocationChunk = nullptr;

    friend class World;
    friend class ResourceManager;
    friend class AssetPipelineObjectFactory;
    friend class ComponentStorage;
    friend class Type;

    //设置所属世界
    void SetWorld(World* world);

    //设置所有权
    void SetOwnership(Ownership value);

    //获取所有权
    Ownership GetOwnership() const;

    //创建指定ID的未归属对象
    static Object* CreateRawInstance(Type* type, const std::string& instancePath, IChunk* chunk = nullptr);

    //生成UUID文本
    static std::string GenerateUuidText();

    //生成运行时对象ID
    static std::string CreateRuntimeInstancePath(const std::string& prefix, Type* type);

    //创建运行时对象并自动归属当前World或孤儿表
    static Object* CreateRuntimeInstance(Type* type);

    //创建资源对象
    static Object* CreateResourceInstance(Type* type, const std::string& instancePath);

    //销毁已经从所有者摘除的对象
    static bool DestroyDetachedInstance(Object* object);

public:
    //获取实例ID
    const StringId& GetInstanceId() const;

    //获取运行时对象ID
    int32 GetObjectId() const;

    //获取托管包装缓存
    void* GetManagedWrapper() const;

    //设置托管包装缓存
    void SetManagedWrapper(void* value);

    //设置实例ID
    void SetInstanceId(const StringId& id);

    //获取所属世界
    World* GetWorld() const;

    //判断类型
    bool Is(Type* type) const;

    //转换类型
    template<typename T>
    T* Cast()
    {
        static_assert(std::is_base_of_v<Object, T>);
        return GetType()->Is(T::StaticType()) ? static_cast<T*>(this) : nullptr;
    }

    //转换类型
    template<typename T>
    const T* Cast() const
    {
        static_assert(std::is_base_of_v<Object, T>);
        return GetType()->Is(T::StaticType()) ? static_cast<const T*>(this) : nullptr;
    }

    //注册类型
    static void RegisterType(Type* type);

    //查找类型
    static Type* FindType(TypeId typeId);

    //查找类型
    static Type* FindType(const std::string& typeName);

    //获取类型数量
    static uint32 GetTypeCount();

    //注册对象销毁监听器
    static void AddDestroyListener(IObjectDestroyListener* listener);

    //注销对象销毁监听器
    static void RemoveDestroyListener(IObjectDestroyListener* listener);

    //查找对象
    static Object* FindObject(uint64 hash);

    //查找对象
    static Object* FindObject(const StringId& id);

    //按运行时对象ID查找对象
    static Object* FindObjectById(int32 id);

    //判断运行时对象ID是否仍然存活
    static bool IsObjectAlive(int32 id);

    //从绑定层销毁对象
    static bool DestroyObjectFromBinding(Object* object);

    //创建运行时对象
    template<typename T>
    static T* CreateInstance()
    {
        static_assert(std::is_base_of_v<Object, T>);
        return static_cast<T*>(CreateRuntimeInstance(T::StaticType()));
    }

    //销毁对象
    static bool DeleteInstance(Object* object);

    //释放所有孤儿对象
    static void ReleaseOrphanInstances();

    //判断对象ID是否为运行时对象ID
    static bool IsRuntimeInstancePath(const std::string& instancePath);

    //判断运行时对象是否允许参与托管生命周期管理
    static bool IsManagedRuntimeResource(Object* object);

    //释放未使用的对象
    static uint32 UnloadUnusedObjects(const int32* managedRootIds, int32 count);
};

//对象软引用
template<typename T>
class Ref
{
private:
    StringId instanceId;
    mutable T* object = nullptr;

public:
    Ref() = default;
    explicit Ref(const StringId& id)
        : instanceId(id)
    {
        static_assert(std::is_base_of_v<Object, T>);
    }

    explicit Ref(T* value)
    {
        static_assert(std::is_base_of_v<Object, T>);
        Set(value);
    }

    //获取引用保存的稳定ID
    const StringId& GetInstanceId() const
    {
        return instanceId;
    }

    //设置引用保存的稳定ID，并清空运行时缓存
    void SetInstanceId(const StringId& id)
    {
        static_assert(std::is_base_of_v<Object, T>);
        instanceId = id;
        object = nullptr;
    }

    //设置对象
    void Set(T* value)
    {
        object = value;
        instanceId = value ? value->GetInstanceId() : StringId();
    }

    //获取对象
    T* Get() const
    {
        if (!instanceId.IsValid()) return nullptr;

        Object* found = Object::FindObject(instanceId);
        if (found == object) return object;

        object = found ? found->Cast<T>() : nullptr;
        return object;
    }

    //判断是否为空
    bool IsNull() const
    {
        return Get() == nullptr;
    }

    T& operator*() const
    {
        return *Get();
    }

    T* operator->() const
    {
        return Get();
    }

    operator T* () const
    {
        return Get();
    }
};
