#include "FlightController.h"
#include "FlightTerrainStreamer.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsTypes.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/WheelColliderComponent.h"
#include "Platform/InputManager.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/TransformComponent.h"
#include <algorithm>
#include <cmath>

OBJECT_TYPE_IMPLEMENT(FlightController, ScriptBehaviour)

namespace
{
    constexpr float32 Pi = 3.14159265358979323846f;
    constexpr float32 AirDensity = 1.225f;
    constexpr float32 ClMax = 1.6f;
    constexpr float32 TorqueScale = 350.0f;

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

/// <summary>记录机场初始姿态。</summary>
void FlightController::OnStart()
{
    TransformComponent* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform) return;
    spawnPosition = transform->GetLocalPosition();
    spawnRotation = transform->GetLocalRotation();
    ResetFlight();
}

/// <summary>逐帧处理重置和受力显示开关，避免固定步重复消费按键。</summary>
void FlightController::OnUpdate(float32 deltaTime)
{
    (void)deltaTime;
    if (Input::KeyDown(KeyEnum::R)) ResetFlight();
    if (Input::KeyDown(KeyEnum::F)) showForces = !showForces;
}

/// <summary>计算并施加机翼、垂尾、螺旋桨和舵面作用。</summary>
void FlightController::OnFixedUpdate(float32 deltaTime)
{
    TransformComponent* transform = GetEns() ? GetEns()->Transform() : nullptr;
    RigidBodyComponent* body = GetEns() ? GetEns()->GetComponent<RigidBodyComponent>() : nullptr;
    if (!transform || !body || !body->enabled) return;
    float32 step = std::max(deltaTime, 0.0f);
    float32 throttleInput = (Input::Key(KeyEnum::LSHIFT) ? 1.0f : 0.0f) - (Input::Key(KeyEnum::LCTRL) ? 1.0f : 0.0f);
    throttle = std::clamp(throttle + throttleInput * throttleRate * step, 0.0f, 1.0f);
    float32 pitchInput = (Input::Key(KeyEnum::S) ? 1.0f : 0.0f) - (Input::Key(KeyEnum::W) ? 1.0f : 0.0f);
    float32 rollInput = (Input::Key(KeyEnum::D) ? 1.0f : 0.0f) - (Input::Key(KeyEnum::A) ? 1.0f : 0.0f);
    float32 yawInput = (Input::Key(KeyEnum::Q) ? 1.0f : 0.0f) - (Input::Key(KeyEnum::E) ? 1.0f : 0.0f);

    quaternion rotation = transform->GetLocalRotation();
    EvaluateForces(*body, rotation);
    body->AddForce(liftForce);
    body->AddForce(dragForce);
    body->AddForce(thrustForce);
    body->AddTorque(finTorque);
    vector3 right = Rotate(rotation, { 1, 0, 0 });
    vector3 up = Rotate(rotation, { 0, 1, 0 });
    vector3 forward = Rotate(rotation, { 0, 0, -1 });
    float32 forwardSpeed = Dot(body->linearVelocity, forward);
    float32 airFactor = std::clamp(std::abs(forwardSpeed) / 12.0f, 0.0f, 1.0f);
    body->AddTorque(Scale(right, (pitchInput * elevatorAuthority - (alphaDegrees - pitchTrimAngle) * pitchStability)
        * TorqueScale * airFactor - Dot(body->angularVelocity, right) * pitchDamping));
    body->AddTorque(Scale(forward, rollInput * aileronAuthority * TorqueScale * airFactor
        - Dot(body->angularVelocity, forward) * rollDamping));
    body->AddTorque(Scale(up, yawInput * rudderAuthority * TorqueScale * airFactor
        - Dot(body->angularVelocity, up) * yawDamping));

    PhysicsSystem* physics = PhysicsSystem::Current();
    List<WheelColliderComponent*> wheels;
    GetEns()->GetComponentInstances(wheels);
    for (WheelColliderComponent* wheel : wheels)
        if (wheel && wheel->steeringWheel) wheel->steerAngle = yawInput * steeringAngle;

    if (physics)
    {
        for (const PhysicsEvent& event : physics->GetEvents())
        {
            if (event.type == PhysicsEventType::ContactEnter && (event.first == GetEnsId() || event.second == GetEnsId())
                && event.impulse > crashImpulseThreshold)
            {
                ++crashCount;
                ResetFlight();
                return;
            }
        }
    }
    //自由飞行不再限制水平距离或飞行高度，仅在跌穿地面后复位。
    if (transform->GetLocalPosition().y < -200.0f)
    {
        ++crashCount;
        ResetFlight();
    }
}

/// <summary>从同一时刻的速度和姿态计算气动力，供物理施力和帧末诊断共用。</summary>
void FlightController::EvaluateForces(const RigidBodyComponent& body, const quaternion& rotation)
{
    vector3 velocity = body.linearVelocity;
    float32 speed = Length(velocity);
    quaternion inverse{ -rotation.x, -rotation.y, -rotation.z, rotation.w };
    vector3 forward = Rotate(rotation, { 0, 0, -1 });
    vector3 right = Rotate(rotation, { 1, 0, 0 });
    vector3 velocityBody = Rotate(inverse, velocity);
    float32 forwardSpeed = -velocityBody.z;
    alphaDegrees = forwardSpeed > 0.5f ? std::atan2(-velocityBody.y, forwardSpeed) * 180.0f / Pi : 0.0f;
    sideslipDegrees = std::atan2(velocityBody.x, std::max(std::abs(forwardSpeed), 0.5f)) * 180.0f / Pi;

    //升力与气流、翼展同时垂直；负迎角允许负升力，失速平滑衰减。
    float32 cl = std::clamp(liftSlope * (alphaDegrees - zeroLiftAngle), -ClMax, ClMax);
    float32 stall = std::max(stallAngle, 1.0f);
    if (std::abs(alphaDegrees) > stall)
    {
        float32 stallCl = std::clamp(liftSlope * (std::copysign(stall, alphaDegrees) - zeroLiftAngle), -ClMax, ClMax);
        float32 falloff = std::clamp((std::abs(alphaDegrees) - stall) / 20.0f, 0.0f, 1.0f);
        cl = stallCl * (1.0f - falloff * (1.0f - std::clamp(postStallCl, 0.0f, 1.0f)));
    }
    float32 wingSpeedSquared = forwardSpeed > 0.5f ? forwardSpeed * forwardSpeed + velocityBody.y * velocityBody.y : 0.0f;
    float32 wingPressure = 0.5f * AirDensity * wingSpeedSquared;
    float32 effectiveCl = cl * std::max(liftMultiplier, 0.0f);
    vector3 liftDirection = Cross(right, velocity);
    float32 liftDirectionLength = Length(liftDirection);
    liftForce = liftDirectionLength > 0.01f
        ? Scale(liftDirection, wingPressure * wingArea * effectiveCl / liftDirectionLength) : vector3();

    //阻力采用 Cd0 + k*Cl²，避免拉杆时旧公式过大的诱导阻力耗尽空速。
    float32 dynamicPressure = 0.5f * AirDensity * speed * speed;
    float32 drag = dynamicPressure * wingArea * (std::max(dragCoefficient, 0.0f)
        + std::max(inducedDragCoefficient, 0.0f) * effectiveCl * effectiveCl);
    dragForce = speed > 0.01f ? Scale(velocity, -drag / speed) : vector3();
    float32 thrust = maxThrust * throttle * std::clamp(1.0f - std::max(forwardSpeed, 0.0f) / std::max(thrustFadeSpeed, 1.0f), 0.0f, 1.0f);
    thrustForce = Scale(forward, thrust);

    //垂尾位于质心后方；侧向气流产生回正力矩，并通过局部角速度阻尼偏航。
    vector3 finOffset = Scale(forward, -std::max(verticalFinArm, 0.0f));
    vector3 finVelocity = Add(velocity, Cross(body.angularVelocity, finOffset));
    float32 finSideslip = std::atan2(Dot(finVelocity, right), std::max(std::abs(Dot(finVelocity, forward)), 1.0f));
    float32 finPressure = 0.5f * AirDensity * Dot(finVelocity, finVelocity);
    vector3 finForce = Scale(right, -finPressure * std::max(verticalFinArea, 0.0f)
        * std::max(sideForceSlope, 0.0f) * std::clamp(finSideslip, -0.7f, 0.7f));
    dragForce = Add(dragForce, finForce);
    finTorque = Cross(finOffset, finForce);

    PhysicsSystem* physics = PhysicsSystem::Current();
    vector3 gravity = physics ? physics->GetGravity() : vector3{ 0, -9.81f, 0 };
    gravityForce = body.useGravity ? Scale(gravity, body.mass) : vector3();
}

/// <summary>每帧绘制实际受力箭头；红阻力、蓝升力、黄推力、黑重力。</summary>
void FlightController::OnLateUpdate(float32 deltaTime)
{
    (void)deltaTime;
    RenderSystem* renderer = RenderSystem::Current();
    if (!GetEns() || !GetWorld()) return;
    RigidBodyComponent* body = GetEns()->GetComponent<RigidBodyComponent>();
    if (!body || !body->enabled) return;
    //使用模拟完成后的姿态与速度，避免旧力方向与新飞机姿态错位。
    EvaluateForces(*body, GetEns()->Transform()->GetLocalRotation());
    if (!showForces || !renderer) return;
    vector3 origin = GetEns()->Transform()->GetLocalPosition();
    const vector3 forces[] = { dragForce, liftForce, thrustForce, gravityForce };
    const color colors[] = { { 1, 0, 0, 1 }, { 0, 0, 1, 1 }, { 1, 1, 0, 1 }, { 0, 0, 0, 1 } };
    for (int i = 0; i < 4; ++i)
    {
        vector3 line = Scale(forces[i], std::max(forceDrawScale, 0.0f));
        float32 length = Length(line);
        if (length < 0.001f) continue;
        vector3 end = Add(origin, line);
        renderer->DrawLine(*GetWorld(), origin, end, colors[i]);
        vector3 direction = Scale(line, 1.0f / length);
        vector3 side = Cross(direction, std::abs(direction.y) < 0.9f ? vector3{ 0, 1, 0 } : vector3{ 1, 0, 0 });
        float32 head = std::min(length * 0.2f, 0.45f);
        side = Scale(side, head * 0.4f / std::max(Length(side), 0.001f));
        vector3 base = Add(end, Scale(direction, -head));
        renderer->DrawLine(*GetWorld(), end, Add(base, side), colors[i]);
        renderer->DrawLine(*GetWorld(), end, Add(base, Scale(side, -1)), colors[i]);
    }
}

/// <summary>读取空速。</summary>
float32 FlightController::GetAirspeed()
{
    RigidBodyComponent* body = GetEns() ? GetEns()->GetComponent<RigidBodyComponent>() : nullptr;
    return body ? Length(body->linearVelocity) : 0.0f;
}

/// <summary>读取油门。</summary>
float32 FlightController::GetThrottle() { return throttle; }
/// <summary>读取坠毁次数。</summary>
int32 FlightController::GetCrashCount() { return crashCount; }
/// <summary>读取侧滑角。</summary>
float32 FlightController::GetSideslipDegrees() { return sideslipDegrees; }
/// <summary>读取升力牛顿数。</summary>
float32 FlightController::GetLiftNewtons() { return Length(liftForce); }
/// <summary>读取阻力及垂尾侧向阻力合力牛顿数。</summary>
float32 FlightController::GetDragNewtons() { return Length(dragForce); }
/// <summary>读取推力牛顿数。</summary>
float32 FlightController::GetThrustNewtons() { return Length(thrustForce); }
/// <summary>读取重力牛顿数。</summary>
float32 FlightController::GetWeightNewtons() { return Length(gravityForce); }
/// <summary>读取垂直爬升速度。</summary>
float32 FlightController::GetClimbRate()
{
    RigidBodyComponent* body = GetEns() ? GetEns()->GetComponent<RigidBodyComponent>() : nullptr;
    return body ? body->linearVelocity.y : 0.0f;
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


/// <summary>复位到机场并清空力和速度，不保留航路任务。</summary>
void FlightController::ResetFlight()
{
    TransformComponent* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform) return;
    if (FlightTerrainStreamer* terrain = GetEns()->GetComponent<FlightTerrainStreamer>()) terrain->ResetOrigin();
    throttle = 0;
    sideslipDegrees = 0;
    liftForce = dragForce = thrustForce = gravityForce = finTorque = vector3();
    alphaDegrees = 0;
    transform->SetLocalPosition(spawnPosition);
    transform->SetLocalRotation(spawnRotation);
    if (RigidBodyComponent* body = GetEns()->GetComponent<RigidBodyComponent>())
    {
        body->linearVelocity = {};
        body->angularVelocity = {};
        body->pendingForce = {};
        body->pendingTorque = {};
    }
}
