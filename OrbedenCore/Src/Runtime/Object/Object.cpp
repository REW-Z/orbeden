#include "Runtime/Object/Object.h"

#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Physics/ColliderComponent.h"
#include "Runtime/EnsId.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/TransformComponent.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/Reflection.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/World.h"
#include "Scripting/ScriptBehaviour.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace
{
	constexpr uint32 ObjectChunkSlotCount = 64;//每个对象Chunk的槽位数量

    struct ObjectRuntime
    {
    public:
        List<Type*> types;
        std::unordered_map<std::string, Type*> typeByName;
        std::unordered_map<std::string, Object*> objectByPath;
        std::unordered_map<int32, Object*> objectById;
        List<Object*> orphanObjects;
        List<IObjectDestroyListener*> destroyListeners;
        int32 nextObjectId = 1;
        void* currentModuleOwner = nullptr;
        List<Type*> pendingModuleTypes;
        bool moduleTypeRegistrationFailed = false;
    };

    //获取对象运行时注册表
    ObjectRuntime& GetObjectRuntime()
    {
        static ObjectRuntime runtime;
        return runtime;
    }

    //判断字符串前缀
    bool StartsWith(const std::string& text, const char* prefix)
    {
        if (!prefix) return false;

        usize prefixLength = std::strlen(prefix);
        return text.size() >= prefixLength && text.compare(0, prefixLength, prefix) == 0;
    }

    //移除孤儿对象记录
    void RemoveOrphanObject(ObjectRuntime& runtime, Object* object)
    {
        auto it = std::find(runtime.orphanObjects.begin(), runtime.orphanObjects.end(), object);
        if (it != runtime.orphanObjects.end())
        {
            runtime.orphanObjects.erase(it);
        }
    }

    //生成随机64位值
    uint64 GenerateRandom64()
    {
        static std::random_device seed;
        static std::mt19937_64 random(seed());
        static std::uniform_int_distribution<uint64> distribution;
        return distribution(random);
    }

    //分配运行时对象ID
    int32 AllocateObjectId(ObjectRuntime& runtime)
    {
        while (runtime.nextObjectId <= 0 || runtime.objectById.find(runtime.nextObjectId) != runtime.objectById.end())
        {
            runtime.nextObjectId++;
        }

        return runtime.nextObjectId++;
    }
}

//对象Chunk接口
class IChunk
{
public:
    virtual void* Allocate(uint32 objectSize, uint32 objectAlignment) = 0;
    virtual bool Deallocate(std::byte* address) = 0;
    virtual uint32 GetAliveObjectCount() const = 0;
    virtual void VisitLiveObjects(ObjectVisitorFunction visitor, void* userData) const = 0;
    virtual void DestroyChunk() = 0;

protected:
    ~IChunk() = default;
};

//一段连续对象槽位
class ObjectChunkBlock
{
public:
    ChunkSlotAllocator allocator;
    ObjectChunkBlock* next = nullptr;

    ObjectChunkBlock(uint32 size, uint32 count, uint32 alignment)
        : allocator(size, count, alignment)
    {
    }

    //判断Chunk是否还有空槽位
    bool HasFreeSlot() const
    {
        return allocator.GetAliveCount() < allocator.GetSlotCount();
    }
};

//基于连续槽位块的对象Chunk
class ObjectChunkBase : public IChunk
{
private:
    ObjectChunkBlock* headChunk = nullptr;
    ObjectChunkBlock* tailChunk = nullptr;
    uint32 aliveObjectCount = 0;
    uint32 objectSize = 0;
    uint32 objectAlignment = 0;

public:
    ObjectChunkBase(uint32 size, uint32 alignment)
        : objectSize(size), objectAlignment(alignment)
    {
    }

    ObjectChunkBase(const ObjectChunkBase&) = delete;
    ObjectChunkBase& operator=(const ObjectChunkBase&) = delete;

    ~ObjectChunkBase()
    {
        Release();
    }

    //分配一个同类型对象槽位
    void* Allocate(uint32 size, uint32 alignment) override
    {
        assert(size == objectSize);
        assert(alignment == objectAlignment);

        ObjectChunkBlock* chunk = headChunk;
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
        ObjectChunkBlock* newChunk = NEW(ObjectChunkBlock)ObjectChunkBlock(objectSize, ObjectChunkSlotCount, objectAlignment);
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
    bool Deallocate(std::byte* address) override
    {
        if (!address) return false;

        ObjectChunkBlock* chunk = headChunk;
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
        ObjectChunkBlock* chunk = headChunk;
        while (chunk)
        {
            ObjectChunkBlock* next = chunk->next;
            DELETE(chunk);
            chunk = next;
        }

        headChunk = nullptr;
        tailChunk = nullptr;
        aliveObjectCount = 0;
    }

    //获取存活对象数量
    uint32 GetAliveObjectCount() const override
    {
        return aliveObjectCount;
    }

    //按Chunk和槽位顺序遍历存活对象
    void VisitLiveObjects(ObjectVisitorFunction visitor, void* userData) const override
    {
        if (!visitor) return;

        struct VisitorContext
        {
            ObjectVisitorFunction callback = nullptr;
            void* data = nullptr;
        };

        VisitorContext context{ visitor, userData };
        ObjectChunkBlock* chunk = headChunk;
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

//通用对象Chunk
class CommonObjectChunk : public ObjectChunkBase
{
public:
    CommonObjectChunk(uint32 size, uint32 alignment)
        : ObjectChunkBase(size, alignment)
    {
    }

    void DestroyChunk() override
    {
        CommonObjectChunk* self = this;
        DELETE(self);
    }
};

//World拥有的对象Chunk
class WorldOwnObjectChunk : public ObjectChunkBase
{
public:
    WorldOwnObjectChunk(uint32 size, uint32 alignment)
        : ObjectChunkBase(size, alignment)
    {
    }

    void DestroyChunk() override
    {
        WorldOwnObjectChunk* self = this;
        DELETE(self);
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

//获取或创建通用对象Chunk
IChunk* Type::GetOrCreateCommonChunk()
{
    if (!commonChunk)
    {
        commonChunk = NEW(CommonObjectChunk)CommonObjectChunk(objectSize, objectAlignment);
    }

    return commonChunk;
}

//创建World拥有的对象Chunk
IChunk* Type::CreateWorldObjectChunk()
{
    IChunk* chunk = NEW(WorldOwnObjectChunk)WorldOwnObjectChunk(objectSize, objectAlignment);
    worldChunks.push_back(chunk);
    return chunk;
}

//销毁World拥有的对象Chunk
void Type::DestroyWorldObjectChunk(IChunk*& chunk)
{
    if (!chunk) return;

    assert(chunk->GetAliveObjectCount() == 0);
    worldChunks.erase(std::remove(worldChunks.begin(), worldChunks.end(), chunk), worldChunks.end());
    chunk->DestroyChunk();
    chunk = nullptr;
}

//访问指定Chunk中的存活对象
void Type::VisitChunkObjects(IChunk* chunk, ObjectVisitorFunction visitor, void* userData) const
{
    if (!chunk) return;

    chunk->VisitLiveObjects(visitor, userData);
}

//创建对象实例
Object* Type::CreateObject(IChunk* chunk)
{
    if (!constructor) return nullptr;

    IChunk* allocationChunk = chunk ? chunk : GetOrCreateCommonChunk();
    Object* object = constructor(allocationChunk);
    if (object)
    {
        object->allocationChunk = allocationChunk;
    }

    return object;
}

//判断类型能否直接创建实例
bool Type::CanCreateObject() const
{
    return constructor != nullptr && destructor != nullptr;
}

void* Type::GetModuleOwner() const
{
    return moduleOwner;
}

bool Type::HasLiveObjects() const
{
    bool found = false;
    VisitLiveObjects([](Object*, void* userData)
        {
            *static_cast<bool*>(userData) = true;
        }, &found);
    return found;
}

//销毁对象实例
void Type::DestroyObject(Object* object)
{
    if (!object || !destructor) return;
    if (object->GetWorld()) return;

    IChunk* allocationChunk = object->allocationChunk;
    std::byte* address = reinterpret_cast<std::byte*>(object);
    destructor(object);
    DeallocateMemory(allocationChunk, address);
}

//分配对象内存
void* Type::AllocateMemory(IChunk* chunk)
{
    IChunk* allocationChunk = chunk ? chunk : GetOrCreateCommonChunk();
    void* memory = allocationChunk->Allocate(objectSize, objectAlignment);
    assert(memory);
    return memory;
}

//释放对象内存
void Type::DeallocateMemory(IChunk* chunk, std::byte* address)
{
    if (!address) return;
    assert(chunk);
    if (!chunk) return;

    bool deallocated = chunk->Deallocate(address);
    assert(deallocated);

    if (deallocated && chunk == commonChunk && commonChunk->GetAliveObjectCount() == 0)
    {
        ReleaseStorage();
    }
}

//释放当前类型对象存储
void Type::ReleaseStorage()
{
    if (commonChunk)
    {
        assert(commonChunk->GetAliveObjectCount() == 0);
        commonChunk->DestroyChunk();
        commonChunk = nullptr;
    }
}

//访问当前类型的所有存活对象
void Type::VisitLiveObjects(ObjectVisitorFunction visitor, void* userData) const
{
    if (commonChunk)
    {
        commonChunk->VisitLiveObjects(visitor, userData);
    }

    for (IChunk* chunk : worldChunks)
    {
        if (!chunk) continue;

        chunk->VisitLiveObjects(visitor, userData);
    }
}

Type OrbedenObject::type("Object", nullptr, sizeof(OrbedenObject), alignof(OrbedenObject),
    OrbedenObject::ConstructObject, OrbedenObject::DestructObject);

Type* OrbedenObject::StaticType()
{
    return &OrbedenObject::type;
}

Type* OrbedenObject::GetType() const
{
    return &OrbedenObject::type;
}

Object* OrbedenObject::ConstructObject(IChunk* chunk)
{
    return new (OrbedenObject::type.AllocateMemory(chunk)) OrbedenObject();
}

void OrbedenObject::DestructObject(Object* object)
{
    OrbedenObject* instance = static_cast<OrbedenObject*>(object);
    instance->~OrbedenObject();
}

//获取实例ID
const StringId& Object::GetInstanceId() const
{
    return instanceId;
}

//获取运行时对象ID
int32 Object::GetObjectId() const
{
    return objectId;
}

//获取托管包装缓存
void* Object::GetManagedWrapper() const
{
    return managedWrapper;
}

//设置托管包装缓存
void Object::SetManagedWrapper(void* value)
{
    managedWrapper = value;
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
    if (runtime.currentModuleOwner)
    {
        type->moduleOwner = runtime.currentModuleOwner;
        if (runtime.typeByName.find(type->GetName()) != runtime.typeByName.end())
        {
            runtime.moduleTypeRegistrationFailed = true;
            return;
        }
        for (Type* pendingType : runtime.pendingModuleTypes)
        {
            if (std::string(pendingType->GetName()) != type->GetName()) continue;
            runtime.moduleTypeRegistrationFailed = true;
            return;
        }
        runtime.pendingModuleTypes.push_back(type);
        return;
    }

    auto emptySlot = std::find(runtime.types.begin(), runtime.types.end(), nullptr);
    type->id = emptySlot == runtime.types.end()
        ? static_cast<TypeId>(runtime.types.size())
        : static_cast<TypeId>(std::distance(runtime.types.begin(), emptySlot));
    type->mask = type->id < 64 ? (1ull << type->id) : 0;
    type->moduleOwner = runtime.currentModuleOwner;

    if (emptySlot == runtime.types.end()) runtime.types.push_back(type);
    else *emptySlot = type;
    runtime.typeByName[type->GetName()] = type;
}

void Object::BeginModuleTypeRegistration(void* moduleOwner)
{
    ObjectRuntime& runtime = GetObjectRuntime();
    runtime.currentModuleOwner = moduleOwner;
    runtime.pendingModuleTypes.clear();
    runtime.moduleTypeRegistrationFailed = moduleOwner == nullptr;
}

bool Object::EndModuleTypeRegistration(bool commit)
{
    ObjectRuntime& runtime = GetObjectRuntime();
    bool succeeded = commit && !runtime.moduleTypeRegistrationFailed;
    void* moduleOwner = runtime.currentModuleOwner;
    List<Type*> pendingTypes;
    if (succeeded) pendingTypes.swap(runtime.pendingModuleTypes);
    else runtime.pendingModuleTypes.clear();

    runtime.currentModuleOwner = nullptr;
    runtime.moduleTypeRegistrationFailed = false;
    for (Type* type : pendingTypes)
    {
        RegisterType(type);
        type->moduleOwner = moduleOwner;
    }
    return succeeded;
}

bool Object::UnregisterModuleTypes(void* moduleOwner)
{
    if (!moduleOwner) return false;

    ObjectRuntime& runtime = GetObjectRuntime();
    List<Type*> ownedTypes;
    for (Type* type : runtime.types)
    {
        if (type && type->GetModuleOwner() == moduleOwner) ownedTypes.push_back(type);
    }
    for (Type* type : ownedTypes)
    {
        if (type->HasLiveObjects()) return false;
    }

    for (auto it = ownedTypes.rbegin(); it != ownedTypes.rend(); ++it)
    {
        Type* type = *it;
        Reflection::UnregisterType(type);
        UnregisterScriptCallbacks(type);
        type->ReleaseStorage();

        auto namedType = runtime.typeByName.find(type->GetName());
        if (namedType != runtime.typeByName.end() && namedType->second == type) runtime.typeByName.erase(namedType);
        if (type->GetId() < runtime.types.size() && runtime.types[type->GetId()] == type) runtime.types[type->GetId()] = nullptr;
        type->moduleOwner = nullptr;
    }
    return true;
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

//注册对象销毁监听器
void Object::AddDestroyListener(IObjectDestroyListener* listener)
{
    if (!listener) return;

    List<IObjectDestroyListener*>& listeners = GetObjectRuntime().destroyListeners;
    if (std::find(listeners.begin(), listeners.end(), listener) == listeners.end())
    {
        listeners.push_back(listener);
    }
}

//注销对象销毁监听器
void Object::RemoveDestroyListener(IObjectDestroyListener* listener)
{
    List<IObjectDestroyListener*>& listeners = GetObjectRuntime().destroyListeners;
    auto it = std::find(listeners.begin(), listeners.end(), listener);
    if (it != listeners.end()) listeners.erase(it);
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

//按运行时对象ID查找对象
Object* Object::FindObjectById(int32 id)
{
    if (id <= 0) return nullptr;

    ObjectRuntime& runtime = GetObjectRuntime();
    auto it = runtime.objectById.find(id);
    return it == runtime.objectById.end() ? nullptr : it->second;
}

//判断运行时对象ID是否仍然存活
bool Object::IsObjectAlive(int32 id)
{
    return FindObjectById(id) != nullptr;
}

//创建指定ID的未归属对象
Object* Object::CreateRawInstance(Type* type, const std::string& instancePath, IChunk* chunk)
{
    if (!type || instancePath.empty()) return nullptr;

    ObjectRuntime& runtime = GetObjectRuntime();
    if (runtime.objectByPath.find(instancePath) != runtime.objectByPath.end()) return nullptr;

    Object* object = type->CreateObject(chunk);
    if (!object) return nullptr;

    object->SetInstanceId(StringId(instancePath));
    if (!object->GetInstanceId().IsValid())
    {
        type->DestroyObject(object);
        return nullptr;
    }

    object->objectId = AllocateObjectId(runtime);
    runtime.objectByPath[object->GetInstanceId().GetPath()] = object;
    runtime.objectById[object->objectId] = object;
    return object;
}

//生成UUID文本
std::string Object::GenerateUuidText()
{
    std::array<uint8, 16> bytes{};
    for (usize offset = 0; offset < bytes.size(); offset += 8)
    {
        uint64 value = GenerateRandom64();
        for (usize index = 0; index < 8; ++index)
        {
            bytes[offset + index] = static_cast<uint8>((value >> ((7 - index) * 8)) & 0xff);
        }
    }

    bytes[6] = static_cast<uint8>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<uint8>((bytes[8] & 0x3f) | 0x80);

    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (usize index = 0; index < bytes.size(); ++index)
    {
        if (index == 4 || index == 6 || index == 8 || index == 10)
        {
            stream << '-';
        }

        stream << std::setw(2) << static_cast<uint32>(bytes[index]);
    }

    return stream.str();
}

//生成运行时对象ID
std::string Object::CreateRuntimeInstancePath(const std::string& prefix, Type* type)
{
    if (prefix.empty() || !type) return std::string();

    std::string instancePath;
    do
    {
        instancePath = prefix + "/" + type->GetName() + "/" + GenerateUuidText();
    } while (Object::FindObject(StringId(instancePath)));

    return instancePath;
}

//判断对象ID是否为运行时对象ID
bool Object::IsRuntimeInstancePath(const std::string& instancePath)
{
    return StartsWith(instancePath, "world://runtime/")
        || StartsWith(instancePath, "orphan://runtime/");
}

//判断运行时对象是否允许参与托管生命周期管理
bool Object::IsManagedRuntimeResource(Object* object)
{
    if (!object) return false;
    if (!IsRuntimeInstancePath(object->GetInstanceId().GetPath())) return false;

    return object->Is(Mesh::StaticType())
        || object->Is(Material::StaticType())
        || object->Is(Shader::StaticType());
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
        Object* object = CreateRawInstance(type, world->AllocateRuntimeObjectPath(type));
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

    std::string instancePath = CreateRuntimeInstancePath("orphan://runtime", type);
    Object* object = CreateRawInstance(type, instancePath);
    if (!object) return nullptr;

    object->SetOwnership(Ownership::OrphanOwned);
    GetObjectRuntime().orphanObjects.push_back(object);
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

    //通知对象销毁监听者
    List<IObjectDestroyListener*> listeners = runtime.destroyListeners;
    for (IObjectDestroyListener* listener : listeners)
    {
        if (listener) listener->OnObjectDestroyed(object);
    }

    runtime.objectByPath.erase(object->GetInstanceId().GetPath());
    runtime.objectById.erase(object->GetObjectId());

    Type* type = object->GetType();
    object->SetInstanceId(StringId());
    object->objectId = 0;
    object->SetManagedWrapper(nullptr);
    object->SetOwnership(Ownership::None);
    type->DestroyObject(object);
    return true;
}

//从绑定层销毁对象
bool Object::DestroyObjectFromBinding(Object* object)
{
    if (!object || !IsObjectAlive(object->GetObjectId())) return false;

    if (Component* component = object->Cast<Component>())
    {
        World* world = component->GetWorld();
        if (!world)
        {
            Log::Error("Component destroy failed: component is not attached to a world.");
            return false;
        }

        if (component->GetType() == TransformComponent::StaticType())
        {
            Log::Error("TransformComponent cannot be destroyed directly.");
            return false;
        }

        return world->RemoveComponent(component);
    }

    if (object->GetOwnership() == Ownership::ResourceOwned)
    {
        return ResourceManager::DestroyObject(object);
    }

    return DeleteInstance(object);
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

//释放未使用的对象
uint32 Object::UnloadUnusedObjects(const int32* managedRootIds, int32 count)
{
    ObjectRuntime& runtime = GetObjectRuntime();
    std::unordered_set<int32> marked;

    //托管包装仍然存活的对象作为根
    for (int32 index = 0; managedRootIds && index < count; ++index)
    {
        ResourceManager::MarkObjectGraph(FindObjectById(managedRootIds[index]), marked);
    }

    //当前场景显式引用的资源作为根
    if (World* world = World::CurrentWorld())
    {
        ResourceManager::MarkObjectGraph(world->renderSettings.skybox.Get(), marked);
        world->ForEachComponent<StaticMeshRenderer>([&](StaticMeshRenderer* renderer)
        {
            if (renderer) ResourceManager::MarkObjectGraph(renderer->mesh.Get(), marked);
        });
        world->ForEachEns([&](Ens& ens)
        {
            for (Component* component : ens.GetComponents())
            {
                ColliderComponent* collider = component ? component->Cast<ColliderComponent>() : nullptr;
                if (!collider) continue;

                if (ConvexMeshColliderComponent* convex = collider->Cast<ConvexMeshColliderComponent>())
                {
                    ResourceManager::MarkObjectGraph(convex->mesh.Get(), marked);
                }
                else if (TriangleMeshColliderComponent* triangle = collider->Cast<TriangleMeshColliderComponent>())
                {
                    ResourceManager::MarkObjectGraph(triangle->mesh.Get(), marked);
                }
            }
        });
    }

    uint32 removedCount = ResourceManager::ReleaseUnmarkedObjects(marked);

    //运行时创建的非组件对象按可达性释放
    List<Object*> unusedObjects;
    for (const auto& pair : runtime.objectById)
    {
        Object* object = pair.second;
        if (!object || object->Is(Component::StaticType())) continue;
        if (object->GetOwnership() == Ownership::ResourceOwned) continue;
        if (marked.find(object->GetObjectId()) != marked.end()) continue;

        unusedObjects.push_back(object);
    }

    for (Object* object : unusedObjects)
    {
        if (DeleteInstance(object))
        {
            removedCount++;
        }
    }

    return removedCount;
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
