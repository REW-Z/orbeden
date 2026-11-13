#include "FlightController.h"
#include "Engine.h"
#include "FlightTerrainStreamer.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsTypes.h"
#include "Runtime/Object/RigidBody.h"
#include "Runtime/Object/WheelCollider.h"
#include "InputManager/InputManager.h"
#include "Rendering/RenderSystem.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/Ens.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/World.h"
#include "PxRigidBody.h"
#include <algorithm>
#include <cmath>

OBJECT_TYPE_IMPLEMENT(FlightController, Script)

namespace
{
    constexpr float32 Pi = 3.14159265358979323846f;
    constexpr float32 AirDensity = 1.225f;
    constexpr float32 ClMax = 1.6f;
    constexpr float32 FlapStep = 0.5f;

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
    Transform* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform)
    {
        return;
    }
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
    //襟翼按键调整目标档位
    if (Input::KeyDown(KeyEnum::Z))
    {
        SetFlapTarget(flapTarget - FlapStep);
    }
    if (Input::KeyDown(KeyEnum::X))
    {
        SetFlapTarget(flapTarget + FlapStep);
    }
}

/// <summary>计算并施加机翼、垂尾和舵面作用。</summary>
void FlightController::OnFixedUpdate(float32 deltaTime)
{
    Transform* transform = GetEns() ? GetEns()->Transform() : nullptr;
    RigidBody* body = GetEns() ? GetEns()->GetComponent<RigidBody>() : nullptr;
    if (!transform || !body || !body->enabled)
    {
        return;
    }
    //襟翼按收放速度靠近目标档位
    float32 flapStep = std::max(flapDeployRate, 0.0f) * std::max(deltaTime, 0.0f);
    flapPosition = std::clamp(flapPosition + std::clamp(flapTarget - flapPosition, -flapStep, flapStep), 0.0f, 1.0f);
    float32 yawInput = GetYawInput();

    //按作用点施加各部件气动力
    EvaluateForces(*body, *transform);
    for (const SurfaceForces& surface : surfaces)
    {
        body->AddForceAtPosition(Add(Add(surface.lift, surface.drag), surface.side), surface.position);
    }

    PhysicsSystem* physics = PhysicsSystem::Current();
    List<WheelCollider*> wheels;
    GetEns()->CollectComponentsInDescendants(wheels);
    int groundedWheelCount = 0;
    for (WheelCollider* wheel : wheels)
    {
        if (!wheel || !wheel->enabled || !wheel->GetEns()->GetWorldActive())
        {
            continue;
        }
        if (wheel->steeringWheel)
        {
            wheel->steerAngle = yawInput * steeringAngle;
        }
        if (wheel->IsGrounded())
        {
            ++groundedWheelCount;
        }
    }

    //在接地机轮下方施加纵向刹车力
    float32 brake = std::clamp(brakeInput, 0.0f, 1.0f);
    if (brake > 0.0f && groundedWheelCount > 0 && deltaTime > 0.0f)
    {
        vector3 center = transform->GetWorldPosition();
        if (physics)
        {
            physx::PxRigidActor* actor = physics->GetRigidActor(GetEnsId());
            physx::PxRigidBody* nativeBody = actor ? actor->is<physx::PxRigidBody>() : nullptr;
            if (nativeBody)
            {
                physx::PxVec3 position = nativeBody->getGlobalPose().transform(nativeBody->getCMassLocalPose().p);
                center = { position.x, position.y, position.z };
            }
        }
        for (WheelCollider* wheel : wheels)
        {
            if (!wheel || !wheel->enabled || !wheel->GetEns()->GetWorldActive() || !wheel->IsGrounded())
            {
                continue;
            }
            Transform* wheelTransform = wheel->GetEns()->Transform();
            quaternion rotation = wheelTransform->GetWorldRotation();
            vector3 normal = Rotate(rotation, { 0, 1, 0 });
            vector3 forward = Rotate(rotation, { 0, 0, -1 });
            if (wheel->steeringWheel)
            {
                float32 steerRadians = wheel->steerAngle * Pi / 180.0f;
                forward = Add(Scale(forward, std::cos(steerRadians)), Scale(Cross(normal, forward), std::sin(steerRadians)));
            }
            const float32* matrix = wheelTransform->GetWorldMatrix().m;
            float32 scale = std::sqrt(matrix[4] * matrix[4] + matrix[5] * matrix[5] + matrix[6] * matrix[6]);
            float32 compression = wheel->GetCompression();
            vector3 mount = RenderMath::TransformPoint(wheelTransform->GetWorldMatrix(), wheel->wheelOffset);
            vector3 contact = Add(mount, Scale(normal, -((wheel->suspensionRestLength + wheel->wheelRadius) * scale - compression)));
            vector3 offset = Add(contact, Scale(center, -1.0f));
            vector3 velocity = Add(body->linearVelocity, Cross(body->angularVelocity, offset));
            float32 forwardSpeed = Dot(velocity, forward);
            float32 normalForce = std::max(wheel->suspensionStiffness * compression
                - wheel->suspensionDamping * Dot(velocity, normal), 0.0f);
            float32 tractionLimit = brake * std::max(brakeFrictionCoefficient, 0.0f) * normalForce;
            float32 stopLimit = std::max(std::abs(forwardSpeed) - 0.02f, 0.0f) * body->mass
                / (deltaTime * groundedWheelCount);
            vector3 brakeForce = Scale(forward, -std::copysign(std::min(tractionLimit, stopLimit), forwardSpeed));
            body->AddForceAtPosition(brakeForce, contact);
        }
    }

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
    //跌穿地面后复位
    if (transform->GetLocalPosition().y < -200.0f)
    {
        ++crashCount;
        ResetFlight();
    }
}

//计算升力系数与失速衰减
float32 FlightController::CalculateLiftCoefficient(float32 angleDegrees, float32 slope, float32 zeroLiftDegrees,
    float32 stallDegrees, float32 postStallRatio, float32 maxCoefficient) const
{
    float32 cl = std::clamp(std::max(slope, 0.0f) * (angleDegrees - zeroLiftDegrees), -maxCoefficient, maxCoefficient);
    float32 stall = std::max(stallDegrees, 1.0f);
    if (std::abs(angleDegrees) > stall)
    {
        float32 stallCl = std::clamp(std::max(slope, 0.0f)
            * (std::copysign(stall, angleDegrees) - zeroLiftDegrees), -maxCoefficient, maxCoefficient);
        float32 falloff = std::clamp((std::abs(angleDegrees) - stall) / 20.0f, 0.0f, 1.0f);
        cl = stallCl * (1.0f - falloff * (1.0f - std::clamp(postStallRatio, 0.0f, 1.0f)));
    }
    return cl;
}

//计算表面升力与阻力
FlightController::SurfaceForces FlightController::CalculateSurfaceForces(const vector3& position,
    const vector3& velocity, const vector3& spanAxis, float32 area, float32 liftCoefficient, float32 surfaceDragCoefficient) const
{
    SurfaceForces result;
    result.position = position;
    vector3 liftDirection = Cross(spanAxis, velocity);
    float32 liftDirectionLength = Length(liftDirection);
    float32 speed = Length(velocity);
    float32 surfaceArea = std::max(area, 0.0f);
    //按垂直翼展的气流计算升力
    result.lift = liftDirectionLength > 0.001f
        ? Scale(liftDirection, 0.5f * AirDensity * liftDirectionLength * surfaceArea * liftCoefficient) : vector3();
    float32 dynamicPressure = 0.5f * AirDensity * speed * speed;
    result.drag = speed > 0.001f
        ? Scale(velocity, -dynamicPressure * surfaceArea * std::max(surfaceDragCoefficient, 0.0f) / speed) : vector3();
    return result;
}

//计算各部件受力与仪表数值
void FlightController::EvaluateForces(const RigidBody& body, const Transform& transform)
{
    World* world = GetWorld();
    if (!world)
    {
        return;
    }

    //同步飞机与引用节点的世界变换
    quaternion rotation = transform.GetWorldRotation();
    quaternion inverse{ -rotation.x, -rotation.y, -rotation.z, rotation.w };
    vector3 center = transform.GetWorldPosition();
    vector3 right = Rotate(rotation, { 1, 0, 0 });
    vector3 up = Rotate(rotation, { 0, 1, 0 });
    vector3 velocityBody = Rotate(inverse, body.linearVelocity);
    sideslipDegrees = std::atan2(velocityBody.x,
        std::sqrt(velocityBody.z * velocityBody.z + velocityBody.y * velocityBody.y)) * 180.0f / Pi;
    float32 pitchInput = GetPitchInput();
    float32 rollInput = GetRollInput();
    float32 yawInput = GetYawInput();
    const EnsId nodes[] = { leftFlapSection, leftAileronSection, leftTipSection,
        rightFlapSection, rightAileronSection, rightTipSection, horizontalTail, verticalTail, fuselage };
    const float32 sectionWeights[] = { std::max(wingSectionAreaFractions.x, 0.0f),
        std::max(wingSectionAreaFractions.y, 0.0f), std::max(wingSectionAreaFractions.z, 0.0f) };
    float32 areaWeight = sectionWeights[0] + sectionWeights[1] + sectionWeights[2];
    float32 sectionAreaScale = areaWeight > 0.0f ? wingArea * 0.5f / areaWeight : 0.0f;
    liftForce = {};
    dragNewtons = 0.0f;
    sideNewtons = 0.0f;

    //逐点计算主翼、尾翼和机身受力
    for (int i = 0; i < 9; ++i)
    {
        surfaces[i] = {};
        Ens* node = world->GetEns(nodes[i]);
        if (!node)
        {
            continue;
        }
        vector3 position = node->Transform()->GetWorldPosition();
        vector3 offset = Add(position, Scale(center, -1.0f));
        vector3 velocity = Add(body.linearVelocity, Cross(body.angularVelocity, offset));
        vector3 localVelocity = Rotate(inverse, velocity);
        float32 alpha = std::atan2(-localVelocity.y, -localVelocity.z) * 180.0f / Pi;

        if (i < 6)
        {
            int section = i % 3;
            float32 flap = section == 0 ? flapPosition : 0.0f;
            float32 aileron = section == 1 ? (i < 3 ? rollInput : -rollInput) : 0.0f;
            float32 cl = CalculateLiftCoefficient(alpha, liftSlope, zeroLiftAngle, stallAngle, postStallCl, ClMax)
                * std::max(liftMultiplier, 0.0f) + std::max(flapLiftCoefficient, 0.0f) * flap
                + aileron * aileronLiftCoefficient;
            float32 cd = std::max(dragCoefficient, 0.0f) + std::max(inducedDragCoefficient, 0.0f) * cl * cl
                + std::max(flapDragCoefficient, 0.0f) * flap;
            surfaces[i] = CalculateSurfaceForces(position, velocity, right, sectionWeights[section] * sectionAreaScale, cl, cd);
        }
        else if (i == 6 || i == 7)
        {
            bool vertical = i == 7;
            float32 angle = vertical
                ? std::atan2(localVelocity.x, -localVelocity.z) * 180.0f / Pi : alpha + horizontalTailIncidence;
            float32 cl = CalculateLiftCoefficient(angle, vertical ? verticalFinLiftSlope : horizontalTailLiftSlope,
                0.0f, tailStallAngle, tailPostStallCl, 1.2f)
                - (vertical ? yawInput * rudderSideCoefficient : pitchInput * elevatorLiftCoefficient);
            float32 cd = std::max(tailDragCoefficient, 0.0f) + std::max(tailInducedDragCoefficient, 0.0f) * cl * cl;
            surfaces[i] = CalculateSurfaceForces(position, velocity, vertical ? up : right,
                vertical ? verticalFinArea : horizontalTailArea, cl, cd);
            if (vertical)
            {
                surfaces[i].side = surfaces[i].lift;
                surfaces[i].lift = {};
            }
        }
        else
        {
            vector3 forceBody{
                -0.5f * AirDensity * std::max(fuselageDragAreas.x, 0.0f) * std::abs(localVelocity.x) * localVelocity.x,
                -0.5f * AirDensity * std::max(fuselageDragAreas.y, 0.0f) * std::abs(localVelocity.y) * localVelocity.y,
                -0.5f * AirDensity * std::max(fuselageDragAreas.z, 0.0f) * std::abs(localVelocity.z) * localVelocity.z
            };
            surfaces[i].position = position;
            surfaces[i].side = Scale(right, forceBody.x);
            surfaces[i].drag = Rotate(rotation, { 0, forceBody.y, forceBody.z });
        }

        liftForce = Add(liftForce, surfaces[i].lift);
        dragNewtons += Length(surfaces[i].drag);
        sideNewtons += Dot(surfaces[i].side, right);
    }

    //读取物理系统重力
    PhysicsSystem* physics = PhysicsSystem::Current();
    vector3 gravity = physics ? physics->GetGravity() : vector3{ 0, -9.81f, 0 };
    gravityForce = body.useGravity ? Scale(gravity, body.mass) : vector3();
}

//按各部件作用点绘制受力箭头
void FlightController::OnLateUpdate(float32 deltaTime)
{
    (void)deltaTime;
    RenderSystem* renderer = RenderSystem::Current();
    if (!GetEns() || !GetWorld())
    {
        return;
    }
    RigidBody* body = GetEns()->GetComponent<RigidBody>();
    if (!body || !body->enabled)
    {
        return;
    }
    //刷新帧末受力与作用点
    EvaluateForces(*body, *GetEns()->Transform());
    Engine* engine = GetEns()->GetComponent<Engine>();
    if (engine)
    {
        engine->EvaluateForces();
    }
    if (!showForces || !renderer)
    {
        return;
    }

    constexpr int PropellerIndex = 9 * 3;
    constexpr int ForceCount = PropellerIndex + 3;
    vector3 origins[ForceCount];
    vector3 forces[ForceCount];
    color colors[ForceCount];
    for (int i = 0; i < 9; ++i)
    {
        origins[i * 3] = origins[i * 3 + 1] = origins[i * 3 + 2] = surfaces[i].position;
        forces[i * 3] = surfaces[i].lift;
        forces[i * 3 + 1] = surfaces[i].drag;
        forces[i * 3 + 2] = surfaces[i].side;
        colors[i * 3] = { 0.15f, 0.45f, 1, 1 };
        colors[i * 3 + 1] = { 1, 0.15f, 0.1f, 1 };
        colors[i * 3 + 2] = { 0.2f, 1, 0.4f, 1 };
    }
    origins[PropellerIndex] = origins[PropellerIndex + 1] = engine ? engine->GetPropellerPosition() : vector3();
    forces[PropellerIndex] = engine ? engine->GetWindmillDragForce() : vector3();
    forces[PropellerIndex + 1] = engine ? engine->GetThrustForce() : vector3();
    colors[PropellerIndex] = { 1, 0.15f, 0.1f, 1 };
    colors[PropellerIndex + 1] = { 1, 0.85f, 0.1f, 1 };
    origins[PropellerIndex + 2] = GetEns()->Transform()->GetWorldPosition();
    forces[PropellerIndex + 2] = gravityForce;
    colors[PropellerIndex + 2] = { 0, 0, 0, 1 };

    for (int i = 0; i < ForceCount; ++i)
    {
        vector3 origin = origins[i];
        vector3 line = Scale(forces[i], std::max(forceDrawScale, 0.0f));
        float32 length = Length(line);
        if (length < 0.001f)
        {
            continue;
        }
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
    RigidBody* body = GetEns() ? GetEns()->GetComponent<RigidBody>() : nullptr;
    return body ? Length(body->linearVelocity) : 0.0f;
}

/// <summary>读取襟翼位置，0 收起，1 全放。</summary>
float32 FlightController::GetFlapPosition() { return flapPosition; }

//读取襟翼目标位置
float32 FlightController::GetFlapTarget() { return flapTarget; }

//设置襟翼目标位置
void FlightController::SetFlapTarget(float32 value) { flapTarget = std::clamp(value, 0.0f, 1.0f); }

//读取俯仰配平量
float32 FlightController::GetPitchTrim() { return pitchTrim; }

//设置俯仰配平量
void FlightController::SetPitchTrim(float32 value) { pitchTrim = std::clamp(value, -1.0f, 1.0f); }

//读取含配平偏置的俯仰输入
float32 FlightController::GetPitchInput()
{
    float32 keyboard = (Input::Key(KeyEnum::S) ? 1.0f : 0.0f) - (Input::Key(KeyEnum::W) ? 1.0f : 0.0f);
    return std::clamp(keyboard + pitchTrim, -1.0f, 1.0f);
}

/// <summary>读取滚转输入，右滚为正。</summary>
float32 FlightController::GetRollInput()
{
    return (Input::Key(KeyEnum::D) ? 1.0f : 0.0f) - (Input::Key(KeyEnum::A) ? 1.0f : 0.0f);
}

/// <summary>读取偏航输入，左偏航为正。</summary>
float32 FlightController::GetYawInput()
{
    return (Input::Key(KeyEnum::Q) ? 1.0f : 0.0f) - (Input::Key(KeyEnum::E) ? 1.0f : 0.0f);
}

/// <summary>读取坠毁次数。</summary>
int32 FlightController::GetCrashCount() { return crashCount; }
/// <summary>读取侧滑角。</summary>
float32 FlightController::GetSideslipDegrees() { return sideslipDegrees; }
/// <summary>读取升力牛顿数。</summary>
float32 FlightController::GetLiftNewtons() { return Length(liftForce); }
//读取各部件阻力大小之和
float32 FlightController::GetDragNewtons() { return dragNewtons; }
//读取向右为正的侧向合力
float32 FlightController::GetSideForceNewtons() { return sideNewtons; }
/// <summary>读取重力牛顿数。</summary>
float32 FlightController::GetWeightNewtons() { return Length(gravityForce); }
/// <summary>读取垂直爬升速度。</summary>
float32 FlightController::GetClimbRate()
{
    RigidBody* body = GetEns() ? GetEns()->GetComponent<RigidBody>() : nullptr;
    return body ? body->linearVelocity.y : 0.0f;
}

float32 FlightController::GetPitchDegrees()
{
    Transform* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform) return 0.0f;

    vector3 forward = Rotate(transform->GetLocalRotation(), { 0.0f, 0.0f, -1.0f });
    return std::asin(std::clamp(forward.y, -1.0f, 1.0f)) * 180.0f / Pi;
}

//读取右倾为正的滚转角
float32 FlightController::GetRollDegrees()
{
    Transform* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform) return 0.0f;

    quaternion rotation = transform->GetLocalRotation();
    vector3 right = Rotate(rotation, { 1.0f, 0.0f, 0.0f });
    vector3 up = Rotate(rotation, { 0.0f, 1.0f, 0.0f });
    return std::atan2(-right.y, up.y) * 180.0f / Pi;
}

float32 FlightController::GetHeadingDegrees()
{
    Transform* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform) return 0.0f;

    vector3 forward = Rotate(transform->GetLocalRotation(), { 0.0f, 0.0f, -1.0f });
    return std::atan2(forward.x, -forward.z) * 180.0f / Pi;
}

float32 FlightController::GetAltitude()
{
    Transform* transform = GetEns() ? GetEns()->Transform() : nullptr;
    return transform ? transform->GetLocalPosition().y : 0.0f;
}


/// <summary>复位到机场并清空力和速度，不保留航路任务。</summary>
void FlightController::ResetFlight()
{
    Transform* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform) return;
    if (FlightTerrainStreamer* terrain = GetEns()->GetComponent<FlightTerrainStreamer>()) terrain->ResetOrigin();
    if (Engine* engine = GetEns()->GetComponent<Engine>()) engine->ResetThrottle();
    sideslipDegrees = 0;
    liftForce = gravityForce = vector3();
    dragNewtons = sideNewtons = 0.0f;
    for (SurfaceForces& surface : surfaces)
    {
        surface = {};
    }
    flapPosition = 0.0f;
    flapTarget = 0.0f;
    pitchTrim = 0.0f;
    brakeInput = 0.0f;
    transform->SetLocalPosition(spawnPosition);
    transform->SetLocalRotation(spawnRotation);
    if (RigidBody* body = GetEns()->GetComponent<RigidBody>())
    {
        body->linearVelocity = {};
        body->angularVelocity = {};
        body->pendingForce = {};
        body->pendingTorque = {};
    }
}
