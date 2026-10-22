#pragma once

#include <cstring>
#include <type_traits>
#include "Scripting/ScriptInterop.h"

namespace ScriptInterop
{
    //读取无类型标签的 UTF-8 参数
    inline bool ReadDirectString(NativeUtf8 view, std::string& text)
    {
        if (view.length < 0 || (view.length > 0 && !view.data)) return false;
        if (view.length == 0) text.clear();
        else text.assign(reinterpret_cast<const char*>(view.data), static_cast<std::size_t>(view.length));
        return true;
    }

    //从 ABI 参数读取具体 C++ 类型
    template<typename T>
    bool ReadArgument(const InteropValueAbi& input, T& value)
    {
        if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, StringId>)
        {
            const uint8* pointer = nullptr;
            int32 length = 0;
            std::memcpy(&pointer, input.payload, sizeof(pointer));
            std::memcpy(&length, input.payload + 8, sizeof(length));
            if (length < 0 || (length > 0 && !pointer)) return false;
            std::string text;
            if (length > 0) text.assign(reinterpret_cast<const char*>(pointer), static_cast<std::size_t>(length));
            if constexpr (std::is_same_v<T, StringId>) value = StringId(text);
            else value = std::move(text);
        }
        else if constexpr (std::is_same_v<T, bool>) value = input.payload[0] != 0;
        else
        {
            static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= sizeof(input.payload));
            std::memcpy(&value, input.payload, sizeof(T));
        }
        return true;
    }

    //把具体返回值直接写入 ABI
    template<typename T>
    InteropStatus WriteResult(const T& value, Reflection::ValueKind kind, InteropValueAbi* output, std::string& stringStorage)
    {
        *output = {};
        output->kind = static_cast<uint32>(kind);
        if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, StringId>)
        {
            if constexpr (std::is_same_v<T, StringId>) stringStorage = value.GetPath();
            else stringStorage = value;
            const uint8* pointer = reinterpret_cast<const uint8*>(stringStorage.data());
            const int32 length = static_cast<int32>(stringStorage.size());
            std::memcpy(output->payload, &pointer, sizeof(pointer));
            std::memcpy(output->payload + 8, &length, sizeof(length));
        }
        else if constexpr (std::is_same_v<T, bool>) output->payload[0] = value ? uint8(1) : uint8(0);
        else
        {
            static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= sizeof(output->payload));
            std::memcpy(output->payload, &value, sizeof(T));
        }
        return InteropStatus::Ok;
    }
}
