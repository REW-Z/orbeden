#include "Runtime/Object/Object.h"

#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Runtime/EnsId.h"
#include "Runtime/Reflection.h"
#include "Runtime/World.h"

#include <algorithm>
#include <cassert>
#include <unordered_map>

namespace
{
	constexpr uint32 TypeObjectChunkSlotCount = 64;//每个TypeObjectChunk的槽位数量

    struct ObjectRuntime
    {
    public:
        List<Type*> types;
        std::unordered_map<std::string, Type*> typeByName;
        std::unordered_map<std::string, Object*> objectByPath;
        List<Object*> orphanObjects;
        uint64 nextOrphanObjectIndex = 1;
    };

    //获取对象运行时注册表
    ObjectRuntime& GetObjectRuntime()
    {
        static ObjectRuntime runtime;
        return runtime;
    }
}

//某个Type的一段连续对象存储
class TypeObjectChunk
{
public:
    ChunkSlotAllocator allocator;
    TypeObjectChunk* next = nullptr;

    TypeObjectChunk(uint32 size, uint32 count, uint32 alignment)
        : allocator(size, count, alignment)
    {
    }

    //判断Chunk是否还有空槽位
    bool HasFreeSlot() const
    {
        return allocator.GetAliveCount() < allocator.GetSlotCount();
    }
};

//某个Type的所有连续对象Chunk
class TypeObjectStorage
{
private:
    TypeObjectChunk* headChunk = nullptr;
    TypeObjectChunk* tailChunk = nullptr;
    uint32 aliveObjectCount = 0;

public:
    TypeObjectStorage() = default;
    TypeObjectStorage(const TypeObjectStorage&) = delete;
    TypeObjectStorage& operator=(const TypeObjectStorage&) = delete;

    ~TypeObjectStorage()
    {
        Release();
    }

    //分配一个同类型对象槽位
    void* Allocate(uint32 objectSize, uint32 objectAlignment)
    {
        TypeObjectChunk* chunk = headChunk;
        while (chunk)
        {
            if (chunk->HasFreeSlot())
            {
                void* memory = chunk->allocator.AllocateSlot();
                if (memory)
                {
                    aliveObjectCount++;
                    return memory;
                }
            }

            chunk = chunk->next;
        }

		//没有空槽位，创建新的Chunk
        TypeObjectChunk* newChunk = NEW(TypeObjectChunk)TypeObjectChunk(objectSize, TypeObjectChunkSlotCount, objectAlignment);
        if (tailChunk)
        {
            tailChunk->next = newChunk;
        }
        else
        {
            headChunk = newChunk;
        }

        tailChunk = newChunk;
        void* memory = newChunk->allocator.AllocateSlot();
        assert(memory);
        aliveObjectCount++;
        return memory;
    }

    //释放一个同类型对象槽位
    bool Deallocate(std::byte* address)
    {
        if (!address) return false;

        TypeObjectChunk* chunk = headChunk;
        while (chunk)
        {
            if (chunk->allocator.Contains(address))
            {
                chunk->allocator.DeallocateSlot(address);
                assert(aliveObjectCount > 0);
                aliveObjectCount--;
                return true;
            }

            chunk = chunk->next;
        }

        return false;
    }

    //释放所有对象Chunk
    void Release()
    {
        TypeObjectChunk* chunk = headChunk;
        while (chunk)
        {
            TypeObjectChunk* next = chunk->next;
            DELETE(chunk);
            chunk = next;
        }

        headChunk = nullptr;
        tailChunk = nullptr;
        aliveObjectCount = 0;
    }

    //获取存活对象数量
    uint32 GetAliveObjectCount() const
    {
        return aliveObjectCount;
    }

    //按Chunk和槽位顺序遍历存活对象
    void VisitLiveObjects(ObjectVisitorFunction visitor, void* userData) const
    {
        if (!visitor) return;

        struct VisitorContext
        {
            ObjectVisitorFunction callback = nullptr;
            void* data = nullptr;
        };

        VisitorContext context{ visitor, userData };
        TypeObjectChunk* chunk = headChunk;
        while (chunk)
        {
            chunk->allocator.VisitAliveSlots([](void* address, void* contextData)
            {
                VisitorContext* visitorContext = static_cast<VisitorContext*>(contextData);
                visitorContext->callback(reinterpret_cast<Object*>(address), visitorContext->data);
            }, &context);

            chunk = chunk->next;
        }
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
    if (object->GetWorld()) return;

    destructor(object);
}

//分配对象内存
void* Type::AllocateMemory()
{
    if (!objectStorage)
    {
        objectStorage = NEW(TypeObjectStorage)TypeObjectStorage();
    }

    void* memory = objectStorage->Allocate(objectSize, objectAlignment);
    assert(memory);
    return memory;
}

//释放对象内存
void Type::DeallocateMemory(std::byte* address)
{
    if (!address) return;
    assert(objectStorage);
    if (!objectStorage) return;

    bool deallocated = objectStorage->Deallocate(address);
    assert(deallocated);

    if (deallocated && objectStorage->GetAliveObjectCount() == 0)
    {
        ReleaseStorage();
    }
}

//释放当前类型对象存储
void Type::ReleaseStorage()
{
    if (objectStorage)
    {
        assert(objectStorage->GetAliveObjectCount() == 0);
    }

    DELETE(objectStorage);
}

//访问当前类型的所有存活对象
void Type::VisitLiveObjects(ObjectVisitorFunction visitor, void* userData) const
{
    if (!objectStorage) return;

    objectStorage->VisitLiveObjects(visitor, userData);
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

//创建指定ID的未归属对象
Object* Object::CreateRawInstance(Type* type, const std::string& instancePath)
{
    if (!type || instancePath.empty()) return nullptr;

    ObjectRuntime& runtime = GetObjectRuntime();
    if (runtime.objectByPath.find(instancePath) != runtime.objectByPath.end()) return nullptr;

    Object* object = type->CreateObject();
    if (!object) return nullptr;

    object->SetInstanceId(StringId(instancePath));
    if (!object->GetInstanceId().IsValid())
    {
        type->DestroyObject(object);
        return nullptr;
    }

    runtime.objectByPath[object->GetInstanceId().GetPath()] = object;
    return object;
}

//创建运行时对象并自动归属当前World或孤儿表
Object* Object::CreateRuntimeInstance(Type* type)
{
    if (!type) return nullptr;
    if (type->Is(Component::StaticType()))
    {
        Log::Error("Component must be created by Ens::AddComponent.");
        return nullptr;
    }

    World* world = World::CurrentWorld();
    if (world)
    {
        Object* object = CreateRawInstance(type, world->AllocateRuntimeObjectPath());
        if (!object) return nullptr;

        object->SetWorld(world);
        object->SetOwnership(Ownership::WorldOwned);
        if (!world->AddOwnedObject(object))
        {
            object->SetWorld(nullptr);
            object->SetOwnership(Ownership::None);
            DestroyDetachedInstance(object);
            return nullptr;
        }

        return object;
    }

    ObjectRuntime& runtime = GetObjectRuntime();
    std::string instancePath;
    do
    {
        instancePath = "orphan://runtime/" + std::to_string(runtime.nextOrphanObjectIndex++);
    } while (runtime.objectByPath.find(instancePath) != runtime.objectByPath.end());

    Object* object = CreateRawInstance(type, instancePath);
    if (!object) return nullptr;

    object->SetOwnership(Ownership::OrphanOwned);
    runtime.orphanObjects.push_back(object);
    return object;
}

//创建待注册资源对象
Object* Object::CreateResourceInstance(Type* type, const std::string& instancePath)
{
    if (!type) return nullptr;
    if (type->Is(Component::StaticType()))
    {
        Log::Error("Component cannot be created as a resource object.");
        return nullptr;
    }

    Object* object = CreateRawInstance(type, instancePath);
    if (!object) return nullptr;

    return object;
}

//销毁已经从所有者摘除的对象
bool Object::DestroyDetachedInstance(Object* object)
{
    if (!object) return false;
    if (object->GetWorld()) return false;

    ObjectRuntime& runtime = GetObjectRuntime();
    runtime.objectByPath.erase(object->GetInstanceId().GetPath());

    Type* type = object->GetType();
    object->SetInstanceId(StringId());
    object->SetOwnership(Ownership::None);
    type->DestroyObject(object);
    return true;
}

//销毁对象
bool Object::DeleteInstance(Object* object)
{
    if (!object) return false;

    switch (object->GetOwnership())
    {
    case Ownership::WorldOwned:
        if (object->Is(Component::StaticType()))
        {
            Log::Error("Component must be destroyed by Ens::RemoveComponent or World::DestroyEns.");
            return false;
        }
        if (!object->GetWorld())
        {
            return false;
        }
        if (!object->GetWorld()->RemoveOwnedObject(object))
        {
            return false;
        }

        object->SetWorld(nullptr);
        object->SetOwnership(Ownership::None);
        return DestroyDetachedInstance(object);

    case Ownership::OrphanOwned:
    {
        ObjectRuntime& runtime = GetObjectRuntime();
        auto it = std::find(runtime.orphanObjects.begin(), runtime.orphanObjects.end(), object);
        if (it != runtime.orphanObjects.end())
        {
            runtime.orphanObjects.erase(it);
        }

        object->SetOwnership(Ownership::None);
        return DestroyDetachedInstance(object);
    }

    case Ownership::ResourceOwned:
        Log::Error("Resource object must be released by ResourceManager.");
        return false;

    case Ownership::None:
        return DestroyDetachedInstance(object);
    }

    return false;
}

//释放所有孤儿对象
void Object::ReleaseOrphanInstances()
{
    ObjectRuntime& runtime = GetObjectRuntime();
    while (!runtime.orphanObjects.empty())
    {
        Object* object = runtime.orphanObjects.back();
        runtime.orphanObjects.pop_back();
        if (!object) continue;

        object->SetOwnership(Ownership::None);
        DestroyDetachedInstance(object);
    }
}

//设置所属世界
void Object::SetWorld(World* world)
{
    ownerWorld = world;
}

//设置所有权
void Object::SetOwnership(Ownership value)
{
    ownership = value;
}

//获取所有权
Object::Ownership Object::GetOwnership() const
{
    return ownership;
}
