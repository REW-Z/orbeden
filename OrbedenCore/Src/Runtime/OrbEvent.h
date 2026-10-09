#pragma once

#include <functional>
#include <span>
#include "Runtime/Reflection.h"
#include "Scripting/ScriptInterop.h"

//持久化监听器所在的语言域
enum class OrbEventDomain : uint32 { Native = 1, Managed = 2 };

//持久化调用配置，不保存运行时指针
struct OrbEventCall
{
    std::string targetKey;
    OrbEventDomain domain = OrbEventDomain::Native;
    std::string targetType;
    int32 occurrence = 0;
    std::string method;
    bool enabled = true;
    bool useArguments = true;
};

//可序列化的同步多播事件，使用与 C# 相同的签名和持久化格式
class OrbEvent
{
private:
    List<Reflection::ValueKind> parameterKinds;
    List<OrbEventCall> persistentCalls;
    List<std::pair<uint64, std::function<void(std::span<const Reflection::Value>)>>> listeners;
    uint64 nextListenerId = 1;
    uint32 invocationDepth = 0;

public:
    //创建具有精确参数签名的事件，默认无参
    explicit OrbEvent(List<Reflection::ValueKind> signature = {});
    //复制持久化配置，不复制运行时订阅
    OrbEvent(const OrbEvent& value);
    //写入同签名的持久化配置，保留运行时订阅
    OrbEvent& operator=(const OrbEvent& value);
    //读取参数签名
    const List<Reflection::ValueKind>& GetParameterKinds() const;
    //编辑持久化调用配置
    List<OrbEventCall>& GetPersistentCalls();
    //读取持久化调用配置
    const List<OrbEventCall>& GetPersistentCalls() const;
    //添加运行时监听器，返回用于移除的编号
    uint64 Subscribe(std::function<void(std::span<const Reflection::Value>)> listener);
    //移除运行时监听器
    bool Unsubscribe(uint64 listenerId);
    //清空运行时监听器，保留持久化配置
    void ClearSubscriptions();
    //同步派发事件，隔离监听器异常并限制递归深度
    bool Dispatch(std::span<const Reflection::Value> arguments = {});
    //序列化签名和持久化调用配置
    std::string Serialize() const;
    //完整验证后替换持久化配置，保留运行时订阅
    bool Deserialize(const std::string& text);
    //读取文本中的参数签名
    static bool ReadSignature(const std::string& text, List<Reflection::ValueKind>& signature);
    //重映射事件内的场景目标引用
    static bool RemapTargets(std::string& text, const std::function<std::string(const std::string&)>& remap);
};
