#pragma once

#include "Runtime/EnsId.h"

#include <string>

//显式标记需要序列化的非 public 字段，由 OrbedenMetaGen 识别。
#define ORBEDEN_SERIALIZE_FIELD

class ScriptBehaviour;
namespace Reflection
{
    enum class FieldKind;
    class Value;
}

using ScriptCallback = void(*)(ScriptBehaviour*);
using ScriptUpdateCallback = void(*)(ScriptBehaviour*, float32);

//脚本组件的执行域；精确 ScriptBehaviour 实例属于托管域，原生派生类属于原生域。
enum class ScriptDomain : uint32
{
    Native,
    Managed,
};

//托管脚本字段在原生组件中的持久化快照。
struct ManagedScriptField
{
    std::string name;
    std::string typeName;
    Reflection::FieldKind kind;
    std::string value;
    bool inspectorVisible = true;
};

//单个 C++ 脚本类型在各生命周期阶段的非虚调用入口。
struct ScriptCallbackTable
{
    ScriptCallback start = nullptr;
    ScriptUpdateCallback update = nullptr;
    ScriptUpdateCallback fixedUpdate = nullptr;
    ScriptUpdateCallback lateUpdate = nullptr;
    ScriptCallback drawGUI = nullptr;
    ScriptCallback end = nullptr;
};

//注册类型直接声明的脚本回调。
void RegisterScriptCallbacks(Type* type, const ScriptCallbackTable& callbacks);

//注销即将卸载的脚本类型回调表。
void UnregisterScriptCallbacks(Type* type);

//解析类型及其脚本父类继承得到的完整回调表。
ScriptCallbackTable ResolveScriptCallbacks(Type* type);

//原生游戏脚本组件基类，生命周期由 ScriptSystem 的函数指针表调度。
class ScriptBehaviour : public Component
{
    OBJECT_TYPE_DECLARE_BASE(ScriptBehaviour)

private:
    friend class ScriptSystem;

    bool enabled = true;
    ScriptDomain domain = ScriptDomain::Native;
    std::string managedTypeName;
    List<ManagedScriptField> managedFields;
    bool scriptStarted = false;
    bool runtimeRegistered = false;

    //挂载到运行中 World 时加入 ScriptSystem。
    void OnAttach() final;

    //从 World 卸载前离开 ScriptSystem。
    void OnDetach() final;

    //所属 Ens 的活动状态变化时刷新阶段表。
    void OnWorldActiveChanged(bool worldActive) final;

    //读取 enabled 字段文本。
    static std::string GetEnabledField(Object* object);

    //写入 enabled 字段文本。
    static bool SetEnabledField(Object* object, const std::string& value);

    //读取 enabled 类型化字段。
    static Reflection::Value GetEnabledValue(Object* object);

    //通过业务 setter 写入 enabled 类型化字段。
    static bool SetEnabledValue(Object* object, const Reflection::Value& value);

public:
    //注册 ScriptBehaviour 自身持久化字段。
    static void RegisterReflection();

    //获取脚本启用状态。
    bool GetEnabled() const;

    //设置脚本启用状态并刷新阶段表。
    void SetEnabled(bool value);

    //获取由实际原生类型确定的脚本执行域。
    ScriptDomain GetDomain() const;

    //判断当前组件是否是 C# 脚本使用的精确原生宿主。
    bool IsManagedHost() const;

    //获取宿主绑定的 C# 完整类型名。
    const std::string& GetManagedTypeName() const;

    //配置宿主绑定的 C# 完整类型名。
    bool SetManagedTypeName(const std::string& value);

    //获取宿主持有的全部 C# 序列化字段。
    const List<ManagedScriptField>& GetManagedFields() const;

    //按字段名查找 C# 序列化字段。
    const ManagedScriptField* FindManagedField(const std::string& name) const;

    //新增或覆盖一个 C# 序列化字段。
    bool SetManagedField(const std::string& name,
        const std::string& typeName,
        Reflection::FieldKind kind,
        const std::string& value,
        bool inspectorVisible = true);

    //只更新一个已经存在的 C# 序列化字段值。
    bool SetManagedFieldValue(const std::string& name, const std::string& value);

    //把受支持的字段类型名转换为原生 Inspector 字段分类。
    static Reflection::FieldKind GetManagedFieldKind(const std::string& typeName);
};
