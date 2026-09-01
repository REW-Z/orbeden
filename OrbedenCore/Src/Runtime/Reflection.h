#pragma once

#include <string>
#include <type_traits>
#include <variant>

#include "Runtime/EngineTypes.h"
#include "Runtime/Object/Object.h"
#include "Runtime/EnsId.h"

namespace Reflection
{
    //反射值的运行时类型
    enum class ValueKind
    {
        Empty,
        Bool,
        Int32,
        UInt32,
        UInt64,
        Float32,
        String,
        StringId,
        Vector3,
        Color,
        Quaternion,
        EnsId,
        Object,
    };

    //可序列化字段的类型分类
    enum class FieldKind
    {
        Unsupported,
        Bool,
        Int32,
        UInt32,
        UInt64,
        Float32,
        String,
        StringId,
        ObjectRef,
        Vector3,
        Color,
        Quaternion,
        EnsId,
    };

    //反射调用使用的轻量值容器
    class Value
    {
    private:
        ValueKind kind = ValueKind::Empty;
        std::variant<std::monostate, bool, int32, uint32, uint64, float32, std::string, StringId, vector3, color, quaternion, EnsId, Object*> data;

    public:
        Value() = default;

        //创建 bool 反射值
        Value(bool value);

        //创建 int32 反射值
        Value(int32 value);

        //创建 uint32 反射值
        Value(uint32 value);

        //创建 uint64 反射值
        Value(uint64 value);

        //创建 float32 反射值
        Value(float32 value);

        //创建字符串反射值
        Value(const std::string& value);

        //创建 C 字符串反射值
        Value(const char* value);

        //创建稳定 ID 反射值
        Value(const StringId& value);

        //创建 vector3 反射值
        Value(const vector3& value);

        //创建 color 反射值
        Value(const color& value);

        //创建 quaternion 反射值
        Value(const quaternion& value);

        //创建 EnsId 反射值
        Value(const EnsId& value);

        //创建 Object 指针反射值
        Value(Object* value);

        //获取当前反射值类型
        ValueKind GetKind() const;

        //判断是否为空值
        bool IsEmpty() const;

        //转换为 XML/调试用文本
        std::string ToString() const;

        //尝试读取 bool 值
        bool TryGet(bool& value) const;

        //尝试读取 int32 值
        bool TryGet(int32& value) const;

        //尝试读取 uint32 值
        bool TryGet(uint32& value) const;

        //尝试读取 uint64 值
        bool TryGet(uint64& value) const;

        //尝试读取 float32 值
        bool TryGet(float32& value) const;

        //尝试读取字符串值
        bool TryGet(std::string& value) const;

        //尝试读取稳定 ID 值
        bool TryGet(StringId& value) const;

        //尝试读取 vector3 值
        bool TryGet(vector3& value) const;

        //尝试读取 color 值
        bool TryGet(color& value) const;

        //尝试读取 quaternion 值
        bool TryGet(quaternion& value) const;

        //尝试读取 EnsId 值
        bool TryGet(EnsId& value) const;

        //尝试读取 Object 指针值
        bool TryGet(Object*& value) const;

        //按字段类型从文本构造反射值
        static bool FromString(FieldKind kind, const std::string& text, Value& value);
    };

    typedef std::string (*FieldGetter)(Object* object);
    typedef bool (*FieldSetter)(Object* object, const std::string& value);

    //反射方法参数元数据
    struct ParameterInfo
    {
    public:
        const char* name = nullptr;
        const char* typeName = nullptr;
        ValueKind kind = ValueKind::Empty;

        ParameterInfo() = default;

        //创建参数元数据
        ParameterInfo(const char* parameterName, const char* parameterTypeName, ValueKind parameterKind);
    };

    typedef Value (*MethodInvoker)(Object* object, const List<Value>& args, bool& success);

    //反射字段元数据和读写入口
    struct FieldInfo
    {
    public:
        const char* name = nullptr;
        const char* typeName = nullptr;
        const char* objectRefTypeName = nullptr;
        FieldKind kind = FieldKind::Unsupported;
        bool persistent = false;
        FieldGetter getter = nullptr;
        FieldSetter setter = nullptr;

        FieldInfo() = default;

        //创建字段元数据
        FieldInfo(const char* fieldName, const char* fieldTypeName, FieldKind fieldKind, bool isPersistent, FieldGetter getValue, FieldSetter setValue, const char* refTypeName = nullptr);

        //读取字段值
        Value GetValue(Object* object) const;

        //写入字段值
        bool SetValue(Object* object, const Value& value) const;

        //读取字段文本值
        std::string GetValueAsString(Object* object) const;

        //从文本写入字段值
        bool SetValueFromString(Object* object, const std::string& value) const;
    };

    //反射方法元数据和调用入口
    struct MethodInfo
    {
    public:
        const char* name = nullptr;
        const char* returnTypeName = nullptr;
        ValueKind returnKind = ValueKind::Empty;
        List<ParameterInfo> parameters;
        MethodInvoker invoker = nullptr;

        MethodInfo() = default;

        //创建方法元数据
        MethodInfo(const char* methodName, const char* methodReturnTypeName, ValueKind methodReturnKind, const List<ParameterInfo>& methodParameters, MethodInvoker invokeMethod);

        //调用反射方法
        Value Invoke(Object* object, const List<Value>& args, bool* success = nullptr) const;
    };

    //类型的反射元数据集合
    struct TypeInfo
    {
    public:
        Type* type = nullptr;
        List<FieldInfo> fields;
        List<MethodInfo> methods;
    };

    //注册类型字段元数据
    void RegisterTypeFields(Type* type, const List<FieldInfo>& fields);

    //注册类型方法元数据
    void RegisterTypeMethods(Type* type, const List<MethodInfo>& methods);

    //注销一个动态模块类型的全部反射元数据。
    void UnregisterType(Type* type);

    //查找类型元数据
    const TypeInfo* FindTypeInfo(Type* type);

    //查找字段元数据
    const FieldInfo* FindField(Type* type, const std::string& name);

    //按基类到派生类顺序收集可见字段。
    void CollectFields(Type* type, List<const FieldInfo*>& output);

    //查找方法元数据
    const MethodInfo* FindMethod(Type* type, const std::string& name);

    //注册由代码生成器生成的反射元数据
    void RegisterGeneratedReflection();

    //转换 bool 为 XML 文本
    std::string ToXmlValue(bool value);

    //转换 int32 为 XML 文本
    std::string ToXmlValue(int32 value);

    //转换 uint32 为 XML 文本
    std::string ToXmlValue(uint32 value);

    //转换 uint64 为 XML 文本
    std::string ToXmlValue(uint64 value);

    //转换 float32 为 XML 文本
    std::string ToXmlValue(float32 value);

    //转换字符串为 XML 文本
    std::string ToXmlValue(const std::string& value);

    //转换稳定 ID 为 XML 文本
    std::string ToXmlValue(const StringId& value);

    //转换对象引用为 XML 文本，只保存稳定ID
    template<typename T>
    std::string ToXmlValue(const Ref<T>& value)
    {
        static_assert(std::is_base_of_v<Object, T>);
        return ToXmlValue(value.GetInstanceId());
    }

    //转换枚举为 XML 文本
    template<typename T>
    std::enable_if_t<std::is_enum_v<T>, std::string> ToXmlValue(T value)
    {
        return ToXmlValue(static_cast<uint32>(value));
    }

    //转换 vector3 为 XML 文本
    std::string ToXmlValue(const vector3& value);

    //转换 color 为 XML 文本
    std::string ToXmlValue(const color& value);

    //转换 quaternion 为 XML 文本
    std::string ToXmlValue(const quaternion& value);

    //转换 EnsId 为 XML 文本
    std::string ToXmlValue(const EnsId& value);

    //从 XML 文本读取 bool
    bool SetFromXmlValue(bool& target, const std::string& value);

    //从 XML 文本读取 int32
    bool SetFromXmlValue(int32& target, const std::string& value);

    //从 XML 文本读取 uint32
    bool SetFromXmlValue(uint32& target, const std::string& value);

    //从 XML 文本读取 uint64
    bool SetFromXmlValue(uint64& target, const std::string& value);

    //从 XML 文本读取 float32
    bool SetFromXmlValue(float32& target, const std::string& value);

    //从 XML 文本读取字符串
    bool SetFromXmlValue(std::string& target, const std::string& value);

    //从 XML 文本读取稳定 ID
    bool SetFromXmlValue(StringId& target, const std::string& value);

    //从 XML 文本读取对象引用，只写入稳定ID
    template<typename T>
    bool SetFromXmlValue(Ref<T>& target, const std::string& value)
    {
        static_assert(std::is_base_of_v<Object, T>);
        target.SetInstanceId(StringId(value));
        return true;
    }

    //从 XML 文本读取枚举
    template<typename T>
    std::enable_if_t<std::is_enum_v<T>, bool> SetFromXmlValue(T& target, const std::string& value)
    {
        uint32 parsed = 0;
        if (!SetFromXmlValue(parsed, value)) return false;

        target = static_cast<T>(parsed);
        return true;
    }

    //从 XML 文本读取 vector3
    bool SetFromXmlValue(vector3& target, const std::string& value);

    //从 XML 文本读取 color
    bool SetFromXmlValue(color& target, const std::string& value);

    //从 XML 文本读取 quaternion
    bool SetFromXmlValue(quaternion& target, const std::string& value);

    //从 XML 文本读取 EnsId
    bool SetFromXmlValue(EnsId& target, const std::string& value);
}
