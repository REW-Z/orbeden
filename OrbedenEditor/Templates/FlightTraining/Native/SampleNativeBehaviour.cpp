#include "SampleNativeBehaviour.h"

#include "Log/Log.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/Transform.h"

#include <cmath>
#include <span>

OBJECT_TYPE_IMPLEMENT(SampleNativeBehaviour, Script)

void SampleNativeBehaviour::OnStart()
{
    elapsedTime = 0.0f;
    interopTimer = 0.0f;
    interopResolved = false;
}

void SampleNativeBehaviour::OnUpdate(float32 deltaTime)
{
    //首次 Update 时两端均已初始化；Native OnStart 早于 Managed 初始化。
    if (!interopResolved)
    {
        interopResolved = true;
        managedSample = ScriptInterop::FindManagedComponent(GetEnsId(), "{{PROJECT_NAME}}.SampleBehaviour");
        constexpr Reflection::ValueKind signature[]{ Reflection::ValueKind::Float32 };
        if (managedSample.IsValid()) managedSample.ResolveMethod("ReceiveNativePing", signature, managedPingMethod);
    }
    elapsedTime += deltaTime * speed;
    Ens* ens = GetEns();
    Transform* transform = ens ? ens->Transform() : nullptr;
    if (!transform) return;

    vector3 position = transform->GetLocalPosition();
    position.y = 1.0f + std::sin(elapsedTime) * 0.2f;
    transform->SetLocalPosition(position);

    //高频场景持有 MemberHandle；这里每两秒演示一次，不再按字符串查找方法。
    interopTimer += deltaTime;
    if (interopTimer >= 2.0f && managedSample.IsValid())
    {
        interopTimer = 0.0f;
        Reflection::Value arguments[]{ Reflection::Value(elapsedTime) };
        Reflection::Value result;
        managedSample.Invoke(managedPingMethod, arguments, result);
    }
}

void SampleNativeBehaviour::ReceiveManagedPing(float32 value)
{
    (void)value;
    Log::Info("SampleNativeBehaviour received a pre-resolved C# call.");
}

void SampleNativeBehaviour::OnLateUpdate(float32 deltaTime)
{
    (void)deltaTime;
}

void SampleNativeBehaviour::OnDrawGUI()
{
}

void SampleNativeBehaviour::OnEnd()
{
}
