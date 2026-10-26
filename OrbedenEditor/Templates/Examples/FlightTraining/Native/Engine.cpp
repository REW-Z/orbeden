#include "Engine.h"
#include "Runtime/Object/Ens.h"
#include "Runtime/Object/RigidBody.h"
#include "Runtime/Object/Transform.h"
#include "InputManager/InputManager.h"
#include <algorithm>
#include <cmath>

OBJECT_TYPE_IMPLEMENT(Engine, Script)

namespace
{
    /// <summary>相加向量。</summary>
    vector3 Add(const vector3& a, const vector3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    /// <summary>缩放向量。</summary>
    vector3 Scale(const vector3& value, float32 scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    /// <summary>计算向量叉积。</summary>
    vector3 Cross(const vector3& a, const vector3& b)
    {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    /// <summary>用四元数旋转向量。</summary>
    vector3 Rotate(const quaternion& rotation, const vector3& value)
    {
        vector3 axis{ rotation.x, rotation.y, rotation.z };
        vector3 doubledCross = Scale(Cross(axis, value), 2.0f);
        return Add(value, Add(Scale(doubledCross, rotation.w), Cross(axis, doubledCross)));
    }

    /// <summary>计算向量点积。</summary>
    float32 Dot(const vector3& a, const vector3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    /// <summary>计算向量长度。</summary>
    float32 Length(const vector3& value)
    {
        return std::sqrt(Dot(value, value));
    }
}

/// <summary>每物理步调整油门并沿机头方向施加推力。</summary>
void Engine::OnFixedUpdate(float32 deltaTime)
{
    Transform* transform = GetEns() ? GetEns()->Transform() : nullptr;
    RigidBody* body = GetEns() ? GetEns()->GetComponent<RigidBody>() : nullptr;
    if (!transform || !body || !body->enabled) return;

    //按油门键调整油门
    float32 step = std::max(deltaTime, 0.0f);
    float32 throttleInput = (Input::Key(KeyEnum::LSHIFT) ? 1.0f : 0.0f) - (Input::Key(KeyEnum::LCTRL) ? 1.0f : 0.0f);
    throttle = std::clamp(throttle + throttleInput * throttleRate * step, 0.0f, 1.0f);

    //推力随前向空速衰减
    vector3 forward = Rotate(transform->GetLocalRotation(), { 0, 0, -1 });
    float32 forwardSpeed = Dot(body->linearVelocity, forward);
    float32 thrust = maxThrust * throttle * std::clamp(1.0f - std::max(forwardSpeed, 0.0f) / std::max(thrustFadeSpeed, 1.0f), 0.0f, 1.0f);
    thrustForce = Scale(forward, thrust);
    body->AddForce(thrustForce);
}

/// <summary>读取油门。</summary>
float32 Engine::GetThrottle() { return throttle; }

/// <summary>读取推力牛顿数。</summary>
float32 Engine::GetThrustNewtons() { return Length(thrustForce); }

/// <summary>读取推力向量。</summary>
vector3 Engine::GetThrustForce() { return thrustForce; }

/// <summary>清空油门和推力。</summary>
void Engine::ResetThrottle()
{
    throttle = 0.0f;
    thrustForce = {};
}
