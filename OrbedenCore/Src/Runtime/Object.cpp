#include "Runtime/Object.h"

#include "Memory/MemoryManager.h"
#include "Runtime/ObjectSystem.h"

#include <cassert>

namespace
{
    constexpr uint32 ObjectPoolPageBlockCount = 64;
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

//判断类型
bool Object::Is(Type* type) const
{
    return GetType()->Is(type);
}

//注册类型
void Object::RegisterType(Type* type)
{
    if (!type) return;

    ObjectSystem& system = GetObjectSystem();
    type->id = static_cast<TypeId>(system.types.size());
    type->mask = type->id < 64 ? (1ull << type->id) : 0;

    system.types.push_back(type);
    system.typeByName[type->GetName()] = type;
}

//查找类型
Type* Object::FindType(TypeId typeId)
{
    ObjectSystem& system = GetObjectSystem();
    if (typeId >= system.types.size()) return nullptr;

    return system.types[typeId];
}

//查找类型
Type* Object::FindType(const std::string& typeName)
{
    ObjectSystem& system = GetObjectSystem();
    auto it = system.typeByName.find(typeName);
    if (it == system.typeByName.end()) return nullptr;

    return it->second;
}

//获取类型数量
uint32 Object::GetTypeCount()
{
    return static_cast<uint32>(GetObjectSystem().types.size());
}

//查找对象
Object* Object::FindObject(uint64 hash)
{
    ObjectSystem& system = GetObjectSystem();
    auto it = system.objectById.find(hash);
    if (it == system.objectById.end()) return nullptr;

    return it->second;
}

//查找对象
Object* Object::FindObject(const StringId& id)
{
    return FindObject(id.GetHash());
}

//创建对象
Object* Object::CreateInstance(Type* type, const std::string& instancePath)
{
    if (!type) return nullptr;

    ObjectSystem& system = GetObjectSystem();
    Object* object = type->CreateObject();
    if (!object) return nullptr;

    std::string finalPath = instancePath;
    if (finalPath.empty())
    {
        finalPath = std::string(type->GetName()) + "_" + std::to_string(system.nextObjectIndex++);
    }

    object->SetInstanceId(StringId(finalPath));
    assert(object->GetInstanceId().IsValid());
    assert(system.objectById.find(object->GetInstanceId().GetHash()) == system.objectById.end());

    system.objectById[object->GetInstanceId().GetHash()] = object;
    return object;
}

//销毁对象
bool Object::DeleteInstance(Object* object)
{
    if (!object) return false;

    ObjectSystem& system = GetObjectSystem();
    system.objectById.erase(object->GetInstanceId().GetHash());

    Type* type = object->GetType();
    type->DestroyObject(object);
    return true;
}
