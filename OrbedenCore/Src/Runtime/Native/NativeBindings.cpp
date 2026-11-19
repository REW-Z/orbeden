#include "Runtime/Native/NativeBindings.h"
#include <unordered_map>

namespace
{
    struct BindingEntry { uint64 signature; std::vector<void*> functions; };
    std::unordered_map<Type*, BindingEntry>& Entries() { static std::unordered_map<Type*, BindingEntry> entries; return entries; }
    std::unordered_map<std::string, BindingEntry> handleEntries;
    uint32 generation = 1;
}

void NativeBindings::Register(Type* type, uint64 signature, std::span<void* const> functions)
{
    if (!type) throw std::invalid_argument("Binding type is null");
    auto found = Entries().find(type);
    if (found != Entries().end())
    {
        if (found->second.signature != signature) throw std::invalid_argument("Conflicting binding signature");
        return;
    }
    Entries().emplace(type, BindingEntry{ signature, { functions.begin(), functions.end() } });
}

//按普通类名称登记共享句柄函数表
void NativeBindings::Register(const char* typeName, uint64 signature, std::span<void* const> functions)
{
    auto [entry, inserted] = handleEntries.try_emplace(typeName, BindingEntry{ signature, { functions.begin(), functions.end() } });
    if (!inserted && entry->second.signature != signature)
    {
        throw std::invalid_argument("Conflicting handle binding signature");
    }
}

//判断原生类型是否提供托管绑定
bool NativeBindings::HasManagedBinding(Type* type)
{
    return Entries().contains(type);
}

void NativeBindings::Unregister(Type* type)
{
    if (!Entries().erase(type)) return;
    if (++generation == 0) generation = 1;
}

uint32 NativeBindings::GetGeneration() { return generation; }

NativeBindingStatus NativeBindings::Resolve(const char* typeName, int32 length, uint64 signature, uint32* typeId, const void*** functions)
{
    if (!typeName || length <= 0 || !typeId || !functions) return NativeBindingStatus::InvalidArgument;
    Type* type = Object::FindType(std::string(typeName, length));
    auto found = Entries().find(type);
    if (!type)
    {
        auto handle = handleEntries.find(std::string(typeName, length));
        if (handle == handleEntries.end() || handle->second.signature != signature)
        {
            return NativeBindingStatus::TypeMismatch;
        }
        *typeId = 0;
        *functions = const_cast<const void**>(handle->second.functions.data());
        return NativeBindingStatus::Ok;
    }
    if (found == Entries().end() || found->second.signature != signature) return NativeBindingStatus::TypeMismatch;
    *typeId = type->GetId(); *functions = const_cast<const void**>(found->second.functions.data());
    return NativeBindingStatus::Ok;
}

NativeBindingBuffer NativeBindings::AllocateBuffer(std::span<const uint8> data)
{
    if (data.size() > static_cast<size_t>(std::numeric_limits<int32>::max())) throw std::length_error("Binding buffer is too large");
    NativeBindingBuffer buffer{ new uint8[data.size()], static_cast<int32>(data.size()) };
    if (!data.empty()) std::memcpy(buffer.data, data.data(), data.size());
    return buffer;
}

void NativeBindings::ReleaseBuffer(NativeBindingBuffer buffer) { delete[] buffer.data; }

#include "Runtime/World.h"
#include "Runtime/Object/Ens.h"
#include "ResourceManager/ResourceManager.h"

namespace
{
    //借用托管缓冲并提交到当前世界
    uint8 ORBEDEN_NATIVE_CALL DrawMeshInstanced(int32 meshId, int32 materialId, const MeshInstanceData* instances,
        int32 count, const float32* parameters, int32 floatCount, uint32 subMeshIndex, uint32 drawLayer,
        uint8 castShadows, uint8 receiveShadows, EnsId camera)
    {
        World* world = World::CurrentWorld();
        if (!world || !instances || count <= 0 || floatCount < 0 || (floatCount > 0 && !parameters))
        {
            return 0;
        }
        try
        {
            InstanceDrawOptions options { drawLayer, castShadows != 0, receiveShadows != 0, camera };
            return world->DrawMeshInstanced(NativeBindings::Require<Mesh>(meshId), NativeBindings::Require<Material>(materialId),
                { instances, static_cast<usize>(count) }, { parameters, static_cast<usize>(floatCount) }, subMeshIndex, options);
        }
        catch (...)
        {
            return 0;
        }
    }

    const char* ORBEDEN_NATIVE_CALL GetBindingObjectTypeName(int32 id)
    {
        Object* object = Object::FindObjectById(id);
        return object ? object->GetType()->GetName() : nullptr;
    }
    void* ORBEDEN_NATIVE_CALL GetBindingObjectPointer(int32 id) { return Object::FindObjectById(id); }
    EnsId ORBEDEN_NATIVE_CALL GetBindingObjectEns(int32 id)
    {
        Object* object = Object::FindObjectById(id);
        Component* component = object ? object->Cast<Component>() : nullptr;
        return component ? component->GetEnsId() : EnsId();
    }
    int32 ORBEDEN_NATIVE_CALL CreateBindingObject(uint32 typeId)
    {
        try { return NativeBindings::Id(Object::CreateInstance(Object::FindType(typeId))); }
        catch (...) { return 0; }
    }
    int32 ORBEDEN_NATIVE_CALL AddBindingComponent(EnsId ens, uint32 typeId)
    {
        try
        {
            World* world = World::CurrentWorld();
            return world ? NativeBindings::Id(world->AddComponentInstance(ens, Object::FindType(typeId))) : 0;
        }
        catch (...) { return 0; }
    }
    int32 ORBEDEN_NATIVE_CALL GetBindingComponents(EnsId ens, uint32 typeId, int32* output, int32 capacity)
    {
        try
        {
            World* world = World::CurrentWorld();
            if (!world || capacity < 0 || (capacity != 0 && !output)) return 0;
            List<Component*> components;
            Type* requestedType = Object::FindType(typeId);
            Ens* owner = world->GetEns(ens);
            if (!requestedType || !owner) return 0;
            for (Component* component : owner->GetComponents())
                if (component && component->GetType()->Is(requestedType)
                    && NativeBindings::HasManagedBinding(component->GetType())) components.push_back(component);
            for (int32 index = 0; index < capacity && index < static_cast<int32>(components.size()); ++index)
                output[index] = components[index]->GetObjectId();
            return static_cast<int32>(components.size());
        }
        catch (...) { return 0; }
    }
    int32 ORBEDEN_NATIVE_CALL LoadBindingResource(uint32 typeId, const uint8* key, int32 length)
    {
        if (!key || length <= 0) return 0;
        try
        {
            Type* type = Object::FindType(typeId);
            if (!type) return 0;
            std::string resourceKey = ResourceManager::ToResourceKey(std::string(reinterpret_cast<const char*>(key), length));
            Object* object = nullptr;
            if (Object::IsRuntimeInstancePath(resourceKey))
            {
                object = ResourceManager::FindLoaded(resourceKey);
                if (!object) object = Object::FindObject(StringId(resourceKey));
            }
            else object = ResourceManager::Load(type, resourceKey);
            return object && object->GetType()->Is(type) ? NativeBindings::Id(object) : 0;
        }
        catch (...) { return 0; }
    }
}

void RegisterBindings_Orbeden();

NativeBindingsApi NativeBindingsApi::Create()
{
    RegisterBindings_Orbeden();
    //登记直接实例绘制的独立函数表
    void* drawFunctions[] = { reinterpret_cast<void*>(&DrawMeshInstanced) };
    NativeBindings::Register("World.DrawMeshInstanced", 1, drawFunctions);
    NativeBindingsApi api;
    api.GetGeneration = reinterpret_cast<void*>(&NativeBindings::GetGeneration);
    api.ResolveType = reinterpret_cast<void*>(&NativeBindings::Resolve);
    api.GetObjectTypeName = reinterpret_cast<void*>(&GetBindingObjectTypeName);
    api.GetObjectPointer = reinterpret_cast<void*>(&GetBindingObjectPointer);
    api.GetObjectEns = reinterpret_cast<void*>(&GetBindingObjectEns);
    api.CreateObject = reinterpret_cast<void*>(&CreateBindingObject);
    api.AddComponent = reinterpret_cast<void*>(&AddBindingComponent);
    api.GetComponents = reinterpret_cast<void*>(&GetBindingComponents);
    api.LoadResource = reinterpret_cast<void*>(&LoadBindingResource);
    api.ReleaseBuffer = reinterpret_cast<void*>(&NativeBindings::ReleaseBuffer);
    return api;
}
