#include "Rendering/ShaderParameters.h"

namespace
{
    //统一数组首元素的参数名称
    std::string_view GetParameterName(const char* name)
    {
        std::string_view result(name);
        if (result.ends_with("[0]")) result.remove_suffix(3);
        return result;
    }
}

//注册参数名称并获取稳定的缓存项
ShaderParameterValue& ShaderParameters::GetOrCreate(const char* name)
{
    std::string_view key = GetParameterName(name);
    auto found = values.find(key);
    if (found == values.end())
    {
        uint32 index = static_cast<uint32>(values.size());
        found = values.try_emplace(std::string(key)).first;
        found->second.name = found->first.c_str();
        found->second.index = index;
    }
    return found->second;
}

//更新参数类型与数据
void ShaderParameters::Write(const char* name, ShaderParameterType type, const void* data, usize size)
{
    ShaderParameterValue& value = GetOrCreate(name);
    if (value.revision != 0 && value.type == type && std::memcmp(value.bytes.data(), data, size) == 0) return;
    value.type = type;
    std::memcpy(value.bytes.data(), data, size);
    value.revision = ++revision;
    if (!value.changed)
    {
        value.changed = true;
        changes.push_back(&value);
    }
}

//清除已提交的参数变化
void ShaderParameters::ClearChanges()
{
    for (ShaderParameterValue* value : changes) value->changed = false;
    changes.clear();
}

//查询参数值
const ShaderParameterValue* ShaderParameters::Find(std::string_view name) const
{
    if (name.ends_with("[0]")) name.remove_suffix(3);
    auto found = values.find(name);
    return found == values.end() ? nullptr : &found->second;
}
