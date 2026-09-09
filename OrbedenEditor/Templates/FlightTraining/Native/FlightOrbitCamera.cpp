#include "FlightOrbitCamera.h"
#include "Platform/InputManager.h"
#include "Runtime/Ens.h"
#include "Runtime/World.h"
#include "Runtime/Object/TransformComponent.h"
#include <algorithm>
#include <cmath>

OBJECT_TYPE_IMPLEMENT(FlightOrbitCamera, Script)

/// <summary>记录飞机目标，将相机作为独立根节点跟随。</summary>
void FlightOrbitCamera::OnStart()
{
    if (!GetWorld() || !GetEns()) return;
    target = GetEns()->Transform()->parent;
    if (!GetWorld()->GetEns(target)) return;
    initialPosition = GetEns()->Transform()->GetLocalPosition();
    initialRotation = GetEns()->Transform()->GetLocalRotation();
    GetWorld()->SetParent(GetEnsId(), EnsId());
    azimuth = 0;
    elevation = std::clamp(defaultElevation, -85.0f, 85.0f);
    dragging = false;
    OnLateUpdate(0);
}

/// <summary>右键拖动环绕，C 回到机尾；相机不继承飞机的俯仰和滚转。</summary>
void FlightOrbitCamera::OnLateUpdate(float32 deltaTime)
{
    (void)deltaTime;
    World* world = GetWorld();
    Ens* aircraft = world ? world->GetEns(target) : nullptr;
    if (!aircraft || !GetEns()) return;
    constexpr float32 DegreesToRadians = 0.017453292519943295f;
    bool pressed = Input::Key(KeyEnum::MOUSER);
    vector2 mousePosition = Input::MousePos();
    if (pressed && dragging)
    {
        azimuth = std::remainder(azimuth - (mousePosition.x - lastMousePosition.x) * sensitivity, 360.0f);
        elevation = std::clamp(elevation + (mousePosition.y - lastMousePosition.y) * sensitivity, -85.0f, 85.0f);
    }
    lastMousePosition = mousePosition;
    dragging = pressed;
    if (Input::KeyDown(KeyEnum::C))
    {
        quaternion rotation = aircraft->Transform()->GetLocalRotation();
        //机身 +Z 为尾部方向；只取其水平投影，保留世界竖直方向。
        float32 tailX = 2 * (rotation.x * rotation.z + rotation.w * rotation.y);
        float32 tailZ = 1 - 2 * (rotation.x * rotation.x + rotation.y * rotation.y);
        if (tailX * tailX + tailZ * tailZ > 0.0001f)
            azimuth = std::atan2(tailX, tailZ) / DegreesToRadians;
        elevation = std::clamp(defaultElevation, -85.0f, 85.0f);
    }

    float32 yaw = azimuth * DegreesToRadians;
    float32 pitch = elevation * DegreesToRadians;
    float32 radius = std::max(distance, 3.0f);
    vector3 center = aircraft->Transform()->GetLocalPosition();
    TransformComponent* camera = GetEns()->Transform();
    camera->SetLocalPosition({ center.x + std::sin(yaw) * std::cos(pitch) * radius,
        center.y + std::sin(pitch) * radius, center.z + std::cos(yaw) * std::cos(pitch) * radius });
    //Yaw * Pitch：相机 -Z 始终指向飞机，极角限幅避免倒置。
    float32 sy = std::sin(yaw * 0.5f), cy = std::cos(yaw * 0.5f);
    float32 sp = std::sin(pitch * 0.5f), cp = std::cos(pitch * 0.5f);
    camera->SetLocalRotation({ -cy * sp, sy * cp, sy * sp, cy * cp });
}

/// <summary>还原模板层级，便于下一次启动重新找到目标。</summary>
void FlightOrbitCamera::OnEnd()
{
    World* world = GetWorld();
    if (!world || !GetEns() || !world->GetEns(target)) return;
    world->SetParent(GetEnsId(), target);
    GetEns()->Transform()->SetLocalPosition(initialPosition);
    GetEns()->Transform()->SetLocalRotation(initialRotation);
    dragging = false;
}
