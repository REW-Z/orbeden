#pragma once

#include "Rendering/RenderTypes.h"

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

//着色器参数类型
enum class ShaderParameterType : uint8
{
    Matrix4,
    Vector3,
    Color,
    Int,
    Float,
};

//单个着色器参数的类型与值
struct ShaderParameterValue
{
    const char* name = nullptr;
    uint32 index = 0;
    ShaderParameterType type = ShaderParameterType::Float;
    std::array<uint8, sizeof(matrix4x4)> bytes = {};
    uint64 revision = 0;
    bool changed = false;

    //读取指定类型的参数值
    template<typename T> T Read() const
    {
        T value;
        static_assert(sizeof(T) <= sizeof(bytes));
        std::memcpy(&value, bytes.data(), sizeof(T));
        return value;
    }
};

//按名称保存一组着色器输入，由后端在绘制时提交
class ShaderParameters
{
private:
    struct NameHash
    {
        using is_transparent = void;

        //计算参数名称哈希
        usize operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
    };

    std::unordered_map<std::string, ShaderParameterValue, NameHash, std::equal_to<>> values;
    List<ShaderParameterValue*> changes;
    uint64 revision = 0;

    //更新参数类型与数据
    void Write(const char* name, ShaderParameterType type, const void* data, usize size);

public:
    ShaderParameters() = default;
    ShaderParameters(const ShaderParameters&) = delete;
    ShaderParameters& operator=(const ShaderParameters&) = delete;
    ShaderParameters(ShaderParameters&&) = default;
    ShaderParameters& operator=(ShaderParameters&&) = default;

    //注册参数名称并获取稳定的缓存项
    ShaderParameterValue& GetOrCreate(const char* name);
    //写入矩阵参数
    void SetMatrix4(const char* name, const matrix4x4& value) { Write(name, ShaderParameterType::Matrix4, &value, sizeof(value)); }
    //写入向量参数
    void SetVector3(const char* name, const vector3& value) { Write(name, ShaderParameterType::Vector3, &value, sizeof(value)); }
    //写入四分量参数
    void SetColor(const char* name, const color& value) { Write(name, ShaderParameterType::Color, &value, sizeof(value)); }
    //写入整数参数
    void SetInt(const char* name, int32 value) { Write(name, ShaderParameterType::Int, &value, sizeof(value)); }
    //写入浮点参数
    void SetFloat(const char* name, float32 value) { Write(name, ShaderParameterType::Float, &value, sizeof(value)); }
    //查询参数值
    const ShaderParameterValue* Find(std::string_view name) const;
    //读取尚未提交的参数变化
    const auto& GetChanges() const { return changes; }
    //清除已提交的参数变化
    void ClearChanges();
};
