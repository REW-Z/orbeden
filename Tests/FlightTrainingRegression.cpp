#include "FileSystem/PathDefines.h"
#include "Physics/HeightFieldComponent.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsReflection.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/WheelColliderComponent.h"
#include "Platform/InputManager.h"
#include "Rendering/RenderScene.h"
#include "Rendering/TransformCache.h"
#include "Runtime/Reflection.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/WorldSerializer.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/Object/TransformComponent.h"
#include "FlightController.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

extern "C" void OrbedenGameNative_RegisterReflection();

/// <summary>检查回归条件，失败时输出原因。</summary>
void Require(bool condition, const char* message)
{
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::fflush(stderr);
    std::_Exit(1);
}

/// <summary>加载真实模板，验证地形生成、三轮支撑及全油门起飞。</summary>
int main(int argc, char** argv)
{
    Require(argc >= 2, "Pass the FlightTraining template directory");
    Reflection::RegisterGeneratedReflection();
    PhysicsReflection::Register();
    OrbedenGameNative_RegisterReflection();
    PathDefines::SetContentRoot(argv[1]);
    World world;
    World::SetCurrentWorld(&world);
    Require(WorldSerializer::LoadXml(world, PathDefines::GetContentFilePath("World/main.world")), "Load template world");
    FlightController* flight = nullptr;
    HeightFieldComponent* terrain = nullptr;
    world.ForEachComponent<FlightController>([&](FlightController* value) { flight = value; });
    world.ForEachComponent<HeightFieldComponent>([&](HeightFieldComponent* value) { terrain = value; });
    Require(flight && terrain, "Template components exist");
    Require(terrain->flattenMinZ == -180 && terrain->flattenMaxZ == 180, "Deserialize runway flatten region");
    Require(terrain->material.Get() != nullptr, "Deserialize and resolve terrain material");

    //只更新渲染场景，不启动物理，确认编辑模式也生成地形。
    RenderScene scene;
    TransformCache cache;
    scene.Update(world, cache);
    Mesh* mesh = terrain->GetEns()->GetComponent<StaticMeshRenderer>()->mesh.Get();
    Require(mesh && mesh->vertices.size() == 129 * 129, "Generate terrain in edit mode");
    for (const vector3& normal : mesh->normals) Require(normal.y > 0, "Terrain normals face upward");
    std::printf("terrain: %zu vertices, upward normals, material resolved\n", mesh->vertices.size());

    List<WheelColliderComponent*> wheels;
    flight->GetEns()->GetComponentInstances(wheels);
    Require(wheels.size() == 3, "Load three wheel instances");
    Require(wheels[0]->steeringWheel && wheels[1]->wheelOffset.x < 0 && wheels[2]->wheelOffset.x > 0,
        "Deserialize distinct wheel positions");
    RigidBodyComponent* body = flight->GetEns()->GetComponent<RigidBodyComponent>();
    ScriptCallbackTable callbacks = ResolveScriptCallbacks(flight->GetType());
    Require(callbacks.start && callbacks.fixedUpdate, "Resolve real template callbacks");
    callbacks.start(flight);
    PhysicsSystem physics;
    Require(physics.Initialize(), "Initialize PhysX");
    const int frequency = argc > 2 ? std::atoi(argv[2]) : 60;
    Require(frequency >= 30, "Frequency must be at least 30 Hz");
    const float dt = 1.0f / frequency;

    //不施加推进，先让起落架稳定，再观测十秒。
    float minHeight = 1000, maxHeight = -1000;
    for (int i = 0; i < 15 * frequency; ++i)
    {
        physics.FixedUpdate(world, dt);
        if (i >= 5 * frequency)
        {
            minHeight = std::min(minHeight, flight->GetAltitude());
            maxHeight = std::max(maxHeight, flight->GetAltitude());
        }
    }
    int grounded = 0;
    for (WheelColliderComponent* wheel : wheels) grounded += wheel->IsGrounded() ? 1 : 0;
    std::printf("rest: wheels=%d, height=%.4f, oscillation=%.6f, vy=%.6f\n", grounded,
        flight->GetAltitude(), maxHeight - minHeight, body->linearVelocity.y);
    Require(grounded == 3, "All three wheels support the aircraft");
    Require(maxHeight - minHeight < 0.02f && std::abs(body->linearVelocity.y) < 0.02f, "No sustained ground bouncing");

    //跑道外的平整带同样必须与地形渲染高度一致。
    for (float x : { -8.0f, 8.0f })
    {
        for (float z : { -160.0f, 0.0f, 160.0f })
        {
            PhysicsQueryHit hit;
            Require(physics.Raycast({ x, 10.0f, z }, { 0, -1, 0 }, 20, hit, 1), "Raycast flattened terrain");
            Require(std::abs(hit.position.y) < 0.01f, "Collision terrain matches visible flattened terrain");
        }
    }

    //全油门滑跑，达到 25 m/s 后拉杆，使用未经替换的模板代码。
    InputManager::SetEnabled(true);
    InputManager::SetKeyState(KeyEnum::LSHIFT, true);
    bool airborne = false;
    for (int i = 0; i < 15 * frequency; ++i)
    {
        InputManager::BeginFrame();
        InputManager::SetKeyState(KeyEnum::S, flight->GetAirspeed() >= 25 && flight->GetPitchDegrees() < 12);
        callbacks.fixedUpdate(flight, dt);
        physics.FixedUpdate(world, dt);
        if (i % (2 * frequency) == 0) std::printf("flight: t=%.1f speed=%.2f altitude=%.2f pitch=%.2f z=%.2f\n",
            i * dt, flight->GetAirspeed(), flight->GetAltitude(), flight->GetPitchDegrees(),
            flight->GetEns()->Transform()->GetLocalPosition().z);
        if (flight->GetAltitude() > 5 && body->linearVelocity.y > 0)
        {
            airborne = true;
            std::printf("takeoff: t=%.2f speed=%.2f altitude=%.2f\n", i * dt, flight->GetAirspeed(), flight->GetAltitude());
            break;
        }
    }
    Require(airborne && flight->GetCrashCount() == 0, "Take off without resetting");
    //离地后继续飞行十秒，避免只验证一次短暂弹起。
    for (int i = 0; i < 10 * frequency; ++i)
    {
        InputManager::BeginFrame();
        InputManager::SetKeyState(KeyEnum::S, flight->GetPitchDegrees() < 8);
        InputManager::SetKeyState(KeyEnum::W, flight->GetPitchDegrees() > 15);
        callbacks.fixedUpdate(flight, dt);
        physics.FixedUpdate(world, dt);
        Require(flight->GetAltitude() > 3 && flight->GetCrashCount() == 0, "Sustain flight without ground contact or reset");
    }
    std::printf("cruise: speed=%.2f altitude=%.2f z=%.2f\n", flight->GetAirspeed(), flight->GetAltitude(),
        flight->GetEns()->Transform()->GetLocalPosition().z);
    //从空中平飞姿态分别施加副翼，验证实际机翼倾斜方向。
    InputManager::SetKeyState(KeyEnum::LSHIFT, false);
    InputManager::SetKeyState(KeyEnum::S, false);
    InputManager::SetKeyState(KeyEnum::W, false);
    for (KeyEnum key : { KeyEnum::A, KeyEnum::D })
    {
        flight->ResetFlight();
        flight->GetEns()->Transform()->SetLocalPosition({ 0, 100, 0 });
        flight->GetEns()->Transform()->SetLocalRotation({ 0, 0, 0, 1 });
        body->linearVelocity = { 0, 0, -30 };
        cache.Update(world);
        InputManager::BeginFrame();
        InputManager::SetKeyState(key, true);
        callbacks.fixedUpdate(flight, dt);
        physics.FixedUpdate(world, dt);
        float roll = flight->GetRollDegrees();
        Require(key == KeyEnum::A ? roll > 0 : roll < 0, "A banks left; D banks right");
        std::printf("bank: key=%c roll=%.4f degrees\n", key == KeyEnum::A ? 'A' : 'D', roll);
        InputManager::SetKeyState(key, false);
    }
    physics.Shutdown();
    scene.UnbindWorld();
    world.Clear();
    World::SetCurrentWorld(nullptr);
    ResourceManager::Shutdown();
    std::puts("PASS");
}
