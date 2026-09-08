#pragma once

#include "Scripting/ScriptBehaviour.h"
#include "Runtime/EngineTypes.h"

//飞行训练 Demo 的物理气动控制器；飞机由 RigidBody + Collider 驱动，
//升力采用升力曲线采样（15 度失速），起落架为弹簧阻尼悬挂。
class FlightController final : public ScriptBehaviour
{
    OBJECT_TYPE_DECLARE(FlightController)

public:
    //航线
    float32 checkpointRadius = 12.0f;
    //推进（推重比 > 1：13000N / 1200kg ≈ 1.1g）
    float32 throttleRate = 1.0f;
    float32 maxThrust = 13000.0f;
    //气动
    float32 wingArea = 20.0f;
    float32 liftSlope = 0.105f;
    //训练机升力倍率，改善低速爬升。
    float32 liftMultiplier = 1.15f;
    float32 zeroLiftAngle = -4.0f;
    float32 stallAngle = 15.0f;
    float32 postStallCl = 0.9f;
    float32 dragCoefficient = 0.03f;
    float32 elevatorAuthority = 6.0f;
    float32 aileronAuthority = 7.0f;
    float32 rudderAuthority = 5.0f;
    float32 pitchStability = 0.6f;
    float32 pitchDamping = 4000.0f;
    //地面转向（写入转向轮的 WheelCollider.steerAngle）。
    float32 steeringAngle = 30.0f;
    float32 crashImpulseThreshold = 15000.0f;

    //以下公开方法供 C# HUD 通过预解析句柄读取。
    float32 GetAirspeed();
    float32 GetThrottle();
    float32 GetLapTime();
    float32 GetBestLapTime();
    int32 GetCheckpointIndex();
    int32 GetCheckpointCount();
    int32 GetCompletedLaps();
    int32 GetCrashCount();
    float32 GetPitchDegrees();
    float32 GetRollDegrees();
    float32 GetHeadingDegrees();
    float32 GetAltitude();
    void ResetFlight();

private:
    vector3 spawnPosition;
    quaternion spawnRotation;
    float32 throttle = 0.0f;
    float32 lapTime = 0.0f;
    float32 bestLapTime = 0.0f;
    int32 checkpointIndex = 0;
    int32 completedLaps = 0;
    int32 crashCount = 0;

    void ResetAircraft();

protected:
    void OnStart();
    void OnFixedUpdate(float32 deltaTime);
};
