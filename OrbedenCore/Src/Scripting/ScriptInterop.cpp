#include "Scripting/ScriptInterop.h"

#include <cstring>
#include <exception>
#include <stdexcept>
#include <limits>
#include <thread>
#include <vector>

#include "Runtime/Object/Ens.h"
#include "Runtime/World.h"
#include "Runtime/Object/Script.h"

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

    struct ResolvedNativeMember
    {
        Type* instanceType;
        MemberKind kind;
        const void* address;
    };
    List<ResolvedNativeMember> nativeMembers;
    uint32 memberGeneration = 1;
    uint32 memberRegistryGeneration = 0;
    Component* cachedComponent = nullptr;
    uint64 cachedComponentId = 0;

    class ComponentCache final : public IObjectDestroyListener
    {
    public:
        //销毁前清空最近访问的组件指针
        void OnObjectDestroyed(Object* object) override
        {
            if (object != cachedComponent) return;
            cachedComponent = nullptr;
            cachedComponentId = 0;
        }
    } componentCache;

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

        //把当前调用的退出职责交给类型化调用作用域
        void Transfer() { entered = false; }

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
        case Reflection::ValueKind::Vector2:
        {
            vector2 current;
            if (!value.TryGet(current)) return false;
            WritePayload(destination, current);
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
        case Reflection::ValueKind::Vector2: value = Reflection::Value(ReadPayload<vector2>(source)); return true;
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
        if (!currentWorld) return nullptr;
        if (handle.domain != ComponentDomain::Native || handle.generation != nativeGeneration || handle.slot > static_cast<uint64>(std::numeric_limits<int32>::max())) return nullptr;
        if (handle.slot == cachedComponentId && cachedComponent && cachedComponent->GetWorld() == currentWorld) return cachedComponent;
        Object* object = Object::FindObjectById(static_cast<int32>(handle.slot));
        Component* component = object ? object->Cast<Component>() : nullptr;
        if (!component || component->GetWorld() != currentWorld) return nullptr;
        cachedComponentId = handle.slot;
        cachedComponent = component;
        return component;
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

    //切换成员代次并清空登记槽位
    void ClearNativeMembers()
    {
        nativeMembers.clear();
        memberRegistryGeneration = Reflection::GetRegistryGeneration();
        if (++memberGeneration == 0) memberGeneration = 1;
    }

    //解析时登记成员归属，调用时按槽位读取
    MemberHandle AddNativeMember(Type* type, MemberKind kind, const void* address)
    {
        if (memberRegistryGeneration != Reflection::GetRegistryGeneration()) ClearNativeMembers();
        const uint32 generation = memberGeneration;
        for (std::size_t index = 0; index < nativeMembers.size(); ++index)
        {
            const ResolvedNativeMember& entry = nativeMembers[index];
            if (entry.instanceType == type && entry.kind == kind && entry.address == address)
                return { ComponentDomain::Native, kind, index + 1, generation, 0 };
        }
        nativeMembers.push_back({ type, kind, address });
        return { ComponentDomain::Native, kind, nativeMembers.size(), generation, 0 };
    }

    InteropStatus ResolveNativeField(ComponentHandle component, std::string_view name, MemberHandle* output)
    {
        Component* instance = ResolveNativeComponent(component);
        if (!instance) return InteropStatus::StaleHandle;
        if (!output || name.empty()) return InteropStatus::InvalidArgument;
        const Reflection::FieldInfo* field = Reflection::FindField(instance->GetType(), std::string(name));
        if (!field) return InteropStatus::NotFound;
        if (field->kind == Reflection::FieldKind::Unsupported) return InteropStatus::UnsupportedType;
        *output = AddNativeMember(instance->GetType(), MemberKind::Field, field);
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
            if (rawKind > static_cast<uint32>(Reflection::ValueKind::Object) && rawKind != static_cast<uint32>(Reflection::ValueKind::Vector2)) return InteropStatus::UnsupportedType;
            kinds.push_back(static_cast<Reflection::ValueKind>(rawKind));
        }

        bool ambiguous = false;
        const Reflection::MethodInfo* method = Reflection::FindMethod(instance->GetType(), std::string(name), kinds, &ambiguous);
        if (ambiguous) return InteropStatus::AmbiguousMethod;
        if (!method) return InteropStatus::NotFound;
        *output = AddNativeMember(instance->GetType(), MemberKind::Method, method);
        return InteropStatus::Ok;
    }

    //校验代次和登记槽位，不再遍历类型元数据
    InteropStatus ValidateNativeMember(Type* type, MemberHandle member, MemberKind expected, const void*& address)
    {
        address = nullptr;
        if (member.domain != ComponentDomain::Native || member.kind != expected || !member.slot) return InteropStatus::InvalidArgument;
        if (member.generation != memberGeneration || memberRegistryGeneration != Reflection::GetRegistryGeneration()) return InteropStatus::StaleHandle;
        if (member.slot > nativeMembers.size()) return InteropStatus::InvalidArgument;
        const ResolvedNativeMember& entry = nativeMembers[static_cast<std::size_t>(member.slot - 1)];
        if (entry.instanceType != type || entry.kind != expected) return InteropStatus::InvalidArgument;
        address = entry.address;
        return InteropStatus::Ok;
    }

    InteropStatus GetNativeField(ComponentHandle component, MemberHandle member, InteropValueAbi* output)
    {
        Component* instance = ResolveNativeComponent(component);
        if (!instance) return InteropStatus::StaleHandle;
        const void* address = nullptr;
        InteropStatus memberStatus = ValidateNativeMember(instance->GetType(), member, MemberKind::Field, address);
        if (memberStatus != InteropStatus::Ok) return memberStatus;
        if (!output) return InteropStatus::InvalidArgument;

        const Reflection::FieldInfo* field = static_cast<const Reflection::FieldInfo*>(address);
        Reflection::Value value = field->GetValue(instance);
        if (value.IsEmpty() && field->kind != Reflection::FieldKind::Unsupported) return InteropStatus::InvocationFailed;
        utf8Result.clear();
        return EncodeValue(value, *output, &utf8Result) ? InteropStatus::Ok : InteropStatus::UnsupportedType;
    }

    InteropStatus SetNativeField(ComponentHandle component, MemberHandle member, const InteropValueAbi* input)
    {
        Component* instance = ResolveNativeComponent(component);
        if (!instance) return InteropStatus::StaleHandle;
        const void* address = nullptr;
        InteropStatus memberStatus = ValidateNativeMember(instance->GetType(), member, MemberKind::Field, address);
        if (memberStatus != InteropStatus::Ok) return memberStatus;
        if (!input) return InteropStatus::InvalidArgument;

        const Reflection::FieldInfo* field = static_cast<const Reflection::FieldInfo*>(address);
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
        const void* address = nullptr;
        InteropStatus memberStatus = ValidateNativeMember(instance->GetType(), member, MemberKind::Method, address);
        if (memberStatus != InteropStatus::Ok) return memberStatus;
        if (count < 0 || (count > 0 && !input) || !output) return InteropStatus::InvalidArgument;

        const Reflection::MethodInfo* method = static_cast<const Reflection::MethodInfo*>(address);
        if (count != static_cast<int32>(method->parameters.size())) return InteropStatus::TypeMismatch;
        for (int32 index = 0; index < count; ++index)
        {
            if (input[index].kind > static_cast<uint32>(Reflection::ValueKind::Object)
                && input[index].kind != static_cast<uint32>(Reflection::ValueKind::Vector2)) return InteropStatus::UnsupportedType;
            if (input[index].kind != static_cast<uint32>(method->parameters[index].kind)) return InteropStatus::TypeMismatch;
        }
        if (!method->abiInvoker) return InteropStatus::UnsupportedType;
        return method->abiInvoker(instance, input, output, utf8Result);
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

    InteropStatus ORBEDEN_NATIVE_CALL NativeFindByTypeRuntimeId(EnsId ens, uint32 typeRuntimeId, int32 occurrence, ComponentHandle* output)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        try { return FindNative(ens, Object::FindType(typeRuntimeId), occurrence, output); }
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

    //绑定时一次性校验完整参数和返回类型
    InteropStatus ORBEDEN_NATIVE_CALL ResolveDirect(ComponentHandle component, const uint8* name, int32 length, uint32 returnKind, const uint32* kinds, int32 count, MemberHandle* output)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        if (!name || length <= 0 || count < 0 || (count && !kinds) || !output) return InteropStatus::InvalidArgument;
        try
        {
            MemberHandle methodHandle;
            status = ResolveNativeMethod(component, ReadUtf8(name, length), { kinds, static_cast<std::size_t>(count) }, &methodHandle);
            if (status != InteropStatus::Ok) return status;
            Component* instance = ResolveNativeComponent(component);
            const void* address = nullptr;
            status = ValidateNativeMember(instance->GetType(), methodHandle, MemberKind::Method, address);
            if (status != InteropStatus::Ok) return status;
            const auto* method = static_cast<const Reflection::MethodInfo*>(address);
            if (static_cast<uint32>(method->returnKind) != returnKind) return InteropStatus::TypeMismatch;
            if (!method->directInvoker) return InteropStatus::UnsupportedType;
            *output = AddNativeMember(instance->GetType(), MemberKind::DirectMethod, method);
            return InteropStatus::Ok;
        }
        catch (...) { return InteropStatus::InvocationFailed; }
    }

    //此入口只读注册表，不分配、不调用脚本，允许省略 GC 切换
    InteropStatus ORBEDEN_NATIVE_CALL PrepareDirect(ComponentHandle component, MemberHandle member, void** instanceOutput, void** entryOutput)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        if (!instanceOutput || !entryOutput) return InteropStatus::InvalidArgument;
        *instanceOutput = nullptr;
        *entryOutput = nullptr;
        Component* instance = ResolveNativeComponent(component);
        if (!instance) return InteropStatus::StaleHandle;
        const void* address = nullptr;
        status = ValidateNativeMember(instance->GetType(), member, MemberKind::DirectMethod, address);
        if (status != InteropStatus::Ok) return status;
        const auto* method = static_cast<const Reflection::MethodInfo*>(address);
        if (!method->directInvoker) return InteropStatus::UnsupportedType;
        *instanceOutput = static_cast<Object*>(instance);
        *entryOutput = method->directInvoker;
        guard.Transfer();
        return InteropStatus::Ok;
    }

    //匹配成功的 Prepare，调用期间禁止卸载原生模块
    void ORBEDEN_NATIVE_CALL CompleteDirect()
    {
        if (std::this_thread::get_id() == mainThread && reentrancyDepth > 0) --reentrancyDepth;
    }

    //只在绑定时查找字段并校验完整类型
    InteropStatus ORBEDEN_NATIVE_CALL ResolveDirectField(ComponentHandle component, const uint8* name, int32 length, uint32 valueKind, MemberHandle* output, uint32* access)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        if (!name || length <= 0 || !output || !access) return InteropStatus::InvalidArgument;
        *output = {};
        *access = 0;
        try
        {
            Component* instance = ResolveNativeComponent(component);
            if (!instance) return InteropStatus::StaleHandle;
            const Reflection::FieldInfo* field = Reflection::FindField(instance->GetType(), std::string(ReadUtf8(name, length)));
            if (!field) return InteropStatus::NotFound;
            if (field->directKind == Reflection::ValueKind::Empty || (!field->directGetter && !field->directSetter)) return InteropStatus::UnsupportedType;
            if (static_cast<uint32>(field->directKind) != valueKind) return InteropStatus::TypeMismatch;
            *output = AddNativeMember(instance->GetType(), MemberKind::DirectField, field);
            *access = (field->directGetter ? 1u : 0u) | (field->directSetter ? 2u : 0u);
            return InteropStatus::Ok;
        }
        catch (...) { return InteropStatus::InvocationFailed; }
    }

    //只校验句柄及访问权限；实际读写使用正常 GC 切换
    InteropStatus ORBEDEN_NATIVE_CALL PrepareDirectField(ComponentHandle component, MemberHandle member, uint32 access, void** instanceOutput, void** entryOutput)
    {
        CallGuard guard;
        InteropStatus status = guard.Enter();
        if (status != InteropStatus::Ok) return status;
        if (!instanceOutput || !entryOutput || access > 2) return InteropStatus::InvalidArgument;
        *instanceOutput = nullptr;
        *entryOutput = nullptr;
        Component* instance = ResolveNativeComponent(component);
        if (!instance) return InteropStatus::StaleHandle;
        const void* address = nullptr;
        status = ValidateNativeMember(instance->GetType(), member, MemberKind::DirectField, address);
        if (status != InteropStatus::Ok) return status;
        const auto* field = static_cast<const Reflection::FieldInfo*>(address);
        void* entry = access == 1 ? field->directGetter : access == 2 ? field->directSetter : nullptr;
        if (access && !entry) return InteropStatus::UnsupportedType;
        *instanceOutput = static_cast<Object*>(instance);
        *entryOutput = entry;
        guard.Transfer();
        return InteropStatus::Ok;
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
        api.FindNativeByTypeRuntimeId = reinterpret_cast<void*>(&NativeFindByTypeRuntimeId);
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

    ComponentProxy FindNativeComponent(EnsId ens, TypeRuntimeId typeRuntimeId, int32 occurrence)
    {
        ComponentHandle handle;
        return NativeFindByTypeRuntimeId(ens, typeRuntimeId, occurrence, &handle) == InteropStatus::Ok ? ComponentProxy(handle) : ComponentProxy();
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

    const TypedMethodsApi* GetTypedMethodsApi()
    {
        static const TypedMethodsApi api
        {
            reinterpret_cast<void*>(&ResolveDirect),
            reinterpret_cast<void*>(&PrepareDirect),
            reinterpret_cast<void*>(&CompleteDirect),
        };
        return &api;
    }

    const TypedFieldsApi* GetTypedFieldsApi()
    {
        static const TypedFieldsApi api
        {
            reinterpret_cast<void*>(&ResolveDirectField),
            reinterpret_cast<void*>(&PrepareDirectField),
            reinterpret_cast<void*>(&CompleteDirect),
        };
        return &api;
    }

    bool HasActiveCalls() { return reentrancyDepth != 0; }

    NativeUtf8 StoreDirectString(std::string_view text)
    {
        if (text.size() > static_cast<std::size_t>(std::numeric_limits<int32>::max())) throw std::length_error("Native UTF-8 result is too large");
        utf8Result.assign(text);
        return { reinterpret_cast<const uint8*>(utf8Result.data()), static_cast<int32>(utf8Result.size()), 0 };
    }

    void Initialize(World* world)
    {
        cachedComponent = nullptr;
        cachedComponentId = 0;
        ClearNativeMembers();
        Object::AddDestroyListener(&componentCache);
        currentWorld = world;
        mainThread = std::this_thread::get_id();
        ++nativeGeneration;
        if (nativeGeneration == 0) nativeGeneration = 1;
        managedApi = {};
    }

    void Shutdown()
    {
        Object::RemoveDestroyListener(&componentCache);
        cachedComponent = nullptr;
        cachedComponentId = 0;
        ClearNativeMembers();
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

    void NotifyManagedHostAttached(Script* host)
    {
        if (!host || !host->IsManagedHost() || host->GetManagedTypeName().empty()) return;
        CallManaged<HostFunction>(managedApi.HostAttached, host);
    }

    void NotifyManagedHostDetached(Script* host)
    {
        if (!host || !host->IsManagedHost()) return;
        CallManaged<HostFunction>(managedApi.HostDetached, host);
    }

    void NotifyManagedHostEnabledChanged(Script* host)
    {
        if (!host || !host->IsManagedHost()) return;
        CallManaged<HostFunction>(managedApi.HostEnabledChanged, host);
    }

    bool NotifyManagedHostFieldChanged(Script* host, std::string_view fieldName)
    {
        if (!host || !host->IsManagedHost() || fieldName.empty()) return false;
        if (!managedApi.HostFieldChanged) return true;
        InteropStatus status = CallManaged<HostFieldFunction>(managedApi.HostFieldChanged, host,
            reinterpret_cast<const uint8*>(fieldName.data()), static_cast<int32>(fieldName.size()));
        return status == InteropStatus::Ok || status == InteropStatus::NotFound;
    }
}
