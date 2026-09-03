#pragma once

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Runtime/EnsId.h"
#include "Runtime/Native/NativeApiAbi.h"
#include "Runtime/Native/NativeCall.h"
#include "Runtime/Reflection.h"

class World;

namespace ScriptInterop
{
    enum class InteropStatus : uint32
    {
        Ok,
        NotFound,
        StaleHandle,
        InvalidArgument,
        TypeMismatch,
        AmbiguousMethod,
        UnsupportedType,
        WrongThread,
        ReentrancyLimit,
        InvocationFailed,
    };

    enum class ComponentDomain : uint32
    {
        None,
        Native,
        Managed,
    };

    enum class MemberKind : uint32
    {
        None,
        Field,
        Method,
    };

#pragma pack(push, 8)
    //跨域组件实例句柄。
    struct ComponentHandle
    {
        ComponentDomain domain = ComponentDomain::None;
        uint32 generation = 0;
        uint64 slot = 0;
    };

    //跨域预解析成员句柄。
    struct MemberHandle
    {
        ComponentDomain domain = ComponentDomain::None;
        MemberKind kind = MemberKind::None;
        uint64 slot = 0;
        uint32 generation = 0;
        uint32 reserved = 0;
    };

    //固定 24 字节的跨域值；字符串 payload 保存 UTF-8 指针和字节数。
    struct InteropValueAbi
    {
        uint32 kind = 0;
        uint32 reserved = 0;
        uint8 payload[16]{};
    };

    //由 CLR/NativeAOT 初始化时注册给 C++ 的托管组件操作表。
    struct ManagedScriptInteropApi
    {
        void* FindComponent = nullptr;
        void* IsValid = nullptr;
        void* ResolveField = nullptr;
        void* ResolveMethod = nullptr;
        void* GetField = nullptr;
        void* SetField = nullptr;
        void* Invoke = nullptr;
    };

    //随 OrbedenNativeApi 传入托管域的原生组件操作表。
    struct ScriptInteropApi
    {
        void* FindNativeByTypeId = nullptr;
        void* FindNativeByName = nullptr;
        void* IsValid = nullptr;
        void* ResolveField = nullptr;
        void* ResolveMethod = nullptr;
        void* GetField = nullptr;
        void* SetField = nullptr;
        void* Invoke = nullptr;
        void* RegisterManagedApi = nullptr;

        //创建完整原生脚本互操作函数表。
        static ScriptInteropApi Create();
    };
#pragma pack(pop)

    static_assert(sizeof(ComponentHandle) == 16);
    static_assert(sizeof(MemberHandle) == 24);
    static_assert(sizeof(InteropValueAbi) == 24);

    //统一的跨域组件代理；字符串成员名仅在首次解析时使用。
    class ComponentProxy
    {
    private:
        ComponentHandle handle;
        mutable std::unordered_map<std::string, MemberHandle> fieldCache;
        mutable std::unordered_map<std::string, MemberHandle> methodCache;

        InteropStatus ResolveCachedField(std::string_view name, MemberHandle& member) const;
        InteropStatus ResolveCachedMethod(std::string_view name, std::span<const Reflection::ValueKind> parameterKinds, MemberHandle& member) const;

    public:
        ComponentProxy() = default;
        explicit ComponentProxy(ComponentHandle value);

        //判断句柄是否仍指向当前 World/程序集中的实例。
        bool IsValid() const;

        //获取底层跨域句柄。
        const ComponentHandle& GetHandle() const;

        //预解析字段并返回稳定成员句柄。
        InteropStatus ResolveField(std::string_view name, MemberHandle& member) const;

        //按精确参数类型预解析方法。
        InteropStatus ResolveMethod(std::string_view name, std::span<const Reflection::ValueKind> parameterKinds, MemberHandle& member) const;

        //读取字段；结果字符串会在返回前复制进 Reflection::Value。
        InteropStatus TryGetField(std::string_view name, Reflection::Value& value) const;

        //写入字段，不做隐式数值转换。
        InteropStatus SetField(std::string_view name, const Reflection::Value& value) const;

        //同步调用公开实例方法。
        InteropStatus Invoke(std::string_view name, std::span<const Reflection::Value> args, Reflection::Value& result) const;

        //通过预解析方法句柄调用；适用于重复调用，避免再次处理方法名和参数签名。
        InteropStatus Invoke(const MemberHandle& method, std::span<const Reflection::Value> args, Reflection::Value& result) const;
    };

    //按本进程 TypeId 查找原生组件。
    ComponentProxy FindNativeComponent(EnsId ens, TypeId typeId, int32 occurrence = 0);

    //按完整原生类型名查找原生组件。
    ComponentProxy FindNativeComponent(EnsId ens, std::string_view typeName, int32 occurrence = 0);

    //按完整托管类型名查找 C# 脚本组件。
    ComponentProxy FindManagedComponent(EnsId ens, std::string_view fullTypeName, int32 occurrence = 0);

    //初始化主线程和当前 World 上下文。
    void Initialize(World* world);

    //清空 World 和托管函数表并使旧句柄失效。
    void Shutdown();

    //注册或清空托管组件函数表。
    InteropStatus RegisterManagedApi(const ManagedScriptInteropApi* api);
}

ORBEDEN_ASSERT_NATIVE_API_TABLE(ScriptInterop::ManagedScriptInteropApi, 7);
ORBEDEN_ASSERT_NATIVE_API_TABLE(ScriptInterop::ScriptInteropApi, 9);
