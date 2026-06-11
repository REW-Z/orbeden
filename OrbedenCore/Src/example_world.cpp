#include "Application.h"
#include "Log/Log.h"
#include "Platform/InputManager.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Camera.h"
#include "Runtime/DirectionalLight.h"
#include "Runtime/Ens.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/Resources/MaterialShader.h"
#include "Runtime/Resources/Mesh.h"
#include "Runtime/Resources/Skybox.h"
#include "Runtime/Resources/Texture2D.h"
#include "Runtime/SpaceComponent.h"
#include "Runtime/StaticMeshRenderer.h"
#include "Runtime/World.h"

#include <cmath>
#include <string>

namespace
{
    constexpr const char* CubeMeshKey = "Resources/Examples/World/Meshes/cube.obj//Mesh/Main";
    constexpr const char* GroundMeshKey = "Resources/Examples/World/Meshes/ground.obj//Mesh/Main";
    constexpr const char* ExampleShaderKey = "Resources/Examples/World/Shaders/blinn_phong_shadow";
    constexpr const char* SkyTextureKey = "Resources/Examples/World/Textures/sky_blue.png";

    constexpr float32 Pi = 3.14159265358979323846f;

    Ens GetOrCreateEns(World& world, const std::string& id, const std::string& name)
    {
        Ens ens = world.FindEns(StringId(id));
        return ens.IsValid() ? ens : world.CreateEnsWithStableId(id, name);
    }

    template<typename T>
    T* GetOrCreateWorldObject(World& world, const std::string& id)
    {
        Object* found = Object::FindObject(StringId(id));
        if (found)
        {
            return found->Cast<T>();
        }

        return world.CreateObject<T>(id);
    }

    quaternion Mul(const quaternion& a, const quaternion& b)
    {
        return
        {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        };
    }

    quaternion MakeYawPitchRotation(float32 yawDegrees, float32 pitchDegrees)
    {
        float32 yaw = yawDegrees * Pi / 180.0f;
        float32 pitch = pitchDegrees * Pi / 180.0f;
        quaternion yawRotation = { 0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f) };
        quaternion pitchRotation = { std::sin(pitch * 0.5f), 0.0f, 0.0f, std::cos(pitch * 0.5f) };
        return Mul(yawRotation, pitchRotation);
    }

    vector3 ForwardFromYawPitch(float32 yawDegrees, float32 pitchDegrees)
    {
        float32 yaw = yawDegrees * Pi / 180.0f;
        float32 pitch = pitchDegrees * Pi / 180.0f;
        float32 cosPitch = std::cos(pitch);
        return RenderMath::Normalize({ -std::sin(yaw) * cosPitch, std::sin(pitch), -std::cos(yaw) * cosPitch });
    }

    vector3 RightFromYaw(float32 yawDegrees)
    {
        float32 yaw = yawDegrees * Pi / 180.0f;
        return RenderMath::Normalize({ std::cos(yaw), 0.0f, -std::sin(yaw) });
    }

    vector3 Add(const vector3& a, const vector3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    vector3 Scale(const vector3& value, float32 scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    void AssignShaderToMeshMaterials(Mesh* mesh, MaterialShader* shader)
    {
        if (!mesh || !shader) return;

        for (SubMesh& subMesh : mesh->subMeshes)
        {
            Material* material = subMesh.material.Get();
            if (!material) continue;

            material->shader.Set(shader);
        }
    }

    void SetRendererMesh(Ens ens, Mesh* mesh, bool castShadows, bool receiveShadows)
    {
        StaticMeshRenderer* renderer = ens.IsValid() ? ens.GetComponent<StaticMeshRenderer>() : nullptr;
        if (!renderer && ens.IsValid())
        {
            renderer = ens.AddComponent<StaticMeshRenderer>();
        }
        if (!renderer) return;

        renderer->mesh.Set(mesh);
        renderer->castShadows = castShadows;
        renderer->receiveShadows = receiveShadows;
    }

    //示例世界相机控制系统，负责 WASD/QE 和视角输入。
    class ExampleWorldCameraSystem : public IEngineSystem
    {
    private:
        EnsId cameraEns;
        float32 yaw = 35.0f;
        float32 pitch = -22.0f;
        float32 moveSpeed = 5.0f;
        float32 lookSpeed = 90.0f;

    public:
        void SetCamera(EnsId ens, float32 newYaw, float32 newPitch)
        {
            cameraEns = ens;
            yaw = newYaw;
            pitch = newPitch;
        }

        void Update(World& world, float deltaTime) override
        {
            SpaceComponent* space = world.GetSpaceComponent(cameraEns);
            if (!space) return;

            float32 speed = moveSpeed * (Input::Key(LSHIFT) ? 2.5f : 1.0f);
            vector3 forward = ForwardFromYawPitch(yaw, pitch);
            vector3 right = RightFromYaw(yaw);
            vector3 movement;
            if (Input::Key(W)) movement = Add(movement, forward);
            if (Input::Key(S)) movement = Add(movement, Scale(forward, -1.0f));
            if (Input::Key(D)) movement = Add(movement, right);
            if (Input::Key(A)) movement = Add(movement, Scale(right, -1.0f));
            if (Input::Key(E)) movement.y += 1.0f;
            if (Input::Key(Q)) movement.y -= 1.0f;
            if (RenderMath::Dot(movement, movement) > 0.000001f)
            {
                movement = RenderMath::Normalize(movement);
                space->localPosition = Add(space->localPosition, Scale(movement, speed * deltaTime));
            }

            float32 yawInput = 0.0f;
            float32 pitchInput = 0.0f;
            if (Input::Key(LEFT)) yawInput += 1.0f;
            if (Input::Key(RIGHT)) yawInput -= 1.0f;
            if (Input::Key(UP)) pitchInput += 1.0f;
            if (Input::Key(DOWN)) pitchInput -= 1.0f;
            if (Input::Key(MOUSEL))
            {
                vector2 mouseDelta = Input::MouseMov();
                yawInput -= mouseDelta.x * 0.08f;
                pitchInput -= mouseDelta.y * 0.08f;
            }

            yaw += yawInput * lookSpeed * deltaTime;
            pitch += pitchInput * lookSpeed * deltaTime;
            if (pitch > 82.0f) pitch = 82.0f;
            if (pitch < -82.0f) pitch = -82.0f;
            space->localRotation = MakeYawPitchRotation(yaw, pitch);
        }
    };

    ExampleWorldCameraSystem gCameraSystem;
}

namespace examples
{
    //创建文件资源版复杂渲染示例场景
    void SetupExampleWorld(Application& app)
    {
        World& world = app.GetWorld();

        Mesh* cubeMesh = ResourceManager::Load<Mesh>(CubeMeshKey);
        Mesh* groundMesh = ResourceManager::Load<Mesh>(GroundMeshKey);
        MaterialShader* shader = ResourceManager::Load<MaterialShader>(ExampleShaderKey);
        Texture2D* skyTexture = ResourceManager::Load<Texture2D>(SkyTextureKey);
        if (!cubeMesh || !groundMesh || !shader || !skyTexture)
        {
            Log::Error("Example world setup failed: required resources are missing.");
            return;
        }

        AssignShaderToMeshMaterials(cubeMesh, shader);
        AssignShaderToMeshMaterials(groundMesh, shader);

        Ens cubeEns = GetOrCreateEns(world, "world://examples/world/cube", "ExampleCube");
        SetRendererMesh(cubeEns, cubeMesh, true, true);
        if (SpaceComponent* space = cubeEns.Space())
        {
            space->localPosition = { 0.0f, 1.0f, 0.0f };
            space->localScale = { 1.0f, 1.0f, 1.0f };
        }

        Ens groundEns = GetOrCreateEns(world, "world://examples/world/ground", "ExampleGround");
        SetRendererMesh(groundEns, groundMesh, false, true);

        Ens lightEns = GetOrCreateEns(world, "world://examples/world/directional_light", "ExampleDirectionalLight");
        DirectionalLight* light = lightEns.IsValid() ? lightEns.GetComponent<DirectionalLight>() : nullptr;
        if (!light && lightEns.IsValid())
        {
            light = lightEns.AddComponent<DirectionalLight>();
        }
        if (light)
        {
            light->direction = RenderMath::Normalize({ -0.45f, -1.0f, -0.35f });
            light->color = { 1.0f, 0.96f, 0.86f };
            light->intensity = 1.35f;
            light->castShadows = true;
            light->shadowBias = 0.004f;
            light->shadowStrength = 0.45f;
            light->shadowDistance = 24.0f;
        }

        Ens cameraEns = GetOrCreateEns(world, "world://examples/world/camera", "ExampleCamera");
        Camera* camera = cameraEns.IsValid() ? cameraEns.GetComponent<Camera>() : nullptr;
        if (!camera && cameraEns.IsValid())
        {
            camera = cameraEns.AddComponent<Camera>();
        }
        if (camera)
        {
            camera->fieldOfView = 60.0f;
            camera->nearPlane = 0.1f;
            camera->farPlane = 100.0f;
            camera->clearMode = ClearMode::SolidColor;
            camera->clearColor = { 0.62f, 0.78f, 0.96f, 1.0f };
        }
        if (SpaceComponent* space = cameraEns.Space())
        {
            space->localPosition = { 5.0f, 3.2f, 7.0f };
            space->localRotation = MakeYawPitchRotation(35.0f, -22.0f);
        }

        Skybox* skybox = GetOrCreateWorldObject<Skybox>(world, "world://examples/world/skybox");
        if (skybox)
        {
            skybox->right.Set(skyTexture);
            skybox->left.Set(skyTexture);
            skybox->top.Set(skyTexture);
            skybox->bottom.Set(skyTexture);
            skybox->front.Set(skyTexture);
            skybox->back.Set(skyTexture);
            world.renderSettings.skybox.Set(skybox);
            world.renderSettings.skyboxEnabled = true;
            world.renderSettings.ambientColor = { 0.12f, 0.14f, 0.16f, 1.0f };
        }

        gCameraSystem.SetCamera(cameraEns.GetEns(), 35.0f, -22.0f);
        app.RegisterSystem(&gCameraSystem);
    }
}
