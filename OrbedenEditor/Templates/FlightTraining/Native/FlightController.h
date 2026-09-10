#pragma once

#include "Runtime/Object/Script.h"
#include "Runtime/EngineTypes.h"

class RigidBody;

//自由飞行控制器：气动力、尾翼稳定与运行时受力可视化。
class FlightController final : public Script
{
    OBJECT_TYPE_DECLARE(FlightController)

public:
    float32 throttleRate = 1.0f;
    float32 maxThrust = 15000.0f;
    float32 thrustFadeSpeed = 90.0f;
    float32 wingArea = 24.0f;
    float32 liftSlope = 0.105f;
    float32 liftMultiplier = 1.3f;
    float32 zeroLiftAngle = -5.0f;
    float32 stallAngle = 15.0f;
    float32 postStallCl = 0.45f;
    float32 dragCoefficient = 0.028f;
    float32 inducedDragCoefficient = 0.045f;
    float32 elevatorAuthority = 6.0f;
    float32 aileronAuthority = 7.0f;
    float32 rudderAuthority = 5.0f;
    float32 pitchStability = 0.6f;
    float32 pitchTrimAngle = 2.0f;
    float32 pitchDamping = 4000.0f;
    float32 rollDamping = 1200.0f;
    float32 yawDamping = 1400.0f;
    float32 verticalFinArea = 2.2f;
    float32 sideForceSlope = 2.0f;
    float32 verticalFinArm = 2.2f;
    float32 steeringAngle = 30.0f;
    float32 crashImpulseThreshold = 15000.0f;
    bool showForces = true;
    //米/牛顿；所有箭头使用同一比例，不分别归一化。
    float32 forceDrawScale = 0.0005f;

    float32 GetAirspeed();
    float32 GetThrottle();
    int32 GetCrashCount();
    float32 GetPitchDegrees();
    float32 GetRollDegrees();
    float32 GetHeadingDegrees();
    float32 GetAltitude();
    float32 GetClimbRate();
    float32 GetSideslipDegrees();
    float32 GetLiftNewtons();
    float32 GetDragNewtons();
    float32 GetThrustNewtons();
    float32 GetWeightNewtons();
    void ResetFlight();

private:
    vector3 spawnPosition;
    quaternion spawnRotation;
    float32 throttle = 0.0f;
    float32 sideslipDegrees = 0.0f;
    int32 crashCount = 0;
    vector3 liftForce;
    vector3 dragForce;
    vector3 thrustForce;
    vector3 gravityForce;
    vector3 finTorque;
    float32 alphaDegrees = 0;

    /// <summary>按当前速度和姿态计算受力，不向刚体重复施力。</summary>
    void EvaluateForces(const RigidBody& body, const quaternion& rotation);

protected:
    void OnStart();
    void OnUpdate(float32 deltaTime);
    void OnFixedUpdate(float32 deltaTime);
    void OnLateUpdate(float32 deltaTime);
};
