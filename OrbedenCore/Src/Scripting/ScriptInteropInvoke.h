#pragma once

#include <cstring>
#include <type_traits>
#include <tuple>
#include <stdexcept>
#include <limits>
#include "Scripting/ScriptInterop.h"

namespace ScriptInterop
{
    template<typename T>
    void DirectArrayElement(const T&);
    template<typename T, typename A>
    T DirectArrayElement(const std::vector<T, A>&);
    template<typename T, std::size_t N>
    T DirectArrayElement(const std::array<T, N>&);
    template<typename T>
    void DirectReferenceTarget(const T&);
    template<typename T>
    T DirectReferenceTarget(const Ref<T>&);

    // 读取 C++ 签名对应的托管分类
    template<typename T>
    constexpr uint32 ManagedValueKind()
    {
        using V = std::remove_cvref_t<T>;
        using K = Reflection::ValueKind;
        if constexpr (std::is_void_v<V>)
        {
            return static_cast<uint32>(K::Empty);
        }
        else if constexpr (std::is_enum_v<V>)
        {
            return ManagedValueKind<std::underlying_type_t<V>>();
        }
        else if constexpr (std::is_same_v<V, bool>)
        {
            return static_cast<uint32>(K::Bool);
        }
        else if constexpr (std::is_same_v<V, int32>)
        {
            return static_cast<uint32>(K::Int32);
        }
        else if constexpr (std::is_same_v<V, uint32>)
        {
            return static_cast<uint32>(K::UInt32);
        }
        else if constexpr (std::is_same_v<V, uint64>)
        {
            return static_cast<uint32>(K::UInt64);
        }
        else if constexpr (std::is_same_v<V, float32>)
        {
            return static_cast<uint32>(K::Float32);
        }
        else if constexpr (std::is_same_v<V, std::string>)
        {
            return static_cast<uint32>(K::String);
        }
        else if constexpr (std::is_same_v<V, StringId>)
        {
            return static_cast<uint32>(K::StringId);
        }
        else if constexpr (std::is_same_v<V, vector2>)
        {
            return static_cast<uint32>(K::Vector2);
        }
        else if constexpr (std::is_same_v<V, vector3>)
        {
            return static_cast<uint32>(K::Vector3);
        }
        else if constexpr (std::is_same_v<V, color>)
        {
            return static_cast<uint32>(K::Color);
        }
        else if constexpr (std::is_same_v<V, quaternion>)
        {
            return static_cast<uint32>(K::Quaternion);
        }
        else if constexpr (std::is_same_v<V, EnsId>)
        {
            return static_cast<uint32>(K::EnsId);
        }
        else if constexpr (std::is_pointer_v<V>)
        {
            return static_cast<uint32>(K::Pointer);
        }
        else if constexpr (!std::is_void_v<decltype(DirectReferenceTarget(std::declval<V>()))>)
        {
            return static_cast<uint32>(K::Object);
        }
        else if constexpr (!std::is_void_v<decltype(DirectArrayElement(std::declval<V>()))>)
        {
            constexpr uint32 element = ManagedValueKind<decltype(DirectArrayElement(std::declval<V>()))>();
            if constexpr (element == 0 || element > static_cast<uint32>(K::Pointer) || element == static_cast<uint32>(K::Array))
            {
                return 0xffffffffu;
            }
            else
            {
                return static_cast<uint32>(K::Array) | element << 16;
            }
        }
        else
        {
            return 0xffffffffu;
        }
    }

    // 读取每个元素的原生布局尺寸
    template<typename T>
    constexpr std::size_t DirectSize()
    {
        if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, StringId>)
        {
            return sizeof(NativeUtf8);
        }
        else if constexpr (!std::is_void_v<decltype(DirectArrayElement(std::declval<T>()))>)
        {
            return sizeof(NativeUtf8);
        }
        else if constexpr (!std::is_void_v<decltype(DirectReferenceTarget(std::declval<T>()))>)
        {
            return sizeof(int32);
        }
        else
        {
            return sizeof(T);
        }
    }

    // 从参数自身的原生布局读取具体类型
    template<typename T>
    bool ReadDirectValue(const void* input, T& value)
    {
        if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, StringId>)
        {
            NativeUtf8 view;
            std::memcpy(&view, input, sizeof(view));
            std::string text;
            if (view.length > 0)
            {
                text.assign(reinterpret_cast<const char*>(view.data), static_cast<std::size_t>(view.length));
            }
            if constexpr (std::is_same_v<T, StringId>)
            {
                value = StringId(text);
            }
            else
            {
                value = std::move(text);
            }
        }
        else if constexpr (!std::is_void_v<decltype(DirectReferenceTarget(std::declval<T>()))>)
        {
            int32 id;
            std::memcpy(&id, input, sizeof(id));
            Object* object = id ? Object::FindObjectById(id) : nullptr;
            using Target = decltype(DirectReferenceTarget(std::declval<T>()));
            if (id && (!object || !object->GetType()->Is(Target::StaticType())))
            {
                return false;
            }
            value = T(static_cast<Target*>(object));
        }
        else if constexpr (!std::is_void_v<decltype(DirectArrayElement(std::declval<T>()))>)
        {
            using Element = decltype(DirectArrayElement(std::declval<T>()));
            NativeUtf8 view;
            std::memcpy(&view, input, sizeof(view));
            if constexpr (requires { value.resize(0); })
            {
                value.resize(static_cast<std::size_t>(view.length));
            }
            else if (value.size() != static_cast<std::size_t>(view.length))
            {
                return false;
            }
            for (int32 index = 0; index < view.length; ++index)
            {
                Element element{};
                if (!ReadDirectValue(view.data + index * DirectSize<Element>(), element))
                {
                    return false;
                }
                value[index] = std::move(element);
            }
        }
        else
        {
            static_assert(std::is_trivially_copyable_v<T>);
            std::memcpy(&value, input, sizeof(T));
        }
        return true;
    }

    // 把结果写入自身的原生布局，托管侧在本次调用内复制
    template<typename T>
    void WriteDirectValue(const T& value, void* output)
    {
        if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, StringId>)
        {
            NativeUtf8 view;
            if constexpr (std::is_same_v<T, StringId>)
            {
                view = StoreDirectString(value.GetPath());
            }
            else
            {
                view = StoreDirectString(value);
            }
            std::memcpy(output, &view, sizeof(view));
        }
        else if constexpr (!std::is_void_v<decltype(DirectReferenceTarget(std::declval<T>()))>)
        {
            auto* object = value.Get();
            int32 id = object ? object->GetObjectId() : 0;
            std::memcpy(output, &id, sizeof(id));
        }
        else if constexpr (!std::is_void_v<decltype(DirectArrayElement(std::declval<T>()))>)
        {
            using Element = decltype(DirectArrayElement(std::declval<T>()));
            static thread_local List<uint8> buffer;
            static thread_local List<std::string> strings;
            buffer.resize(value.size() * DirectSize<Element>());
            if constexpr (std::is_same_v<Element, std::string> || std::is_same_v<Element, StringId>)
            {
                strings.resize(value.size());
            }
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                void* destination = buffer.data() + index * DirectSize<Element>();
                if constexpr (std::is_same_v<Element, std::string> || std::is_same_v<Element, StringId>)
                {
                    if constexpr (std::is_same_v<Element, StringId>)
                    {
                        strings[index] = value[index].GetPath();
                    }
                    else
                    {
                        strings[index] = value[index];
                    }
                    NativeUtf8 text{reinterpret_cast<const uint8*>(strings[index].data()), static_cast<int32>(strings[index].size()), 0};
                    std::memcpy(destination, &text, sizeof(text));
                }
                else
                {
                    Element element = value[index];
                    WriteDirectValue(element, destination);
                }
            }
            NativeUtf8 view{buffer.data(), static_cast<int32>(value.size()), 0};
            std::memcpy(output, &view, sizeof(view));
        }
        else
        {
            static_assert(std::is_trivially_copyable_v<T>);
            std::memcpy(output, &value, sizeof(T));
        }
    }

    // 从 ABI 参数读取具体 C++ 类型
    template<typename T>
    bool ReadArgument(const DynamicValueAbi& input, T& value)
    {
        if constexpr (!std::is_void_v<decltype(DirectReferenceTarget(std::declval<T>()))>)
        {
            return ReadDirectValue(input.payload, value);
        }
        else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, StringId>)
        {
            const uint8* pointer = nullptr;
            int32 length = 0;
            std::memcpy(&pointer, input.payload, sizeof(pointer));
            std::memcpy(&length, input.payload + 8, sizeof(length));
            if (length < 0 || (length > 0 && !pointer))
            {
                return false;
            }
            std::string text;
            if (length > 0)
            {
                text.assign(reinterpret_cast<const char*>(pointer), static_cast<std::size_t>(length));
            }
            if constexpr (std::is_same_v<T, StringId>)
            {
                value = StringId(text);
            }
            else
            {
                value = std::move(text);
            }
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            value = input.payload[0] != 0;
        }
        else
        {
            static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= sizeof(input.payload));
            std::memcpy(&value, input.payload, sizeof(T));
        }
        return true;
    }

    // 把具体返回值直接写入 ABI
    template<typename T>
    InteropStatus WriteResult(const T& value, Reflection::ValueKind kind, DynamicValueAbi* output, std::string& stringStorage)
    {
        *output = {};
        output->kind = static_cast<uint32>(kind);
        if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, StringId>)
        {
            if constexpr (std::is_same_v<T, StringId>)
            {
                stringStorage = value.GetPath();
            }
            else
            {
                stringStorage = value;
            }
            const uint8* pointer = reinterpret_cast<const uint8*>(stringStorage.data());
            const int32 length = static_cast<int32>(stringStorage.size());
            std::memcpy(output->payload, &pointer, sizeof(pointer));
            std::memcpy(output->payload + 8, &length, sizeof(length));
        }
        else if constexpr (!std::is_void_v<decltype(DirectReferenceTarget(std::declval<T>()))>)
        {
            WriteDirectValue(value, output->payload);
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            output->payload[0] = value ? uint8(1) : uint8(0);
        }
        else
        {
            static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= sizeof(output->payload));
            std::memcpy(output->payload, &value, sizeof(T));
        }
        return InteropStatus::Ok;
    }

    // 托管调用失败时保留原始状态
    class ManagedCallException : public std::runtime_error
    {
      public:
        InteropStatus status;
        explicit ManagedCallException(InteropStatus value) : std::runtime_error("Managed method call failed"), status(value) {}
    };

    // 借用标量地址并保存引用参数的调用期布局
    template<typename T>
    class ManagedArgument
    {
        const T* borrowed;
        int32 objectId = 0;
        NativeUtf8 view;
        List<uint8> bytes;
        List<std::string> strings;

      public:
        explicit ManagedArgument(const T& value) : borrowed(&value)
        {
            if constexpr (std::is_same_v<T, std::string>)
            {
                if (value.size() > static_cast<std::size_t>(std::numeric_limits<int32>::max()))
                {
                    throw std::length_error("Managed string is too large");
                }
                view = {reinterpret_cast<const uint8*>(value.data()), static_cast<int32>(value.size()), 0};
            }
            else if constexpr (std::is_same_v<T, StringId>)
            {
                strings.push_back(value.GetPath());
                view = {reinterpret_cast<const uint8*>(strings[0].data()), static_cast<int32>(strings[0].size()), 0};
            }
            else if constexpr (!std::is_void_v<decltype(DirectReferenceTarget(std::declval<T>()))>)
            {
                objectId = value.Get() ? value.Get()->GetObjectId() : 0;
            }
            else if constexpr (!std::is_void_v<decltype(DirectArrayElement(std::declval<T>()))>)
            {
                using Element = decltype(DirectArrayElement(std::declval<T>()));
                if (value.size() > static_cast<std::size_t>(std::numeric_limits<int32>::max()))
                {
                    throw std::length_error("Managed array is too large");
                }
                bytes.resize(value.size() * DirectSize<Element>());
                if constexpr (std::is_same_v<Element, std::string> || std::is_same_v<Element, StringId>)
                {
                    strings.resize(value.size());
                }
                for (std::size_t index = 0; index < value.size(); ++index)
                {
                    void* output = bytes.data() + index * DirectSize<Element>();
                    if constexpr (std::is_same_v<Element, std::string> || std::is_same_v<Element, StringId>)
                    {
                        if constexpr (std::is_same_v<Element, StringId>)
                        {
                            strings[index] = value[index].GetPath();
                        }
                        else
                        {
                            strings[index] = value[index];
                        }
                        NativeUtf8 text{reinterpret_cast<const uint8*>(strings[index].data()), static_cast<int32>(strings[index].size()),
                                        0};
                        std::memcpy(output, &text, sizeof(text));
                    }
                    else
                    {
                        Element element = value[index];
                        WriteDirectValue(element, output);
                    }
                }
                view = {bytes.data(), static_cast<int32>(value.size()), 0};
            }
            else
            {
                static_assert(std::is_trivially_copyable_v<T>);
            }
        }

        // 返回参数自身的原生布局地址
        const void* Data() const
        {
            if constexpr (!std::is_void_v<decltype(DirectReferenceTarget(std::declval<T>()))>)
            {
                return &objectId;
            }
            else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, StringId> ||
                               !std::is_void_v<decltype(DirectArrayElement(std::declval<T>()))>)
            {
                return &view;
            }
            else
            {
                return borrowed;
            }
        }
    };

    // 在结果复制完成后释放托管存储
    class ManagedCallFrame
    {
        void* complete;

      public:
        void* entry = nullptr;
        void* context = nullptr;
        bool invoked = false;

        ManagedCallFrame(ComponentHandle component, MemberHandle member)
        {
            InteropStatus status = PrepareManagedCall(component, member, entry, context, complete);
            if (status != InteropStatus::Ok)
            {
                throw ManagedCallException(status);
            }
        }
        ~ManagedCallFrame()
        {
            CompleteManagedCall(complete, invoked);
        }
        ManagedCallFrame(const ManagedCallFrame&) = delete;
        ManagedCallFrame& operator=(const ManagedCallFrame&) = delete;
    };

    template<typename Result, typename... Args>
    class ManagedMethod<Result(Args...)>
    {
        ComponentHandle component;
        MemberHandle member;

      public:
        ManagedMethod() = default;
        ManagedMethod(ComponentHandle owner, MemberHandle binding) : component(owner), member(binding) {}

        // 调用绑定时创建的强类型委托
        Result operator()(const Args&... args) const
        {
            static_assert(sizeof...(Args) <= 16, "Managed methods support up to sixteen arguments");
            std::tuple<ManagedArgument<std::remove_cvref_t<Args>>...> storage{ManagedArgument<std::remove_cvref_t<Args>>(args)...};
            return std::apply(
                [&](const auto&... inputs) -> Result
                {
                    std::array<const void*, sizeof...(Args)> addresses{inputs.Data()...};
                    ManagedCallFrame call(component, member);
                    using Entry = InteropStatus(ORBEDEN_NATIVE_CALL*)(void*, const void* const*, void*);
                    InteropStatus status;
                    if constexpr (std::is_void_v<Result>)
                    {
                        call.invoked = true;
                        status = reinterpret_cast<Entry>(call.entry)(call.context, addresses.data(), nullptr);
                        if (status != InteropStatus::Ok)
                        {
                            throw ManagedCallException(status);
                        }
                    }
                    else
                    {
                        std::array<uint8, DirectSize<Result>()> output{};
                        call.invoked = true;
                        status = reinterpret_cast<Entry>(call.entry)(call.context, addresses.data(), output.data());
                        if (status != InteropStatus::Ok)
                        {
                            throw ManagedCallException(status);
                        }
                        Result result{};
                        if (!ReadDirectValue(output.data(), result))
                        {
                            throw ManagedCallException(InteropStatus::TypeMismatch);
                        }
                        return result;
                    }
                },
                storage);
        }

        // 预解析完整签名并保存不透明绑定
        static InteropStatus Bind(ComponentHandle owner, std::string_view name, ManagedMethod& method)
        {
            constexpr std::array<uint32, sizeof...(Args)> kinds{ManagedValueKind<Args>()...};
            if (ManagedValueKind<Result>() == 0xffffffffu || ((ManagedValueKind<Args>() == 0xffffffffu) || ...))
            {
                return InteropStatus::UnsupportedType;
            }
            MemberHandle binding;
            InteropStatus status = BindManagedMethod(owner, name, ManagedValueKind<Result>(), kinds, binding);
            if (status == InteropStatus::Ok)
            {
                method = ManagedMethod(owner, binding);
            }
            return status;
        }
    };

    template<typename Signature>
    InteropStatus ComponentProxy::TryBindMethod(std::string_view name, ManagedMethod<Signature>& method) const
    {
        return ManagedMethod<Signature>::Bind(handle, name, method);
    }
} // namespace ScriptInterop
