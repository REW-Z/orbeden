#pragma once

#include "Runtime/EnsId.h"

//显式标记需要序列化的非 public 字段，由 OrbedenMetaGen 识别。
#define ORBEDEN_SERIALIZE_FIELD

class ScriptBehaviour;

using ScriptCallback = void(*)(ScriptBehaviour*);
using ScriptUpdateCallback = void(*)(ScriptBehaviour*, float32);

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
    OBJECT_TYPE_DECLARE_ABSTRACT(ScriptBehaviour)

private:
    friend class ScriptSystem;

    bool enabled = true;
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

public:
    //注册 ScriptBehaviour 自身持久化字段。
    static void RegisterReflection();

    //获取脚本启用状态。
    bool GetEnabled() const;

    //设置脚本启用状态并刷新阶段表。
    void SetEnabled(bool value);
};
