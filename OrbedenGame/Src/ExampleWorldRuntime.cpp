#include "Application.h"
#include "Log/Log.h"
#include "Platform/InputManager.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/MaterialShader.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Skybox.h"
#include "Runtime/Object/SpaceComponent.h"
#include "Runtime/Object/Texture2D.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/World.h"

#include <cmath>
#include <string>

namespace
{
    constexpr const char* CubeMeshKey = "Resources/Examples/World/Meshes/cube.obj//Mesh/Main";
    constexpr const char* GroundMeshKey = "Resources/Examples/World/Meshes/ground.obj//Mesh/Main";
    constexpr const char* ExampleShaderKey = "Resources/Examples/World/Shaders/blinn_phong_shadow";
    constexpr const char* SkyTextureKey = "Resources/Examples/World/Textures/sky_blue.png";
    constexpr const char* CameraId = "world://examples/world/camera";
    constexpr float32 Pi = 3.14159265358979323846f;

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
    //补齐示例世界运行时环境
    void InitializeExampleWorldRuntime(Application& app)
    {
        World& world = app.GetWorld();

        Mesh* cubeMesh = ResourceManager::Load<Mesh>(CubeMeshKey);
        Mesh* groundMesh = ResourceManager::Load<Mesh>(GroundMeshKey);
        MaterialShader* shader = ResourceManager::Load<MaterialShader>(ExampleShaderKey);
        Texture2D* skyTexture = ResourceManager::Load<Texture2D>(SkyTextureKey);
        if (!cubeMesh || !groundMesh || !shader || !skyTexture)
        {
            Log::Error("Example world runtime setup failed: required resources are missing.");
            return;
        }

        AssignShaderToMeshMaterials(cubeMesh, shader);
        AssignShaderToMeshMaterials(groundMesh, shader);

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

        Ens cameraEns = world.FindEns(StringId(CameraId));
        if (!cameraEns.IsValid())
        {
            Log::Error("Example world runtime setup failed: camera Ens is missing.");
            return;
        }

        gCameraSystem.SetCamera(cameraEns.GetEns(), 35.0f, -22.0f);
        app.RegisterSystem(&gCameraSystem);
    }
}
