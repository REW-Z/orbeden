#pragma once

#include "Runtime/Object/Script.h"
#include "Runtime/EngineTypes.h"
#include "Rendering/TransformCache.h"

class RigidBody;
class Transform;

//自由飞行控制器：气动力、尾翼稳定与运行时受力可视化。
class FlightController final : public Script
{
    OBJECT_TYPE_DECLARE(FlightController)

public:
    EnsId leftFlapSection;
    EnsId leftAileronSection;
    EnsId leftTipSection;
    EnsId rightFlapSection;
    EnsId rightAileronSection;
    EnsId rightTipSection;
    EnsId horizontalTail;
    EnsId verticalTail;
    EnsId fuselage;

    float32 wingArea = 24.0f;
    //每侧襟翼段、副翼段、翼尖段的面积权重
    vector3 wingSectionAreaFractions = { 0.5f, 0.3f, 0.2f };
    float32 liftSlope = 0.105f;
    float32 liftMultiplier = 1.3f;
    float32 zeroLiftAngle = -5.0f;
    float32 stallAngle = 15.0f;
    float32 postStallCl = 0.45f;
    float32 dragCoefficient = 0.028f;
    float32 inducedDragCoefficient = 0.045f;
    //襟翼段满行程增加的升力与寄生阻力系数，收放速度按满行程每秒计
    float32 flapLiftCoefficient = 0.6f;
    float32 flapDragCoefficient = 0.06f;
    float32 flapDeployRate = 0.4f;
    //尾翼面积按平方米、安装角按度、升力斜率按每度计
    float32 horizontalTailArea = 4.0f;
    float32 horizontalTailLiftSlope = 0.07f;
    float32 horizontalTailIncidence = 0.0f;
    float32 verticalFinArea = 2.2f;
    float32 verticalFinLiftSlope = 0.055f;
    float32 tailStallAngle = 20.0f;
    float32 tailPostStallCl = 0.3f;
    float32 tailDragCoefficient = 0.012f;
    float32 tailInducedDragCoefficient = 0.08f;
    float32 elevatorLiftCoefficient = 0.8f;
    float32 aileronLiftCoefficient = 0.8f;
    float32 rudderSideCoefficient = 0.8f;
    //机体右、上、前方向的等效阻力面积，已含阻力系数
    vector3 fuselageDragAreas = { 8.0f, 3.0f, 0.35f };
    float32 steeringAngle = 30.0f;
    //刹车轴范围为 0 到 1，摩擦系数限制接地制动力
    float32 brakeInput = 0.0f;
    float32 brakeFrictionCoefficient = 0.8f;
    float32 crashImpulseThreshold = 15000.0f;
    bool showForces = true;
    //米/牛顿；所有箭头使用同一比例，不分别归一化。
    float32 forceDrawScale = 0.0005f;

    float32 GetAirspeed();
    int32 GetCrashCount();
    float32 GetPitchDegrees();
    float32 GetRollDegrees();
    float32 GetHeadingDegrees();
    float32 GetAltitude();
    float32 GetClimbRate();
    float32 GetSideslipDegrees();
    float32 GetLiftNewtons();
    float32 GetDragNewtons();
    float32 GetSideForceNewtons();
    float32 GetWeightNewtons();
    float32 GetFlapPosition();
    float32 GetFlapTarget();
    void SetFlapTarget(float32 value);
    float32 GetPitchTrim();
    void SetPitchTrim(float32 value);
    float32 GetPitchInput();
    float32 GetRollInput();
    float32 GetYawInput();
    void ResetFlight();

private:
    struct SurfaceForces
    {
        vector3 position;
        vector3 lift;
        vector3 drag;
        vector3 side;
    };

    ORBEDEN_BIND_IGNORE
    SurfaceForces surfaces[9];
    ORBEDEN_BIND_IGNORE
    TransformCache transformCache;
    vector3 spawnPosition;
    quaternion spawnRotation;
    float32 sideslipDegrees = 0.0f;
    float32 flapPosition = 0.0f;
    float32 flapTarget = 0.0f;
    float32 pitchTrim = 0.0f;
    int32 crashCount = 0;
    vector3 liftForce;
    float32 dragNewtons = 0.0f;
    float32 sideNewtons = 0.0f;
    vector3 gravityForce;

    //计算各部件受力
    void EvaluateForces(const RigidBody& body, const Transform& transform);

    //计算升力系数与失速衰减
    float32 CalculateLiftCoefficient(float32 angleDegrees, float32 slope, float32 zeroLiftDegrees,
        float32 stallDegrees, float32 postStallRatio, float32 maxCoefficient) const;

    //计算表面升力与阻力
    SurfaceForces CalculateSurfaceForces(const vector3& position, const vector3& velocity,
        const vector3& spanAxis, float32 area, float32 liftCoefficient, float32 surfaceDragCoefficient) const;

protected:
    void OnStart();
    void OnUpdate(float32 deltaTime);
    void OnFixedUpdate(float32 deltaTime);
    void OnLateUpdate(float32 deltaTime);
};
