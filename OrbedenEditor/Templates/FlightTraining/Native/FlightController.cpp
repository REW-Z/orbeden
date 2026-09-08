#include "FlightController.h"

#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsTypes.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/WheelColliderComponent.h"
#include "Platform/InputManager.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/TransformComponent.h"

#include <algorithm>
#include <array>
#include <cmath>

OBJECT_TYPE_IMPLEMENT(FlightController, ScriptBehaviour)

namespace
{
    constexpr float32 Pi = 3.14159265358979323846f;
    constexpr float32 AirDensity = 1.225f;
    constexpr float32 ClMax = 1.6f;
    //舵面力矩换算系数，权威度字段乘以该值得到 N·m 量级力矩。
    constexpr float32 TorqueScale = 350.0f;
    constexpr std::array<vector3, 4> Checkpoints =
    {
        vector3{ 0.0f, 18.0f, -130.0f },
        vector3{ 100.0f, 35.0f, -260.0f },
        vector3{ -100.0f, 50.0f, -420.0f },
        vector3{ 0.0f, 30.0f, -560.0f },
    };

    vector3 Add(const vector3& a, const vector3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    vector3 Scale(const vector3& value, float32 scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    vector3 Cross(const vector3& a, const vector3& b)
    {
        return
        {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }

    vector3 Rotate(const quaternion& rotation, const vector3& value)
    {
        vector3 axis{ rotation.x, rotation.y, rotation.z };
        vector3 doubledCross = Scale(Cross(axis, value), 2.0f);
        return Add(value, Add(Scale(doubledCross, rotation.w), Cross(axis, doubledCross)));
    }

    float32 Dot(const vector3& a, const vector3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    float32 Length(const vector3& value)
    {
        return std::sqrt(Dot(value, value));
    }

    float32 DistanceSquared(const vector3& a, const vector3& b)
    {
        float32 x = a.x - b.x;
        float32 y = a.y - b.y;
        float32 z = a.z - b.z;
        return x * x + y * y + z * z;
    }
}

void FlightController::OnStart()
{
    TransformComponent* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform) return;

    spawnPosition = transform->GetLocalPosition();
    spawnRotation = transform->GetLocalRotation();
    ResetAircraft();
}

void FlightController::OnFixedUpdate(float32 deltaTime)
{
    TransformComponent* transform = GetEns() ? GetEns()->Transform() : nullptr;
    RigidBodyComponent* body = GetEns() ? GetEns()->GetComponent<RigidBodyComponent>() : nullptr;
    if (!transform || !body) return;

    float32 step = std::clamp(deltaTime, 0.0f, 0.05f);
    if (Input::KeyDown(KeyEnum::R))
    {
        ResetFlight();
        return;
    }

    float32 throttleInput = (Input::Key(KeyEnum::LSHIFT) ? 1.0f : 0.0f)
        - (Input::Key(KeyEnum::LCTRL) ? 1.0f : 0.0f);
    throttle = std::clamp(throttle + throttleInput * throttleRate * step, 0.0f, 1.0f);
    lapTime += step;

    float32 pitchInput = (Input::Key(KeyEnum::S) ? 1.0f : 0.0f)
        - (Input::Key(KeyEnum::W) ? 1.0f : 0.0f);
    //前轴为 -Z，正滚转力矩压低右翼：D 右倾，A 左倾。
    float32 rollInput = (Input::Key(KeyEnum::D) ? 1.0f : 0.0f)
        - (Input::Key(KeyEnum::A) ? 1.0f : 0.0f);
    float32 yawInput = (Input::Key(KeyEnum::Q) ? 1.0f : 0.0f)
        - (Input::Key(KeyEnum::E) ? 1.0f : 0.0f);

    vector3 velocity = body->linearVelocity;
    float32 speed = Length(velocity);
    quaternion rotation = transform->GetLocalRotation();
    quaternion inverse{ -rotation.x, -rotation.y, -rotation.z, rotation.w };
    vector3 forward = Rotate(rotation, { 0.0f, 0.0f, -1.0f });
    vector3 up = Rotate(rotation, { 0.0f, 1.0f, 0.0f });
    vector3 right = Rotate(rotation, { 1.0f, 0.0f, 0.0f });

    //机体系速度与迎角（抬头为正）。
    vector3 velocityBody = Rotate(inverse, velocity);
    float32 forwardSpeed = -velocityBody.z;
    float32 alphaDegrees = forwardSpeed > 0.5f
        ? std::atan2(-velocityBody.y, forwardSpeed) * 180.0f / Pi
        : 0.0f;

    //升力曲线采样：线性段 → stallAngle 失速 → 线性衰减到失速后保留值。
    float32 cl = std::clamp(liftSlope * (alphaDegrees - zeroLiftAngle), 0.0f, ClMax);
    if (alphaDegrees > stallAngle)
    {
        float32 falloff = std::clamp((alphaDegrees - stallAngle) / 10.0f, 0.0f, 1.0f);
        cl = ClMax * (1.0f - falloff) + ClMax * postStallCl * falloff;
    }

    float32 dynamicPressure = 0.5f * AirDensity * speed * speed;
    float32 lift = dynamicPressure * wingArea * cl * liftMultiplier;
    float32 drag = dynamicPressure * wingArea * dragCoefficient * (1.0f + 4.0f * cl * cl);
    //螺旋桨推力随速度衰减：低速推重比 > 1，高速收敛到巡航速度。
    float32 thrust = maxThrust * throttle * std::clamp(1.0f - speed / 60.0f, 0.0f, 1.0f);

    //推力沿机头、升力沿机体上方（作用于翼点）、阻力沿速度反方向。
    body->AddForce(Scale(forward, thrust));
    body->AddForceAtPosition(Scale(up, lift), Add(transform->GetLocalPosition(), Rotate(rotation, { 0.0f, 0.05f, 0.0f })));
    if (speed > 0.1f) body->AddForce(Scale(velocity, -drag / speed));

    //舵面力矩：W/S 俯仰（绕右轴）、A/D 滚转（绕前轴）、Q/E 偏航（绕上轴）。
    float32 airFactor = std::clamp(speed / 12.0f, 0.0f, 1.0f);
    body->AddTorque(Scale(right, pitchInput * elevatorAuthority * TorqueScale * airFactor));
    body->AddTorque(Scale(forward, rollInput * aileronAuthority * TorqueScale * airFactor));
    body->AddTorque(Scale(up, yawInput * rudderAuthority * TorqueScale * airFactor));

    //俯仰稳定与阻尼：机头追随速度方向，抑制滑跑海豚跳。
    float32 pitchRate = Dot(body->angularVelocity, right);
    body->AddTorque(Scale(right, -alphaDegrees * pitchStability * TorqueScale * airFactor - pitchRate * pitchDamping));

    //起落架：机轮由 WheelCollider 驱动（悬挂/摩擦/转向侧向力），这里只写入转向角。
    List<WheelColliderComponent*> wheels;
    GetEns()->GetComponentInstances(wheels);
    for (WheelColliderComponent* wheel : wheels)
    {
        if (wheel && wheel->steeringWheel)
        {
            wheel->steerAngle = yawInput * steeringAngle;
        }
    }

    //坠毁判定：硬撞击（上一物理步的碰撞事件）或飞出训练区。
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        for (const PhysicsEvent& event : physics->GetEvents())
        {
            if (event.type == PhysicsEventType::ContactEnter
                && (event.first == GetEnsId() || event.second == GetEnsId())
                && event.impulse > crashImpulseThreshold)
            {
                crashCount++;
                ResetFlight();
                return;
            }
        }
    }
    vector3 position = transform->GetLocalPosition();
    if (position.y < -5.0f || position.y > 400.0f
        || std::abs(position.x) > 750.0f || std::abs(position.z) > 750.0f)
    {
        crashCount++;
        ResetFlight();
        return;
    }

    //检查点圈速。
    float32 radiusSquared = checkpointRadius * checkpointRadius;
    if (DistanceSquared(position, Checkpoints[static_cast<usize>(checkpointIndex)]) <= radiusSquared)
    {
        checkpointIndex++;
        if (checkpointIndex == static_cast<int32>(Checkpoints.size()))
        {
            checkpointIndex = 0;
            completedLaps++;
            if (bestLapTime <= 0.0f || lapTime < bestLapTime) bestLapTime = lapTime;
            lapTime = 0.0f;
        }
    }
}

float32 FlightController::GetAirspeed()
{
    RigidBodyComponent* body = GetEns() ? GetEns()->GetComponent<RigidBodyComponent>() : nullptr;
    return body ? Length(body->linearVelocity) : 0.0f;
}

float32 FlightController::GetThrottle()
{
    return throttle;
}

float32 FlightController::GetLapTime()
{
    return lapTime;
}

float32 FlightController::GetBestLapTime()
{
    return bestLapTime;
}

int32 FlightController::GetCheckpointIndex()
{
    return checkpointIndex;
}

int32 FlightController::GetCheckpointCount()
{
    return static_cast<int32>(Checkpoints.size());
}

int32 FlightController::GetCompletedLaps()
{
    return completedLaps;
}

int32 FlightController::GetCrashCount()
{
    return crashCount;
}

float32 FlightController::GetPitchDegrees()
{
    TransformComponent* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform) return 0.0f;

    vector3 forward = Rotate(transform->GetLocalRotation(), { 0.0f, 0.0f, -1.0f });
    return std::asin(std::clamp(forward.y, -1.0f, 1.0f)) * 180.0f / Pi;
}

float32 FlightController::GetRollDegrees()
{
    TransformComponent* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform) return 0.0f;

    quaternion rotation = transform->GetLocalRotation();
    vector3 right = Rotate(rotation, { 1.0f, 0.0f, 0.0f });
    vector3 up = Rotate(rotation, { 0.0f, 1.0f, 0.0f });
    return std::atan2(right.y, up.y) * 180.0f / Pi;
}

float32 FlightController::GetHeadingDegrees()
{
    TransformComponent* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform) return 0.0f;

    vector3 forward = Rotate(transform->GetLocalRotation(), { 0.0f, 0.0f, -1.0f });
    return std::atan2(-forward.x, -forward.z) * 180.0f / Pi;
}

float32 FlightController::GetAltitude()
{
    TransformComponent* transform = GetEns() ? GetEns()->Transform() : nullptr;
    return transform ? transform->GetLocalPosition().y : 0.0f;
}

void FlightController::ResetFlight()
{
    checkpointIndex = 0;
    lapTime = 0.0f;
    ResetAircraft();
}

void FlightController::ResetAircraft()
{
    TransformComponent* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform) return;

    throttle = 0.0f;
    transform->SetLocalPosition(spawnPosition);
    transform->SetLocalRotation(spawnRotation);

    RigidBodyComponent* body = GetEns() ? GetEns()->GetComponent<RigidBodyComponent>() : nullptr;
    if (body)
    {
        body->linearVelocity = vector3();
        body->angularVelocity = vector3();
        body->pendingForce = vector3();
        body->pendingTorque = vector3();
    }
}
