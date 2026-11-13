#include "Engine.h"
#include "Runtime/Object/Ens.h"
#include "Runtime/Object/RigidBody.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/World.h"
#include "InputManager/InputManager.h"
#include <algorithm>
#include <cmath>

OBJECT_TYPE_IMPLEMENT(Engine, Script)

namespace
{
    constexpr float32 AirDensity = 1.225f;

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
    if (!transform || !body || !body->enabled)
    {
        return;
    }

    //按油门键调整油门
    float32 step = std::max(deltaTime, 0.0f);
    float32 throttleInput = (Input::Key(KeyEnum::LSHIFT) ? 1.0f : 0.0f) - (Input::Key(KeyEnum::LCTRL) ? 1.0f : 0.0f);
    SetThrottle(throttle + throttleInput * throttleRate * step);

    EvaluateForces();
    body->AddForceAtPosition(thrustForce, propellerPosition);
    body->AddForceAtPosition(windmillDragForce, propellerPosition);
}

//刷新螺旋桨受力与作用点
void Engine::EvaluateForces()
{
    thrustForce = {};
    windmillDragForce = {};
    propellerPosition = {};
    World* world = GetWorld();
    Transform* transform = GetEns() ? GetEns()->Transform() : nullptr;
    RigidBody* body = GetEns() ? GetEns()->GetComponent<RigidBody>() : nullptr;
    Ens* propellerEns = world ? world->GetEns(propeller) : nullptr;
    if (!transform || !body || !body->enabled || !propellerEns)
    {
        return;
    }

    //同步作用点与局部速度
    propellerPosition = propellerEns->Transform()->GetWorldPosition();
    vector3 offset = Add(propellerPosition, Scale(transform->GetWorldPosition(), -1.0f));
    vector3 velocity = Add(body->linearVelocity, Cross(body->angularVelocity, offset));

    //推力随前向空速衰减
    vector3 forward = Rotate(transform->GetWorldRotation(), { 0, 0, -1 });
    float32 forwardSpeed = Dot(velocity, forward);
    float32 thrust = maxThrust * throttle * std::clamp(1.0f - std::max(forwardSpeed, 0.0f) / std::max(thrustFadeSpeed, 1.0f), 0.0f, 1.0f);
    thrustForce = Scale(forward, thrust);

    //风车状态螺旋桨沿桨轴产生阻力
    float32 windmillDrag = 0.5f * AirDensity * forwardSpeed * forwardSpeed
        * std::max(windmillDragArea, 0.0f) * (1.0f - throttle);
    windmillDragForce = std::abs(forwardSpeed) > 0.01f
        ? Scale(forward, -std::copysign(windmillDrag, forwardSpeed)) : vector3();
}

/// <summary>读取油门。</summary>
float32 Engine::GetThrottle() { return throttle; }

//设置油门位置
void Engine::SetThrottle(float32 value) { throttle = std::clamp(value, 0.0f, 1.0f); }

/// <summary>读取推力牛顿数。</summary>
float32 Engine::GetThrustNewtons() { return Length(thrustForce); }

/// <summary>读取推力向量。</summary>
vector3 Engine::GetThrustForce() { return thrustForce; }

/// <summary>读取风车阻力牛顿数。</summary>
float32 Engine::GetWindmillDragNewtons() { return Length(windmillDragForce); }

/// <summary>读取风车阻力向量。</summary>
vector3 Engine::GetWindmillDragForce() { return windmillDragForce; }

//读取螺旋桨世界坐标
vector3 Engine::GetPropellerPosition() { return propellerPosition; }

/// <summary>清空油门和推力。</summary>
void Engine::ResetThrottle()
{
    throttle = 0.0f;
    thrustForce = {};
    windmillDragForce = {};
    propellerPosition = {};
}
