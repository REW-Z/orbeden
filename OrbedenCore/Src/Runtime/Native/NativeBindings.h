#pragma once

#include "Runtime/Object/Object.h"
#include "Runtime/EnsId.h"
#include "Runtime/Native/NativeCall.h"
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

//动态内容在调用期间传递；禁止将 STL 容器布局跨 ABI 暴露。
#pragma pack(push, 8)
struct NativeBindingSlice { const uint8* data = nullptr; int32 length = 0; };
struct NativeBindingBuffer { uint8* data = nullptr; int32 length = 0; };
#pragma pack(pop)

enum class NativeBindingStatus : uint32 { Ok, InvalidObject, InvalidArgument, InvocationFailed, TypeMismatch };

class NativeBindingError : public std::runtime_error
{
public:
    NativeBindingStatus status;
    NativeBindingError(NativeBindingStatus value, const char* message) : std::runtime_error(message), status(value) {}
};

class NativeBindingWriter
{
    std::vector<uint8> bytes;
public:
    template<typename T> void Scalar(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const uint8* start = reinterpret_cast<const uint8*>(&value);
        bytes.insert(bytes.end(), start, start + sizeof(T));
    }
    void Text(const std::string& value)
    {
        if (value.size() > static_cast<size_t>(std::numeric_limits<int32>::max())) throw std::length_error("Binding string is too large");
        Scalar(static_cast<int32>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    NativeBindingBuffer Finish() const;
};

class NativeBindingReader
{
    const uint8* data;
    int32 remaining;
public:
    explicit NativeBindingReader(NativeBindingSlice value) : data(value.data), remaining(value.length)
    {
        if (remaining < 0 || remaining && !data) throw NativeBindingError(NativeBindingStatus::InvalidArgument, "Invalid binding buffer");
    }
    template<typename T> T Scalar()
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (remaining < static_cast<int32>(sizeof(T))) throw NativeBindingError(NativeBindingStatus::InvalidArgument, "Truncated binding value");
        T value; std::memcpy(&value, data, sizeof(T)); data += sizeof(T); remaining -= sizeof(T); return value;
    }
    int32 Count()
    {
        int32 count = Scalar<int32>();
        if (count < 0 || count > remaining) throw NativeBindingError(NativeBindingStatus::InvalidArgument, "Invalid binding count");
        return count;
    }
    std::string Text()
    {
        int32 length = Count(); std::string value(reinterpret_cast<const char*>(data), length);
        data += length; remaining -= length; return value;
    }
    void Complete() const
    {
        if (remaining != 0) throw NativeBindingError(NativeBindingStatus::InvalidArgument, "Trailing binding bytes");
    }
};

namespace NativeBindings
{
    //由生成模块注册与实际原生类型关联的类型化函数表。
    void Register(Type* type, uint64 signature, std::span<void* const> functions);
    void Unregister(Type* type);
    uint32 GetGeneration();
    NativeBindingStatus Resolve(const char* typeName, int32 length, uint64 signature, uint32* typeId, const void*** functions);
    void ReleaseBuffer(NativeBindingBuffer buffer);
    NativeBindingBuffer AllocateBuffer(std::span<const uint8> data);

    template<typename T> T* Require(int32 id)
    {
        Object* object = Object::FindObjectById(id);
        T* instance = object ? object->Cast<T>() : nullptr;
        if (!instance) throw NativeBindingError(NativeBindingStatus::InvalidObject, "Binding object is no longer alive or has another type");
        return instance;
    }
    template<typename T> T* Optional(int32 id) { return id == 0 ? nullptr : Require<T>(id); }
    inline int32 Id(const Object* object) { return object ? object->GetObjectId() : 0; }
}

inline NativeBindingBuffer NativeBindingWriter::Finish() const { return NativeBindings::AllocateBuffer(bytes); }

#pragma pack(push, 8)
struct NativeBindingsApi
{
    void* GetGeneration = nullptr;
    void* ResolveType = nullptr;
    void* GetObjectTypeName = nullptr;
    void* GetObjectPointer = nullptr;
    void* GetObjectEns = nullptr;
    void* CreateObject = nullptr;
    void* AddComponent = nullptr;
    void* GetComponents = nullptr;
    void* LoadResource = nullptr;
    void* ReleaseBuffer = nullptr;
    static NativeBindingsApi Create();
};
#pragma pack(pop)

static_assert(sizeof(NativeBindingSlice) == (sizeof(void*) == 8 ? 16 : 8));
static_assert(sizeof(NativeBindingBuffer) == sizeof(NativeBindingSlice));
static_assert(offsetof(NativeBindingSlice, length) == sizeof(void*));
static_assert(offsetof(NativeBindingBuffer, length) == sizeof(void*));
static_assert(sizeof(NativeBindingsApi) == 10 * sizeof(void*));