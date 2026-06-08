#include <iomanip>
#include <sstream>
#include <unordered_map>

#include "Runtime/Reflection.h"
#include "Runtime/EnsId.h"

namespace
{
    //获取全局反射注册表
    std::unordered_map<TypeId, Reflection::TypeInfo>& GetReflectionRegistry()
    {
        static std::unordered_map<TypeId, Reflection::TypeInfo> registry;
        return registry;
    }

    //获取或创建指定类型的反射元数据
    Reflection::TypeInfo& EnsureTypeInfo(Type* type)
    {
        Reflection::TypeInfo& info = GetReflectionRegistry()[type->GetId()];
        info.type = type;
        return info;
    }

    //解析基础数值类型
    template<typename T>
    bool ParseNumber(const std::string& value, T& target)
    {
        std::istringstream stream(value);
        T parsed{};
        stream >> parsed;
        if (stream.fail()) return false;

        target = parsed;
        return true;
    }

    //按稳定精度格式化 float32
    std::string FormatFloat(float32 value)
    {
        std::ostringstream stream;
        stream << std::setprecision(9) << value;
        return stream.str();
    }
}

namespace Reflection
{
    //创建 bool 反射值
    Value::Value(bool value)
        : kind(ValueKind::Bool), data(value)
    {
    }

    //创建 int32 反射值
    Value::Value(int32 value)
        : kind(ValueKind::Int32), data(value)
    {
    }

    //创建 uint32 反射值
    Value::Value(uint32 value)
        : kind(ValueKind::UInt32), data(value)
    {
    }

    //创建 uint64 反射值
    Value::Value(uint64 value)
        : kind(ValueKind::UInt64), data(value)
    {
    }

    //创建 float32 反射值
    Value::Value(float32 value)
        : kind(ValueKind::Float32), data(value)
    {
    }

    //创建字符串反射值
    Value::Value(const std::string& value)
        : kind(ValueKind::String), data(value)
    {
    }

    //创建 C 字符串反射值
    Value::Value(const char* value)
        : Value(std::string(value ? value : ""))
    {
    }

    //创建稳定 ID 反射值
    Value::Value(const StringId& value)
        : kind(ValueKind::StringId), data(value)
    {
    }

    //创建 vector3 反射值
    Value::Value(const vector3& value)
        : kind(ValueKind::Vector3), data(value)
    {
    }

    //创建 color4 反射值
    Value::Value(const color4& value)
        : kind(ValueKind::Color4), data(value)
    {
    }

    //创建 quaternion 反射值
    Value::Value(const quaternion& value)
        : kind(ValueKind::Quaternion), data(value)
    {
    }

    //创建 EnsId 反射值
    Value::Value(const EnsId& value)
        : kind(ValueKind::EnsId), data(value)
    {
    }

    //创建 Object 指针反射值
    Value::Value(Object* value)
        : kind(ValueKind::Object), data(value)
    {
    }

    //获取当前反射值类型
    ValueKind Value::GetKind() const
    {
        return kind;
    }

    //判断是否为空值
    bool Value::IsEmpty() const
    {
        return kind == ValueKind::Empty;
    }

    //转换为 XML/调试用文本
    std::string Value::ToString() const
    {
        //按当前类型分发到已有 XML 转换函数
        switch (kind)
        {
        case ValueKind::Bool: return ToXmlValue(std::get<bool>(data));
        case ValueKind::Int32: return ToXmlValue(std::get<int32>(data));
        case ValueKind::UInt32: return ToXmlValue(std::get<uint32>(data));
        case ValueKind::UInt64: return ToXmlValue(std::get<uint64>(data));
        case ValueKind::Float32: return ToXmlValue(std::get<float32>(data));
        case ValueKind::String: return ToXmlValue(std::get<std::string>(data));
        case ValueKind::StringId: return ToXmlValue(std::get<StringId>(data));
        case ValueKind::Vector3: return ToXmlValue(std::get<vector3>(data));
        case ValueKind::Color4: return ToXmlValue(std::get<color4>(data));
        case ValueKind::Quaternion: return ToXmlValue(std::get<quaternion>(data));
        case ValueKind::EnsId: return ToXmlValue(std::get<EnsId>(data));
        case ValueKind::Object:
        {
            Object* object = std::get<Object*>(data);
            return object ? object->GetInstanceId().GetPath() : std::string();
        }
        default: return std::string();
        }
    }

    //尝试读取 bool 值
    bool Value::TryGet(bool& value) const
    {
        if (kind != ValueKind::Bool) return false;
        value = std::get<bool>(data);
        return true;
    }

    //尝试读取 int32 值
    bool Value::TryGet(int32& value) const
    {
        if (kind != ValueKind::Int32) return false;
        value = std::get<int32>(data);
        return true;
    }

    //尝试读取 uint32 值
    bool Value::TryGet(uint32& value) const
    {
        if (kind != ValueKind::UInt32) return false;
        value = std::get<uint32>(data);
        return true;
    }

    //尝试读取 uint64 值
    bool Value::TryGet(uint64& value) const
    {
        if (kind != ValueKind::UInt64) return false;
        value = std::get<uint64>(data);
        return true;
    }

    //尝试读取 float32 值
    bool Value::TryGet(float32& value) const
    {
        if (kind != ValueKind::Float32) return false;
        value = std::get<float32>(data);
        return true;
    }

    //尝试读取字符串值
    bool Value::TryGet(std::string& value) const
    {
        if (kind != ValueKind::String) return false;
        value = std::get<std::string>(data);
        return true;
    }

    //尝试读取稳定 ID 值
    bool Value::TryGet(StringId& value) const
    {
        if (kind != ValueKind::StringId) return false;
        value = std::get<StringId>(data);
        return true;
    }

    //尝试读取 vector3 值
    bool Value::TryGet(vector3& value) const
    {
        if (kind != ValueKind::Vector3) return false;
        value = std::get<vector3>(data);
        return true;
    }

    //尝试读取 color4 值
    bool Value::TryGet(color4& value) const
    {
        if (kind != ValueKind::Color4) return false;
        value = std::get<color4>(data);
        return true;
    }

    //尝试读取 quaternion 值
    bool Value::TryGet(quaternion& value) const
    {
        if (kind != ValueKind::Quaternion) return false;
        value = std::get<quaternion>(data);
        return true;
    }

    //尝试读取 EnsId 值
    bool Value::TryGet(EnsId& value) const
    {
        if (kind != ValueKind::EnsId) return false;
        value = std::get<EnsId>(data);
        return true;
    }

    //尝试读取 Object 指针值
    bool Value::TryGet(Object*& value) const
    {
        if (kind != ValueKind::Object) return false;
        value = std::get<Object*>(data);
        return true;
    }

    //按字段类型从文本构造反射值
    bool Value::FromString(FieldKind kind, const std::string& text, Value& value)
    {
        //字段序列化类型和反射值类型一一映射
        switch (kind)
        {
        case FieldKind::Bool:
        {
            bool parsed = false;
            if (!SetFromXmlValue(parsed, text)) return false;
            value = Value(parsed);
            return true;
        }
        case FieldKind::Int32:
        {
            int32 parsed = 0;
            if (!SetFromXmlValue(parsed, text)) return false;
            value = Value(parsed);
            return true;
        }
        case FieldKind::UInt32:
        {
            uint32 parsed = 0;
            if (!SetFromXmlValue(parsed, text)) return false;
            value = Value(parsed);
            return true;
        }
        case FieldKind::UInt64:
        {
            uint64 parsed = 0;
            if (!SetFromXmlValue(parsed, text)) return false;
            value = Value(parsed);
            return true;
        }
        case FieldKind::Float32:
        {
            float32 parsed = 0.0f;
            if (!SetFromXmlValue(parsed, text)) return false;
            value = Value(parsed);
            return true;
        }
        case FieldKind::String:
        {
            value = Value(text);
            return true;
        }
        case FieldKind::StringId:
        {
            value = Value(StringId(text));
            return true;
        }
        case FieldKind::ObjectRef:
        {
            value = Value(StringId(text));
            return true;
        }
        case FieldKind::Vector3:
        {
            vector3 parsed;
            if (!SetFromXmlValue(parsed, text)) return false;
            value = Value(parsed);
            return true;
        }
        case FieldKind::Color4:
        {
            color4 parsed;
            if (!SetFromXmlValue(parsed, text)) return false;
            value = Value(parsed);
            return true;
        }
        case FieldKind::Quaternion:
        {
            quaternion parsed;
            if (!SetFromXmlValue(parsed, text)) return false;
            value = Value(parsed);
            return true;
        }
        case FieldKind::EnsId:
        {
            EnsId parsed;
            if (!SetFromXmlValue(parsed, text)) return false;
            value = Value(parsed);
            return true;
        }
        default:
            return false;
        }
    }

    //创建参数元数据
    ParameterInfo::ParameterInfo(const char* parameterName, const char* parameterTypeName, ValueKind parameterKind)
        : name(parameterName), typeName(parameterTypeName), kind(parameterKind)
    {
    }

    //创建字段元数据
    FieldInfo::FieldInfo(const char* fieldName, const char* fieldTypeName, FieldKind fieldKind, bool isPersistent, FieldGetter getValue, FieldSetter setValue, const char* refTypeName)
        : name(fieldName), typeName(fieldTypeName), objectRefTypeName(refTypeName), kind(fieldKind), persistent(isPersistent), getter(getValue), setter(setValue)
    {
    }

    //读取字段值
    Value FieldInfo::GetValue(Object* object) const
    {
        Value value;
        if (!object || !getter) return value;

        Value::FromString(kind, getter(object), value);
        return value;
    }

    //写入字段值
    bool FieldInfo::SetValue(Object* object, const Value& value) const
    {
        if (!object || !setter) return false;

        return setter(object, value.ToString());
    }

    //读取字段文本值
    std::string FieldInfo::GetValueAsString(Object* object) const
    {
        if (!object || !getter) return std::string();

        return getter(object);
    }

    //从文本写入字段值
    bool FieldInfo::SetValueFromString(Object* object, const std::string& value) const
    {
        if (!object || !setter) return false;

        return setter(object, value);
    }

    //创建方法元数据
    MethodInfo::MethodInfo(const char* methodName, const char* methodReturnTypeName, ValueKind methodReturnKind, const List<ParameterInfo>& methodParameters, MethodInvoker invokeMethod)
        : name(methodName), returnTypeName(methodReturnTypeName), returnKind(methodReturnKind), parameters(methodParameters), invoker(invokeMethod)
    {
    }

    //调用反射方法
    Value MethodInfo::Invoke(Object* object, const List<Value>& args, bool* success) const
    {
        //由生成代码负责实参类型检查和真实调用
        bool localSuccess = false;
        Value result;
        if (object && invoker)
        {
            result = invoker(object, args, localSuccess);
        }

        if (success)
        {
            *success = localSuccess;
        }

        return result;
    }

    //注册类型字段元数据
    void RegisterTypeFields(Type* type, const List<FieldInfo>& fields)
    {
        if (!type) return;

        EnsureTypeInfo(type).fields = fields;
    }

    //注册类型方法元数据
    void RegisterTypeMethods(Type* type, const List<MethodInfo>& methods)
    {
        if (!type) return;

        EnsureTypeInfo(type).methods = methods;
    }

    //查找类型元数据
    const TypeInfo* FindTypeInfo(Type* type)
    {
        if (!type) return nullptr;

        auto& registry = GetReflectionRegistry();
        auto it = registry.find(type->GetId());
        if (it == registry.end()) return nullptr;

        return &it->second;
    }

    //查找字段元数据
    const FieldInfo* FindField(Type* type, const std::string& name)
    {
        const TypeInfo* info = FindTypeInfo(type);
        if (!info) return nullptr;

        for (const FieldInfo& field : info->fields)
        {
            if (field.name && name == field.name)
            {
                return &field;
            }
        }

        return nullptr;
    }

    //查找方法元数据
    const MethodInfo* FindMethod(Type* type, const std::string& name)
    {
        const TypeInfo* info = FindTypeInfo(type);
        if (!info) return nullptr;

        for (const MethodInfo& method : info->methods)
        {
            if (method.name && name == method.name)
            {
                return &method;
            }
        }

        return nullptr;
    }

    //转换 bool 为 XML 文本
    std::string ToXmlValue(bool value)
    {
        return value ? "true" : "false";
    }

    //转换 int32 为 XML 文本
    std::string ToXmlValue(int32 value)
    {
        return std::to_string(value);
    }

    //转换 uint32 为 XML 文本
    std::string ToXmlValue(uint32 value)
    {
        return std::to_string(value);
    }

    //转换 uint64 为 XML 文本
    std::string ToXmlValue(uint64 value)
    {
        return std::to_string(value);
    }

    //转换 float32 为 XML 文本
    std::string ToXmlValue(float32 value)
    {
        return FormatFloat(value);
    }

    //转换字符串为 XML 文本
    std::string ToXmlValue(const std::string& value)
    {
        return value;
    }

    //转换稳定 ID 为 XML 文本
    std::string ToXmlValue(const StringId& value)
    {
        return value.GetPath();
    }

    //转换 vector3 为 XML 文本
    std::string ToXmlValue(const vector3& value)
    {
        return FormatFloat(value.x) + " " + FormatFloat(value.y) + " " + FormatFloat(value.z);
    }

    //转换 color4 为 XML 文本
    std::string ToXmlValue(const color4& value)
    {
        return FormatFloat(value.r) + " " + FormatFloat(value.g) + " " + FormatFloat(value.b) + " " + FormatFloat(value.a);
    }

    //转换 quaternion 为 XML 文本
    std::string ToXmlValue(const quaternion& value)
    {
        return FormatFloat(value.x) + " " + FormatFloat(value.y) + " " + FormatFloat(value.z) + " " + FormatFloat(value.w);
    }

    //转换 EnsId 为 XML 文本
    std::string ToXmlValue(const EnsId& value)
    {
        return std::to_string(value.id) + ":" + std::to_string(value.version);
    }

    //从 XML 文本读取 bool
    bool SetFromXmlValue(bool& target, const std::string& value)
    {
        if (value == "true" || value == "1")
        {
            target = true;
            return true;
        }

        if (value == "false" || value == "0")
        {
            target = false;
            return true;
        }

        return false;
    }

    //从 XML 文本读取 int32
    bool SetFromXmlValue(int32& target, const std::string& value)
    {
        return ParseNumber(value, target);
    }

    //从 XML 文本读取 uint32
    bool SetFromXmlValue(uint32& target, const std::string& value)
    {
        return ParseNumber(value, target);
    }

    //从 XML 文本读取 uint64
    bool SetFromXmlValue(uint64& target, const std::string& value)
    {
        return ParseNumber(value, target);
    }

    //从 XML 文本读取 float32
    bool SetFromXmlValue(float32& target, const std::string& value)
    {
        return ParseNumber(value, target);
    }

    //从 XML 文本读取字符串
    bool SetFromXmlValue(std::string& target, const std::string& value)
    {
        target = value;
        return true;
    }

    //从 XML 文本读取稳定 ID
    bool SetFromXmlValue(StringId& target, const std::string& value)
    {
        target = StringId(value);
        return true;
    }

    //从 XML 文本读取 vector3
    bool SetFromXmlValue(vector3& target, const std::string& value)
    {
        std::istringstream stream(value);
        vector3 parsed;
        stream >> parsed.x >> parsed.y >> parsed.z;
        if (stream.fail()) return false;

        target = parsed;
        return true;
    }

    //从 XML 文本读取 color4
    bool SetFromXmlValue(color4& target, const std::string& value)
    {
        std::istringstream stream(value);
        color4 parsed;
        stream >> parsed.r >> parsed.g >> parsed.b >> parsed.a;
        if (stream.fail()) return false;

        target = parsed;
        return true;
    }

    //从 XML 文本读取 quaternion
    bool SetFromXmlValue(quaternion& target, const std::string& value)
    {
        std::istringstream stream(value);
        quaternion parsed;
        stream >> parsed.x >> parsed.y >> parsed.z >> parsed.w;
        if (stream.fail()) return false;

        target = parsed;
        return true;
    }

    //从 XML 文本读取 EnsId
    bool SetFromXmlValue(EnsId& target, const std::string& value)
    {
        std::size_t separator = value.find(':');
        if (separator == std::string::npos) return false;

        EnsId parsed;
        if (!SetFromXmlValue(parsed.id, value.substr(0, separator))) return false;
        if (!SetFromXmlValue(parsed.version, value.substr(separator + 1))) return false;

        target = parsed;
        return true;
    }
}
