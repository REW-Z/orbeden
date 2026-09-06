#include "Scripting/ScriptInterop.h"

#include <cstring>
#include <exception>
#include <limits>
#include <thread>
#include <vector>

#include "Runtime/Ens.h"
#include "Runtime/World.h"
#include "Scripting/ScriptBehaviour.h"

namespace
{
    using namespace ScriptInterop;

    using FindManagedFunction = InteropStatus(ORBEDEN_NATIVE_CALL*)(EnsId, const uint8*, int32, int32, ComponentHandle*);
    using ValidateFunction = InteropStatus(ORBEDEN_NATIVE_CALL*)(ComponentHandle);
    using ResolveFieldFunction = InteropStatus(ORBEDEN_NATIVE_CALL*)(ComponentHandle, const uint8*, int32, MemberHandle*);
    using ResolveMethodFunction = InteropStatus(ORBEDEN_NATIVE_CALL*)(ComponentHandle, const uint8*, int32, const uint32*, int32, MemberHandle*);
    using GetFieldFunction = InteropStatus(ORBEDEN_NATIVE_CALL*)(ComponentHandle, MemberHandle, InteropValueAbi*);
    using SetFieldFunction = InteropStatus(ORBEDEN_NATIVE_CALL*)(ComponentHandle, MemberHandle, const InteropValueAbi*);
    using InvokeFunction = InteropStatus(ORBEDEN_NATIVE_CALL*)(ComponentHandle, MemberHandle, const InteropValueAbi*, int32, InteropValueAbi*);
    using HostFunction = InteropStatus(ORBEDEN_NATIVE_CALL*)(void*);
    using HostFieldFunction = InteropStatus(ORBEDEN_NATIVE_CALL*)(void*, const uint8*, int32);

    World* currentWorld = nullptr;
    std::thread::id mainThread;
    uint32 nativeGeneration = 1;
    ManagedScriptInteropApi managedApi;
    thread_local uint32 reentrancyDepth = 0;
    thread_local std::string utf8Result;

    class CallGuard
    {
    private:
        bool entered = false;

    public:
        InteropStatus Enter()
        {
            if (std::this_thread::get_id() != mainThread) return InteropStatus::WrongThread;
            if (reentrancyDepth >= 32) return InteropStatus::ReentrancyLimit;
            ++reentrancyDepth;
            entered = true;
            return InteropStatus::Ok;
        }

        ~CallGuard()
        {
            if (entered) --reentrancyDepth;
        }
    };

    std::string_view ReadUtf8(const uint8* text, int32 length)
    {
        return text && length >= 0
            ? std::string_view(reinterpret_cast<const char*>(text), static_cast<std::size_t>(length))
            : std::string_view();
    }

    template<typename T>
    void WritePayload(InteropValueAbi& destination, const T& value)
    {
        static_assert(sizeof(T) <= sizeof(destination.payload));
        std::memcpy(destination.payload, &value, sizeof(T));
    }

    template<typename T>
    T ReadPayload(const InteropValueAbi& source)
    {
        static_assert(sizeof(T) <= sizeof(source.payload));
        T value{};
        std::memcpy(&value, source.payload, sizeof(T));
        return value;
    }

    struct Utf8Payload
    {
        const uint8* pointer = nullptr;
        int32 length = 0;
        uint32 reserved = 0;
    };

    static_assert(sizeof(Utf8Payload) == 16);

    bool EncodeValue(const Reflection::Value& value, InteropValueAbi& destination, std::string* stringStorage)
    {
        destination = {};
        destination.kind = static_cast<uint32>(value.GetKind());
        switch (value.GetKind())
        {
        case Reflection::ValueKind::Empty: return true;
        case Reflection::ValueKind::Bool:
        {
            bool current = false;
            if (!value.TryGet(current)) return false;
            const uint8 encoded = current ? uint8(1) : uint8(0);
            WritePayload(destination, encoded);
            return true;
        }
        case Reflection::ValueKind::Int32:
        {
            int32 current = 0;
            if (!value.TryGet(current)) return false;
            WritePayload(destination, current);
            return true;
        }
        case Reflection::ValueKind::UInt32:
        {
            uint32 current = 0;
            if (!value.TryGet(current)) return false;
            WritePayload(destination, current);
            return true;
        }
        case Reflection::ValueKind::UInt64:
        {
            uint64 current = 0;
            if (!value.TryGet(current)) return false;
            WritePayload(destination, current);
            return true;
        }
        case Reflection::ValueKind::Float32:
        {
            float32 current = 0.0f;
            if (!value.TryGet(current)) return false;
            WritePayload(destination, current);
            return true;
        }
        case Reflection::ValueKind::String:
        {
            if (!stringStorage || !value.TryGet(*stringStorage)) return false;
            Utf8Payload payload{ reinterpret_cast<const uint8*>(stringStorage->data()), static_cast<int32>(stringStorage->size()), 0 };
            WritePayload(destination, payload);
            return true;
        }
        case Reflection::ValueKind::StringId:
        {
            if (!stringStorage) return false;
            StringId current;
            if (!value.TryGet(current)) return false;
            *stringStorage = current.GetPath();
            Utf8Payload payload{ reinterpret_cast<const uint8*>(stringStorage->data()), static_cast<int32>(stringStorage->size()), 0 };
            WritePayload(destination, payload);
            return true;
        }
        case Reflection::ValueKind::Vector3:
        {
            vector3 current;
            if (!value.TryGet(current)) return false;
            WritePayload(destination, current);
            return true;
        }
        case Reflection::ValueKind::Color:
        {
            color current;
            if (!value.TryGet(current)) return false;
            WritePayload(destination, current);
            return true;
        }
        case Reflection::ValueKind::Quaternion:
        {
            quaternion current;
            if (!value.TryGet(current)) return false;
            WritePayload(destination, current);
            return true;
        }
        case Reflection::ValueKind::EnsId:
        {
            EnsId current;
            if (!value.TryGet(current)) return false;
            WritePayload(destination, current);
            return true;
        }
        case Reflection::ValueKind::Object:
        {
            Object* current = nullptr;
            if (!value.TryGet(current)) return false;
            const int32 objectId = current ? current->GetObjectId() : 0;
            WritePayload(destination, objectId);
            return true;
        }
        default: return false;
        }
    }

    bool DecodeValue(const InteropValueAbi& source, Reflection::Value& value)
    {
        const Reflection::ValueKind kind = static_cast<Reflection::ValueKind>(source.kind);
        switch (kind)
        {
        case Reflection::ValueKind::Empty: value = Reflection::Value(); return true;
        case Reflection::ValueKind::Bool: value = Reflection::Value(ReadPayload<uint8>(source) != 0); return true;
        case Reflection::ValueKind::Int32: value = Reflection::Value(ReadPayload<int32>(source)); return true;
        case Reflection::ValueKind::UInt32: value = Reflection::Value(ReadPayload<uint32>(source)); return true;
        case Reflection::ValueKind::UInt64: value = Reflection::Value(ReadPayload<uint64>(source)); return true;
        case Reflection::ValueKind::Float32: value = Reflection::Value(ReadPayload<float32>(source)); return true;
        case Reflection::ValueKind::String:
        case Reflection::ValueKind::StringId:
        {
            Utf8Payload payload = ReadPayload<Utf8Payload>(source);
            if (payload.length < 0 || (payload.length > 0 && !payload.pointer)) return false;
            std::string text;
            if (payload.length > 0)
                text.assign(reinterpret_cast<const char*>(payload.pointer), static_cast<std::size_t>(payload.length));
            value = kind == Reflection::ValueKind::String ? Reflection::Value(text) : Reflection::Value(StringId(text));
            return true;
        }
        case Reflection::ValueKind::Vector3: value = Reflection::Value(ReadPayload<vector3>(source)); return true;
        case Reflection::ValueKind::Color: value = Reflection::Value(ReadPayload<color>(source)); return true;
        case Reflection::ValueKind::Quaternion: value = Reflection::Value(ReadPayload<quaternion>(source)); return true;
        case Reflection::ValueKind::EnsId: value = Reflection::Value(ReadPayload<EnsId>(source)); return true;
        case Reflection::ValueKind::Object:
        {
            int32 objectId = ReadPayload<int32>(source);
            Object* object = objectId == 0 ? nullptr : Object::FindObjectById(objectId);
            if (objectId != 0 && !object) return false;
            value = Reflection::Value(object);
            return true;
        }
        default: return false;
        }
    }

    Component* ResolveNativeComponent(ComponentHandle handle)
    {
        if (handle.domain != ComponentDomain::Native || handle.generation != nativeGeneration || handle.slot > static_cast<uint64>(std::numeric_limits<int32>::max())) return nullptr;
        Object* object = Object::FindObjectById(static_cast<int32>(handle.slot));
        Component* component = object ? object->Cast<Component>() : nullptr;
        return component && component->GetWorld() == currentWorld ? component : nullptr;
    }

    InteropStatus FindNative(EnsId ens, Type* type, int32 occurrence, ComponentHandle* output)
    {
        if (!output || occurrence < 0 || !currentWorld || !type || !type->Is(Component::StaticType())) return InteropStatus::InvalidArgument;
        Ens* owner = currentWorld->GetEns(ens);
        if (!owner) return InteropStatus::NotFound;

        List<Component*> components;
        owner->GetComponentInstances(type, components);
        if (occurrence >= static_cast<int32>(components.size())) return InteropStatus::NotFound;
        Component* component = components[occurrence];
        if (!component) return InteropStatus::NotFound;

        *output = { ComponentDomain::Native, nativeGeneration, static_cast<uint64>(component->GetObjectId()) };
        return InteropStatus::Ok;
    }

    InteropStatus ValidateNative(ComponentHandle component)
    {
        return ResolveNativeComponent(component) ? InteropStatus::Ok : InteropStatus::StaleHandle;
    }

    InteropStatus ResolveNativeField(ComponentHandle component, std::string_view name, MemberHandle* output)
    {
        Component* instance = ResolveNativeComponent(component);
        if (!instance) return InteropStatus::StaleHandle;
        if (!output || name.empty()) return InteropStatus::InvalidArgument;
        const Reflection::FieldInfo* field = Reflection::FindField(instance->GetType(), std::string(name));
        if (!field) return InteropStatus::NotFound;
        if (field->kind == Reflection::FieldKind::Unsupported) return InteropStatus::UnsupportedType;
        *output = { ComponentDomain::Native, MemberKind::Field, reinterpret_cast<uint64>(field), Reflection::GetRegistryGeneration(), 0 };
        return InteropStatus::Ok;
    }

    InteropStatus ResolveNativeMethod(ComponentHandle component, std::string_view name, std::span<const uint32> rawKinds, MemberHandle* output)
    {
        Component* instance = ResolveNativeComponent(component);
        if (!instance) return InteropStatus::StaleHandle;
        if (!output || name.empty()) return InteropStatus::InvalidArgument;

        List<Reflection::ValueKind> kinds;
        kinds.reserve(rawKinds.size());
        for (uint32 rawKind : rawKinds)
        {
            if (rawKind > static_cast<uint32>(Reflection::ValueKind::Object)) return InteropStatus::UnsupportedType;
            kinds.push_back(static_cast<Reflection::ValueKind>(rawKind));
        }

        bool ambiguous = false;
        const Reflection::MethodInfo* method = Reflection::FindMethod(instance->GetType(), std::string(name), kinds, &ambiguous);
        if (ambiguous) return InteropStatus::AmbiguousMethod;
        if (!method) return InteropStatus::NotFound;
        *output = { ComponentDomain::Native, MemberKind::Method, reinterpret_cast<uint64>(method), Reflection::GetRegistryGeneration(), 0 };
        return InteropStatus::Ok;
    }

    bool NativeMemberBelongsTo(Type* type, MemberHandle member)
    {
        const uintptr_t address = static_cast<uintptr_t>(member.slot);
        for (Type* current = type; current; current = current->GetBaseType())
        {
            const Reflection::TypeInfo* info = Reflection::FindTypeInfo(current);
            if (!info) continue;
            if (member.kind == MemberKind::Field && !info->fields.empty())
            {
                const uintptr_t begin = reinterpret_cast<uintptr_t>(info->fields.data());
                const uintptr_t end = begin + info->fields.size() * sizeof(Reflection::FieldInfo);
                if (address >= begin && address < end && (address - begin) % sizeof(Reflection::FieldInfo) == 0) return true;
            }
            if (member.kind == MemberKind::Method && !info->methods.empty())
            {
                const uintptr_t begin = reinterpret_cast<uintptr_t>(info->methods.data());
                const uintptr_t end = begin + info->methods.size() * sizeof(Reflection::MethodInfo);
                if (address >= begin && address < end && (address - begin) % sizeof(Reflection::MethodInfo) == 0) return true;
            }
        }
        return false;
    }

    InteropStatus ValidateNativeMember(Type* type, MemberHandle member, MemberKind expected)
    {
        if (member.domain != ComponentDomain::Native || member.kind != expected || !member.slot) return InteropStatus::InvalidArgument;
        if (member.generation != Reflection::GetRegistryGeneration()) return InteropStatus::StaleHandle;
        return NativeMemberBelongsTo(type, member) ? InteropStatus::Ok : InteropStatus::InvalidArgument;
    }

    InteropStatus GetNativeField(ComponentHandle component, MemberHandle member, InteropValueAbi* output)
    {
        Component* instance = ResolveNativeComponent(component);
        if (!instance) return InteropStatus::StaleHandle;
        InteropStatus memberStatus = ValidateNativeMember(instance->GetType(), member, MemberKind::Field);
        if (memberStatus != InteropStatus::Ok) return memberStatus;
        if (!output) return InteropStatus::InvalidArgument;

        const Reflection::FieldInfo* field = reinterpret_cast<const Reflection::FieldInfo*>(member.slot);
        Reflection::Value value = field->GetValue(instance);
        if (value.IsEmpty() && field->kind != Reflection::FieldKind::Unsupported) return InteropStatus::InvocationFailed;
        utf8Result.clear();
        return EncodeValue(value, *output, &utf8Result) ? InteropStatus::Ok : InteropStatus::UnsupportedType;
    }

    InteropStatus SetNativeField(ComponentHandle component, MemberHandle member, const InteropValueAbi* input)
    {
        Component* instance = ResolveNativeComponent(component);
        if (!instance) return InteropStatus::StaleHandle;
        InteropStatus memberStatus = ValidateNativeMember(instance->GetType(), member, MemberKind::Field);
        if (memberStatus != InteropStatus::Ok) return memberStatus;
        if (!input) return InteropStatus::InvalidArgument;

        const Reflection::FieldInfo* field = reinterpret_cast<const Reflection::FieldInfo*>(member.slot);
        Reflection::Value value;
        if (!DecodeValue(*input, value)) return InteropStatus::UnsupportedType;
        if (static_cast<uint32>(value.GetKind()) != input->kind) return InteropStatus::TypeMismatch;

        Reflection::Value oldValue = field->GetValue(instance);
        if (!oldValue.IsEmpty() && oldValue.GetKind() != value.GetKind()) return InteropStatus::TypeMismatch;
        return field->SetValue(instance, value) ? InteropStatus::Ok : InteropStatus::TypeMismatch;
    }

    InteropStatus InvokeNative(ComponentHandle component, MemberHandle member, const InteropValueAbi* input, int32 count, InteropValueAbi* output)
    {
        Component* instance = ResolveNativeComponent(component);
        if (!instance) return InteropStatus::StaleHandle;
        InteropStatus memberStatus = ValidateNativeMember(instance->GetType(), member, MemberKind::Method);
        if (memberStatus != InteropStatus::Ok) return memberStatus;
        if (count < 0 || (count > 0 && !input) || !output) return InteropStatus::InvalidArgument;

        const Reflection::MethodInfo* method = reinterpret_cast<const Reflection::MethodInfo*>(member.slot);
        if (count != static_cast<int32>(method->parameters.size())) return InteropStatus::TypeMismatch;
        List<Reflection::Value> args(static_cast<std::size_t>(count));
        for (int32 index = 0; index < count; ++index)
        {
            if (!DecodeValue(input[index], args[index])) return InteropStatus::UnsupportedType;
            if (index >= static_cast<int32>(method->parameters.size()) || args[index].GetKind() != method->parameters[index].kind) return InteropStatus::TypeMismatch;
        }

        bool success = false;
        Reflection::Value result = method->Invoke(instance, std::span<const Reflection::Value>(args.data(), args.size()), &success);
        if (!success) return InteropStatus::InvocationFailed;
        utf8Result.clear();
        return EncodeValue(result, *output, &utf8Result) ? InteropStatus::Ok : InteropStatus::UnsupportedType;
    }

    template<typename TCallback, typename... TArgs>
    InteropStatus CallManaged(void* callback, TArgs... args)
    {
        if (!callback) return InteropStatus::StaleHandle;
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        try
        {
            return reinterpret_cast<TCallback>(callback)(args...);
        }
        catch (...)
        {
            return InteropStatus::InvocationFailed;
        }
    }

    InteropStatus ORBEDEN_NATIVE_CALL NativeFindByTypeId(EnsId ens, uint32 typeId, int32 occurrence, ComponentHandle* output)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        try { return FindNative(ens, Object::FindType(typeId), occurrence, output); }
        catch (...) { return InteropStatus::InvocationFailed; }
    }

    InteropStatus ORBEDEN_NATIVE_CALL NativeFindByName(EnsId ens, const uint8* name, int32 length, int32 occurrence, ComponentHandle* output)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        if (!name || length <= 0) return InteropStatus::InvalidArgument;
        try { return FindNative(ens, Object::FindType(std::string(ReadUtf8(name, length))), occurrence, output); }
        catch (...) { return InteropStatus::InvocationFailed; }
    }

    InteropStatus ORBEDEN_NATIVE_CALL NativeIsValid(ComponentHandle component)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        return status == InteropStatus::Ok ? ValidateNative(component) : status;
    }

    InteropStatus ORBEDEN_NATIVE_CALL NativeResolveField(ComponentHandle component, const uint8* name, int32 length, MemberHandle* output)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        if (!name || length <= 0) return InteropStatus::InvalidArgument;
        try { return ResolveNativeField(component, ReadUtf8(name, length), output); }
        catch (...) { return InteropStatus::InvocationFailed; }
    }

    InteropStatus ORBEDEN_NATIVE_CALL NativeResolveMethod(ComponentHandle component, const uint8* name, int32 length, const uint32* kinds, int32 count, MemberHandle* output)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        if (!name || length <= 0 || count < 0 || (count > 0 && !kinds)) return InteropStatus::InvalidArgument;
        try { return ResolveNativeMethod(component, ReadUtf8(name, length), std::span<const uint32>(kinds, static_cast<std::size_t>(count)), output); }
        catch (...) { return InteropStatus::InvocationFailed; }
    }

    InteropStatus ORBEDEN_NATIVE_CALL NativeGetField(ComponentHandle component, MemberHandle member, InteropValueAbi* output)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        try { return GetNativeField(component, member, output); }
        catch (...) { return InteropStatus::InvocationFailed; }
    }

    InteropStatus ORBEDEN_NATIVE_CALL NativeSetField(ComponentHandle component, MemberHandle member, const InteropValueAbi* input)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        try { return SetNativeField(component, member, input); }
        catch (...) { return InteropStatus::InvocationFailed; }
    }

    InteropStatus ORBEDEN_NATIVE_CALL NativeInvoke(ComponentHandle component, MemberHandle member, const InteropValueAbi* input, int32 count, InteropValueAbi* output)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        try { return InvokeNative(component, member, input, count, output); }
        catch (...) { return InteropStatus::InvocationFailed; }
    }

    InteropStatus ORBEDEN_NATIVE_CALL NativeRegisterManagedApi(const ManagedScriptInteropApi* api)
    {
        return ScriptInterop::RegisterManagedApi(api);
    }

    InteropStatus DispatchIsValid(ComponentHandle handle)
    {
        if (handle.domain == ComponentDomain::Native) return NativeIsValid(handle);
        if (handle.domain == ComponentDomain::Managed) return CallManaged<ValidateFunction>(managedApi.IsValid, handle);
        return InteropStatus::StaleHandle;
    }

    InteropStatus DispatchResolveField(ComponentHandle handle, std::string_view name, MemberHandle* output)
    {
        if (name.empty()) return InteropStatus::InvalidArgument;
        const uint8* text = reinterpret_cast<const uint8*>(name.data());
        if (handle.domain == ComponentDomain::Native) return NativeResolveField(handle, text, static_cast<int32>(name.size()), output);
        if (handle.domain == ComponentDomain::Managed) return CallManaged<ResolveFieldFunction>(managedApi.ResolveField, handle, text, static_cast<int32>(name.size()), output);
        return InteropStatus::StaleHandle;
    }

    InteropStatus DispatchResolveMethod(ComponentHandle handle, std::string_view name, std::span<const Reflection::ValueKind> kinds, MemberHandle* output)
    {
        List<uint32> rawKinds;
        rawKinds.reserve(kinds.size());
        for (Reflection::ValueKind kind : kinds) rawKinds.push_back(static_cast<uint32>(kind));
        const uint8* text = reinterpret_cast<const uint8*>(name.data());
        const uint32* rawData = rawKinds.empty() ? nullptr : rawKinds.data();
        if (handle.domain == ComponentDomain::Native) return NativeResolveMethod(handle, text, static_cast<int32>(name.size()), rawData, static_cast<int32>(rawKinds.size()), output);
        if (handle.domain == ComponentDomain::Managed) return CallManaged<ResolveMethodFunction>(managedApi.ResolveMethod, handle, text, static_cast<int32>(name.size()), rawData, static_cast<int32>(rawKinds.size()), output);
        return InteropStatus::StaleHandle;
    }

    InteropStatus DispatchGet(ComponentHandle handle, MemberHandle member, InteropValueAbi* output)
    {
        if (handle.domain == ComponentDomain::Native) return NativeGetField(handle, member, output);
        if (handle.domain == ComponentDomain::Managed) return CallManaged<GetFieldFunction>(managedApi.GetField, handle, member, output);
        return InteropStatus::StaleHandle;
    }

    InteropStatus DispatchSet(ComponentHandle handle, MemberHandle member, const InteropValueAbi* input)
    {
        if (handle.domain == ComponentDomain::Native) return NativeSetField(handle, member, input);
        if (handle.domain == ComponentDomain::Managed) return CallManaged<SetFieldFunction>(managedApi.SetField, handle, member, input);
        return InteropStatus::StaleHandle;
    }

    InteropStatus DispatchInvoke(ComponentHandle handle, MemberHandle member, const InteropValueAbi* input, int32 count, InteropValueAbi* output)
    {
        if (handle.domain == ComponentDomain::Native) return NativeInvoke(handle, member, input, count, output);
        if (handle.domain == ComponentDomain::Managed) return CallManaged<InvokeFunction>(managedApi.Invoke, handle, member, input, count, output);
        return InteropStatus::StaleHandle;
    }
}

namespace ScriptInterop
{
    ScriptInteropApi ScriptInteropApi::Create()
    {
        ScriptInteropApi api;
        api.FindNativeByTypeId = reinterpret_cast<void*>(&NativeFindByTypeId);
        api.FindNativeByName = reinterpret_cast<void*>(&NativeFindByName);
        api.IsValid = reinterpret_cast<void*>(&NativeIsValid);
        api.ResolveField = reinterpret_cast<void*>(&NativeResolveField);
        api.ResolveMethod = reinterpret_cast<void*>(&NativeResolveMethod);
        api.GetField = reinterpret_cast<void*>(&NativeGetField);
        api.SetField = reinterpret_cast<void*>(&NativeSetField);
        api.Invoke = reinterpret_cast<void*>(&NativeInvoke);
        api.RegisterManagedApi = reinterpret_cast<void*>(&NativeRegisterManagedApi);
        return api;
    }

    ComponentProxy::ComponentProxy(ComponentHandle value)
        : handle(value)
    {
    }

    bool ComponentProxy::IsValid() const
    {
        return DispatchIsValid(handle) == InteropStatus::Ok;
    }

    const ComponentHandle& ComponentProxy::GetHandle() const
    {
        return handle;
    }

    InteropStatus ComponentProxy::ResolveField(std::string_view name, MemberHandle& member) const
    {
        return DispatchResolveField(handle, name, &member);
    }

    InteropStatus ComponentProxy::ResolveMethod(std::string_view name, std::span<const Reflection::ValueKind> parameterKinds, MemberHandle& member) const
    {
        return DispatchResolveMethod(handle, name, parameterKinds, &member);
    }

    InteropStatus ComponentProxy::ResolveCachedField(std::string_view name, MemberHandle& member) const
    {
        std::string key(name);
        auto found = fieldCache.find(key);
        if (found != fieldCache.end())
        {
            member = found->second;
            return InteropStatus::Ok;
        }

        InteropStatus status = ResolveField(name, member);
        if (status == InteropStatus::Ok) fieldCache.emplace(std::move(key), member);
        return status;
    }

    InteropStatus ComponentProxy::ResolveCachedMethod(std::string_view name, std::span<const Reflection::ValueKind> parameterKinds, MemberHandle& member) const
    {
        std::string key(name);
        for (Reflection::ValueKind kind : parameterKinds)
        {
            key.push_back('#');
            key += std::to_string(static_cast<uint32>(kind));
        }

        auto found = methodCache.find(key);
        if (found != methodCache.end())
        {
            member = found->second;
            return InteropStatus::Ok;
        }

        InteropStatus status = ResolveMethod(name, parameterKinds, member);
        if (status == InteropStatus::Ok) methodCache.emplace(std::move(key), member);
        return status;
    }

    InteropStatus ComponentProxy::TryGetField(std::string_view name, Reflection::Value& value) const
    {
        MemberHandle member;
        InteropStatus status = ResolveCachedField(name, member);
        if (status != InteropStatus::Ok) return status;

        InteropValueAbi abi;
        status = DispatchGet(handle, member, &abi);
        if (status != InteropStatus::Ok) return status;
        return DecodeValue(abi, value) ? InteropStatus::Ok : InteropStatus::UnsupportedType;
    }

    InteropStatus ComponentProxy::SetField(std::string_view name, const Reflection::Value& value) const
    {
        MemberHandle member;
        InteropStatus status = ResolveCachedField(name, member);
        if (status != InteropStatus::Ok) return status;

        std::string stringStorage;
        InteropValueAbi abi;
        if (!EncodeValue(value, abi, &stringStorage)) return InteropStatus::UnsupportedType;
        return DispatchSet(handle, member, &abi);
    }

    InteropStatus ComponentProxy::Invoke(std::string_view name, std::span<const Reflection::Value> args, Reflection::Value& result) const
    {
        List<Reflection::ValueKind> kinds;
        kinds.reserve(args.size());
        for (const Reflection::Value& arg : args) kinds.push_back(arg.GetKind());

        MemberHandle member;
        InteropStatus status = ResolveCachedMethod(name, kinds, member);
        if (status != InteropStatus::Ok) return status;
        return Invoke(member, args, result);
    }

    InteropStatus ComponentProxy::Invoke(const MemberHandle& method, std::span<const Reflection::Value> args, Reflection::Value& result) const
    {
        std::vector<std::string> strings(args.size());
        List<InteropValueAbi> values(args.size());
        for (std::size_t index = 0; index < args.size(); ++index)
        {
            if (!EncodeValue(args[index], values[index], &strings[index])) return InteropStatus::UnsupportedType;
        }

        InteropValueAbi output;
        InteropStatus status = DispatchInvoke(handle, method, values.empty() ? nullptr : values.data(), static_cast<int32>(values.size()), &output);
        if (status != InteropStatus::Ok) return status;
        return DecodeValue(output, result) ? InteropStatus::Ok : InteropStatus::UnsupportedType;
    }

    ComponentProxy FindNativeComponent(EnsId ens, TypeId typeId, int32 occurrence)
    {
        ComponentHandle handle;
        return NativeFindByTypeId(ens, typeId, occurrence, &handle) == InteropStatus::Ok ? ComponentProxy(handle) : ComponentProxy();
    }

    ComponentProxy FindNativeComponent(EnsId ens, std::string_view typeName, int32 occurrence)
    {
        ComponentHandle handle;
        const uint8* name = reinterpret_cast<const uint8*>(typeName.data());
        return NativeFindByName(ens, name, static_cast<int32>(typeName.size()), occurrence, &handle) == InteropStatus::Ok ? ComponentProxy(handle) : ComponentProxy();
    }

    ComponentProxy FindManagedComponent(EnsId ens, std::string_view fullTypeName, int32 occurrence)
    {
        ComponentHandle handle;
        InteropStatus status = CallManaged<FindManagedFunction>(managedApi.FindComponent, ens,
            reinterpret_cast<const uint8*>(fullTypeName.data()), static_cast<int32>(fullTypeName.size()), occurrence, &handle);
        return status == InteropStatus::Ok ? ComponentProxy(handle) : ComponentProxy();
    }

    void Initialize(World* world)
    {
        currentWorld = world;
        mainThread = std::this_thread::get_id();
        ++nativeGeneration;
        if (nativeGeneration == 0) nativeGeneration = 1;
        managedApi = {};
    }

    void Shutdown()
    {
        managedApi = {};
        currentWorld = nullptr;
        ++nativeGeneration;
        if (nativeGeneration == 0) nativeGeneration = 1;
    }

    InteropStatus RegisterManagedApi(const ManagedScriptInteropApi* api)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        managedApi = api ? *api : ManagedScriptInteropApi();
        return InteropStatus::Ok;
    }

    void NotifyManagedHostAttached(ScriptBehaviour* host)
    {
        if (!host || !host->IsManagedHost() || host->GetManagedTypeName().empty()) return;
        CallManaged<HostFunction>(managedApi.HostAttached, host);
    }

    void NotifyManagedHostDetached(ScriptBehaviour* host)
    {
        if (!host || !host->IsManagedHost()) return;
        CallManaged<HostFunction>(managedApi.HostDetached, host);
    }

    void NotifyManagedHostEnabledChanged(ScriptBehaviour* host)
    {
        if (!host || !host->IsManagedHost()) return;
        CallManaged<HostFunction>(managedApi.HostEnabledChanged, host);
    }

    bool NotifyManagedHostFieldChanged(ScriptBehaviour* host, std::string_view fieldName)
    {
        if (!host || !host->IsManagedHost() || fieldName.empty()) return false;
        if (!managedApi.HostFieldChanged) return true;
        InteropStatus status = CallManaged<HostFieldFunction>(managedApi.HostFieldChanged, host,
            reinterpret_cast<const uint8*>(fieldName.data()), static_cast<int32>(fieldName.size()));
        return status == InteropStatus::Ok || status == InteropStatus::NotFound;
    }
}
