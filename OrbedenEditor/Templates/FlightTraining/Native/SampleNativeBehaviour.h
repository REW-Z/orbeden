#pragma once

#include "Scripting/ScriptBehaviour.h"
#include "Scripting/ScriptInterop.h"

//无需 C# binding 的高性能原生脚本；同时演示预解析后调用 C# 方法。
class SampleNativeBehaviour final : public ScriptBehaviour
{
    OBJECT_TYPE_DECLARE(SampleNativeBehaviour)

public:
    float32 speed = 2.0f;

    //public 方法会由 MetaGen 加入互操作方法表，C# 可以预解析后调用。
    void ReceiveManagedPing(float32 value);

private:
    float32 elapsedTime = 0.0f;
    float32 interopTimer = 0.0f;
    bool interopResolved = false;
    ScriptInterop::ComponentProxy managedSample;
    ScriptInterop::MemberHandle managedPingMethod;

protected:
    void OnStart();
    void OnUpdate(float32 deltaTime);
    void OnLateUpdate(float32 deltaTime);
    void OnDrawGUI();
    void OnEnd();
};
