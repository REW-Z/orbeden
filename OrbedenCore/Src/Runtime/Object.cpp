#include "Runtime/Object.h"

#include "Memory/MemoryManager.h"
#include "Runtime/Reflection.h"

#include <cassert>
#include <unordered_map>

namespace
{
    constexpr uint32 ObjectPoolPageBlockCount = 64;

    struct ObjectRuntime
    {
    public:
        List<Type*> types;
        std::unordered_map<std::string, Type*> typeByName;
        std::unordered_map<std::string, Object*> objectByPath;
        uint64 nextObjectIndex = 1;
    };

    //获取对象运行时注册表
    ObjectRuntime& GetObjectRuntime()
    {
        static ObjectRuntime runtime;
        return runtime;
    }
}

//对象池页
class ObjectPoolPage
{
public:
    PoolAllocator allocator;
    ObjectPoolPage* next = nullptr;

    ObjectPoolPage(uint32 size, uint32 count, uint32 alignment)
        : allocator(size, count, alignment)
    {
    }
};

//创建字符串ID
StringId::StringId(const std::string& text)
    : path(text), pathHash(text.empty() ? 0 : CalculateHash(text))
{
}

//创建字符串ID
StringId::StringId(const char* text)
    : StringId(text ? std::string(text) : std::string())
{
}

//计算稳定Hash
uint64 StringId::CalculateHash(const std::string& text)
{
    uint64 hash = 14695981039346656037ull;
    for (char ch : text)
    {
        hash ^= static_cast<uint8>(ch);
        hash *= 1099511628211ull;
    }

    return hash;
}

//判断是否有效
bool StringId::IsValid() const
{
    return pathHash != 0;
}

//获取Hash
uint64 StringId::GetHash() const
{
    return pathHash;
}

//获取路径文本
const std::string& StringId::GetPath() const
{
    return path;
}

bool StringId::operator==(const StringId& other) const
{
    return pathHash == other.pathHash && path == other.path;
}

bool StringId::operator!=(const StringId& other) const
{
    return !(*this == other);
}

//创建运行时类型信息
Type::Type(const char* typeName, Type* base, uint32 size, uint32 alignment, ObjectConstructorFunction ctor, ObjectDestructorFunction dtor)
    : name(typeName), baseType(base), objectSize(size), objectAlignment(alignment), constructor(ctor), destructor(dtor)
{
    Object::RegisterType(this);
}

//获取类型ID
TypeId Type::GetId() const
{
    return id;
}

//获取类型Mask
uint64 Type::GetMask() const
{
    return mask;
}

//获取类型名
const char* Type::GetName() const
{
    return name;
}

//获取基类型
Type* Type::GetBaseType() const
{
    return baseType;
}

//获取对象大小
uint32 Type::GetObjectSize() const
{
    return objectSize;
}

//获取对象对齐
uint32 Type::GetObjectAlignment() const
{
    return objectAlignment;
}

//判断继承关系
bool Type::Is(Type* type) const
{
    if (!type) return false;

    const Type* current = this;
    uint32 depth = 0;
    while (current)
    {
        if (current == type) return true;

        current = current->baseType;
        assert(++depth < 256);
    }

    return false;
}

//获取字段元数据
const List<Reflection::FieldInfo>& Type::GetFields() const
{
    Reflection::RegisterGeneratedReflection();

    static const List<Reflection::FieldInfo> emptyFields;
    const Reflection::TypeInfo* info = Reflection::FindTypeInfo(const_cast<Type*>(this));
    return info ? info->fields : emptyFields;
}

//查找字段元数据
const Reflection::FieldInfo* Type::GetField(const std::string& fieldName) const
{
    Reflection::RegisterGeneratedReflection();
    return Reflection::FindField(const_cast<Type*>(this), fieldName);
}

//获取方法元数据
const List<Reflection::MethodInfo>& Type::GetMethods() const
{
    Reflection::RegisterGeneratedReflection();

    static const List<Reflection::MethodInfo> emptyMethods;
    const Reflection::TypeInfo* info = Reflection::FindTypeInfo(const_cast<Type*>(this));
    return info ? info->methods : emptyMethods;
}

//查找方法元数据
const Reflection::MethodInfo* Type::GetMethod(const std::string& methodName) const
{
    Reflection::RegisterGeneratedReflection();
    return Reflection::FindMethod(const_cast<Type*>(this), methodName);
}

//创建对象实例
Object* Type::CreateObject()
{
    return constructor ? constructor() : nullptr;
}

//销毁对象实例
void Type::DestroyObject(Object* object)
{
    if (!object || !destructor) return;

    destructor(object);
}

//分配对象内存
void* Type::AllocateMemory()
{
    void* memory = tailPage ? tailPage->allocator.Allocate(objectSize, objectAlignment) : nullptr;
    if (!memory)
    {
        ObjectPoolPage* page = NEW(ObjectPoolPage)ObjectPoolPage(objectSize, ObjectPoolPageBlockCount, objectAlignment);

        if (tailPage)
        {
            tailPage->next = page;
        }
        else
        {
            headPage = page;
        }

        tailPage = page;
        memory = page->allocator.Allocate(objectSize, objectAlignment);
    }

    assert(memory);
    activeObjectCount++;
    return memory;
}

//释放对象内存
void Type::DeallocateMemory(std::byte* address)
{
    if (!address) return;

    ObjectPoolPage* page = headPage;
    while (page)
    {
        if (page->allocator.Contains(address))
        {
            page->allocator.Deallocate(address);
            assert(activeObjectCount > 0);
            activeObjectCount--;

            if (activeObjectCount == 0)
            {
                ReleaseStorage();
            }
            return;
        }

        page = page->next;
    }

    assert(false);
}

//释放对象池
void Type::ReleaseStorage()
{
    ObjectPoolPage* page = headPage;
    while (page)
    {
        ObjectPoolPage* next = page->next;
        DELETE(page);
        page = next;
    }

    headPage = nullptr;
    tailPage = nullptr;
}

OBJECT_TYPE_IMPLEMENT_ROOT(Object)

//获取实例ID
const StringId& Object::GetInstanceId() const
{
    return instanceId;
}

//设置实例ID
void Object::SetInstanceId(const StringId& id)
{
    instanceId = id;
}

World* Object::GetWorld() const
{
    return ownerWorld;
}

//判断类型
bool Object::Is(Type* type) const
{
    return GetType()->Is(type);
}

//注册类型
void Object::RegisterType(Type* type)
{
    if (!type) return;

    ObjectRuntime& runtime = GetObjectRuntime();
    type->id = static_cast<TypeId>(runtime.types.size());
    type->mask = type->id < 64 ? (1ull << type->id) : 0;

    runtime.types.push_back(type);
    runtime.typeByName[type->GetName()] = type;
}

//查找类型
Type* Object::FindType(TypeId typeId)
{
    ObjectRuntime& runtime = GetObjectRuntime();
    if (typeId >= runtime.types.size()) return nullptr;

    return runtime.types[typeId];
}

//查找类型
Type* Object::FindType(const std::string& typeName)
{
    ObjectRuntime& runtime = GetObjectRuntime();
    auto it = runtime.typeByName.find(typeName);
    if (it == runtime.typeByName.end()) return nullptr;

    return it->second;
}

//获取类型数量
uint32 Object::GetTypeCount()
{
    return static_cast<uint32>(GetObjectRuntime().types.size());
}

//查找对象
Object* Object::FindObject(uint64 hash)
{
    if (hash == 0) return nullptr;

    ObjectRuntime& runtime = GetObjectRuntime();
    for (const auto& pair : runtime.objectByPath)
    {
        Object* object = pair.second;
        if (object && object->GetInstanceId().GetHash() == hash)
        {
            return object;
        }
    }

    return nullptr;
}

//查找对象
Object* Object::FindObject(const StringId& id)
{
    if (!id.IsValid()) return nullptr;

    ObjectRuntime& runtime = GetObjectRuntime();
    auto it = runtime.objectByPath.find(id.GetPath());
    if (it == runtime.objectByPath.end()) return nullptr;

    return it->second;
}

//创建对象
Object* Object::CreateInstance(Type* type, const std::string& instancePath)
{
    if (!type) return nullptr;

    ObjectRuntime& runtime = GetObjectRuntime();

    std::string finalPath = instancePath;
    if (finalPath.empty())
    {
        do
        {
            finalPath = std::string(type->GetName()) + "_" + std::to_string(runtime.nextObjectIndex++);
        } while (runtime.objectByPath.find(finalPath) != runtime.objectByPath.end());
    }
    else if (runtime.objectByPath.find(finalPath) != runtime.objectByPath.end())
    {
        return nullptr;
    }

    Object* object = type->CreateObject();
    if (!object) return nullptr;

    object->SetInstanceId(StringId(finalPath));
    if (!object->GetInstanceId().IsValid())
    {
        type->DestroyObject(object);
        return nullptr;
    }

    runtime.objectByPath[object->GetInstanceId().GetPath()] = object;
    return object;
}

//销毁对象
bool Object::DeleteInstance(Object* object)
{
    if (!object) return false;

    ObjectRuntime& runtime = GetObjectRuntime();
    runtime.objectByPath.erase(object->GetInstanceId().GetPath());

    Type* type = object->GetType();
    type->DestroyObject(object);
    return true;
}

//设置所属世界
void Object::SetWorld(World* world)
{
    ownerWorld = world;
}
