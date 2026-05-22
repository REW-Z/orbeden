#pragma once

#include "Defines/types.h"

#include <string>
#include <type_traits>

class Object;
class ObjectPoolPage;
class Type;

typedef uint32 TypeId;
typedef Object* (*ObjectConstructorFunction)();
typedef void (*ObjectDestructorFunction)(Object* object);

//稳定字符串ID
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

private:
    TypeId id = 0;
    uint64 mask = 0;
    const char* name = nullptr;
    Type* baseType = nullptr;
    uint32 objectSize = 0;
    uint32 objectAlignment = 0;
    uint32 activeObjectCount = 0;
    ObjectConstructorFunction constructor = nullptr;
    ObjectDestructorFunction destructor = nullptr;
    ObjectPoolPage* headPage = nullptr;
    ObjectPoolPage* tailPage = nullptr;

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

    //创建对象实例
    Object* CreateObject();

    //销毁对象实例
    void DestroyObject(Object* object);

    //分配对象内存
    void* AllocateMemory();

    //释放对象内存
    void DeallocateMemory(std::byte* address);

    //释放对象池
    void ReleaseStorage();
};

//声明对象类型
#define OBJECT_TYPE_DECLARE(CLASS) \
public: \
    static Type type; \
    static Type* StaticType(); \
    virtual Type* GetType() const; \
    static Object* ConstructObject(); \
    static void DestructObject(Object* object);

//实现根对象类型
#define OBJECT_TYPE_IMPLEMENT_ROOT(CLASS) \
    Type CLASS::type(#CLASS, nullptr, sizeof(CLASS), alignof(CLASS), CLASS::ConstructObject, CLASS::DestructObject); \
    Type* CLASS::StaticType() { return &CLASS::type; } \
    Type* CLASS::GetType() const { return &CLASS::type; } \
    Object* CLASS::ConstructObject() { return new (CLASS::type.AllocateMemory()) CLASS(); } \
    void CLASS::DestructObject(Object* object) { CLASS* instance = static_cast<CLASS*>(object); instance->~CLASS(); CLASS::type.DeallocateMemory(reinterpret_cast<std::byte*>(instance)); }

//实现派生对象类型
#define OBJECT_TYPE_IMPLEMENT(CLASS, BASE) \
    Type CLASS::type(#CLASS, BASE::StaticType(), sizeof(CLASS), alignof(CLASS), CLASS::ConstructObject, CLASS::DestructObject); \
    Type* CLASS::StaticType() { return &CLASS::type; } \
    Type* CLASS::GetType() const { return &CLASS::type; } \
    Object* CLASS::ConstructObject() { return new (CLASS::type.AllocateMemory()) CLASS(); } \
    void CLASS::DestructObject(Object* object) { CLASS* instance = static_cast<CLASS*>(object); instance->~CLASS(); CLASS::type.DeallocateMemory(reinterpret_cast<std::byte*>(instance)); }

#define TYPEOF(CLASS) CLASS::StaticType()
#define TYPE_OF(CLASS) TYPEOF(CLASS)
#define IS(CLASS) ->Is(TYPEOF(CLASS))
#define AS(CLASS) ->Cast<CLASS>()

//旧工程风格的对象操作符宏
#ifndef ORBEDEN_DISABLE_LEGACY_OBJECT_OPERATORS
#define typeof(CLASS) TYPEOF(CLASS)
#define is(CLASS) IS(CLASS)
#define as(CLASS) AS(CLASS)
#endif

//基础对象
class Object
{
    OBJECT_TYPE_DECLARE(Object)

private:
    StringId instanceId;

public:
    Object() = default;
    virtual ~Object() = default;

    //获取实例ID
    const StringId& GetInstanceId() const;

    //设置实例ID
    void SetInstanceId(const StringId& id);

    //判断类型
    bool Is(Type* type) const;

    //转换类型
    template<typename T>
    T* Cast();

    //转换类型
    template<typename T>
    const T* Cast() const;

    //注册类型
    static void RegisterType(Type* type);

    //查找类型
    static Type* FindType(TypeId typeId);

    //查找类型
    static Type* FindType(const std::string& typeName);

    //获取类型数量
    static uint32 GetTypeCount();

    //查找对象
    static Object* FindObject(uint64 hash);

    //查找对象
    static Object* FindObject(const StringId& id);

    //创建对象
    static Object* CreateInstance(Type* type, const std::string& instancePath = "");

    //创建对象
    template<typename T>
    static T* CreateInstance(const std::string& instancePath = "");

    //销毁对象
    static bool DeleteInstance(Object* object);
};

//对象弱引用
template<typename T>
class Ref
{
private:
    StringId instanceId;
    mutable T* object = nullptr;

public:
    Ref() = default;
    explicit Ref(const StringId& id);
    explicit Ref(T* value);

    //设置对象
    void Set(T* value);

    //获取对象
    T* Get() const;

    //判断是否为空
    bool IsNull() const;

    T& operator*() const;
    T* operator->() const;
    operator T* () const;
};

template<typename T>
T* Object::Cast()
{
    static_assert(std::is_base_of_v<Object, T>);
    return GetType()->Is(T::StaticType()) ? static_cast<T*>(this) : nullptr;
}

template<typename T>
const T* Object::Cast() const
{
    static_assert(std::is_base_of_v<Object, T>);
    return GetType()->Is(T::StaticType()) ? static_cast<const T*>(this) : nullptr;
}

template<typename T>
T* Object::CreateInstance(const std::string& instancePath)
{
    static_assert(std::is_base_of_v<Object, T>);
    return static_cast<T*>(CreateInstance(T::StaticType(), instancePath));
}

template<typename T>
Ref<T>::Ref(const StringId& id)
    : instanceId(id)
{
    static_assert(std::is_base_of_v<Object, T>);
}

template<typename T>
Ref<T>::Ref(T* value)
{
    static_assert(std::is_base_of_v<Object, T>);
    Set(value);
}

template<typename T>
void Ref<T>::Set(T* value)
{
    object = value;
    instanceId = value ? value->GetInstanceId() : StringId();
}

template<typename T>
T* Ref<T>::Get() const
{
    if (!instanceId.IsValid()) return nullptr;

    Object* found = Object::FindObject(instanceId);
    if (found == object) return object;

    object = found ? found->Cast<T>() : nullptr;
    return object;
}

template<typename T>
bool Ref<T>::IsNull() const
{
    return Get() == nullptr;
}

template<typename T>
T& Ref<T>::operator*() const
{
    return *Get();
}

template<typename T>
T* Ref<T>::operator->() const
{
    return Get();
}

template<typename T>
Ref<T>::operator T* () const
{
    return Get();
}
