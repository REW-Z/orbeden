#include <functional>
#include "Physics/PhysicsSystem.h"
#include "Runtime/LayerSettings.h"

#include "Log/Log.h"
#include "Profiler/Profiler.h"
#include "Runtime/Object/CharacterController.h"
#include "Runtime/Object/Collider.h"
#include "Runtime/Object/HeightField.h"
#include "Runtime/Object/RigidBody.h"
#include "Runtime/Object/WheelCollider.h"
#include "Rendering/TransformCache.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/Ens.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Transform.h"

#include "PxPhysicsAPI.h"
#include "characterkinematic/PxBoxController.h"
#include "characterkinematic/PxCapsuleController.h"
#include "characterkinematic/PxControllerManager.h"
#include "cooking/PxCooking.h"
#include "extensions/PxDefaultAllocator.h"
#include "extensions/PxDefaultCpuDispatcher.h"
#include "extensions/PxExtensionsAPI.h"
#include "extensions/PxRigidBodyExt.h"

#include <algorithm>
#include <string>
#include <bit>
#include <cmath>
#include <memory>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

using namespace physx;

namespace
{
    constexpr float32 MinimumDimension = 0.001f;
    constexpr float32 Pi = 3.14159265358979323846f;
    constexpr uint32 BindingMagic = 0x4F524250u;

    //当前 Application 拥有的 PhysicsSystem，供游戏模块访问。
    PhysicsSystem* currentPhysicsSystem = nullptr;

    //机轮模拟使用的向量工具。
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

    float32 Dot(const vector3& a, const vector3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    vector3 Rotate(const quaternion& rotation, const vector3& value)
    {
        vector3 axis{ rotation.x, rotation.y, rotation.z };
        vector3 doubledCross = Scale(Cross(axis, value), 2.0f);
        return Add(value, Add(Scale(doubledCross, rotation.w), Cross(axis, doubledCross)));
    }

    vector3 NormalizeSafe(const vector3& value, const vector3& fallback)
    {
        float32 length = std::sqrt(Dot(value, value));
        return length > 0.0001f ? Scale(value, 1.0f / length) : fallback;
    }

    //绕单位轴旋转向量。
    vector3 RotateAxis(const vector3& value, const vector3& axis, float32 angle)
    {
        float32 cosine = std::cos(angle);
        float32 sine = std::sin(angle);
        return Add(Add(Scale(value, cosine), Scale(Cross(axis, value), sine)), Scale(axis, Dot(axis, value) * (1.0f - cosine)));
    }

    uint64 EnsKey(EnsId ens)
    {
        return (static_cast<uint64>(ens.version) << 32) | ens.id;
    }

    PxVec3 ToPx(const vector3& value)
    {
        return PxVec3(value.x, value.y, value.z);
    }

    vector3 FromPx(const PxVec3& value)
    {
        return { value.x, value.y, value.z };
    }

    PxQuat ToPx(const quaternion& value)
    {
        PxQuat result(value.x, value.y, value.z, value.w);
        return result.magnitudeSquared() > 0.000001f ? result.getNormalized() : PxQuat(PxIdentity);
    }

    quaternion FromPx(const PxQuat& value)
    {
        return { value.x, value.y, value.z, value.w };
    }

    bool NearlyEqual(const vector3& a, const vector3& b)
    {
        constexpr float32 epsilon = 0.00001f;
        return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon && std::abs(a.z - b.z) <= epsilon;
    }

    bool NearlyEqual(const quaternion& a, const quaternion& b)
    {
        constexpr float32 epsilon = 0.00001f;
        float32 direct = std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z) + std::abs(a.w - b.w);
        float32 negated = std::abs(a.x + b.x) + std::abs(a.y + b.y) + std::abs(a.z + b.z) + std::abs(a.w + b.w);
        return std::min(direct, negated) <= epsilon * 4.0f;
    }

    vector3 GetWorldScale(const Transform& transform)
    {
        //根节点直接使用局部缩放，避免旋转矩阵的舍入误差进入碰撞形状尺寸。
        if (transform.parent.IsNull())
        {
            const vector3& scale = transform.GetLocalScale();
            return { std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) };
        }
        const float32* m = transform.worldMatrix.m;
        return
        {
            std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]),
            std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]),
            std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]),
        };
    }

    float32 Positive(float32 value)
    {
        return std::max(std::abs(value), MinimumDimension);
    }

    uint64 MixHash(uint64 hash, uint64 value)
    {
        hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
        return hash;
    }

    uint64 MixFloat(uint64 hash, float32 value)
    {
        return MixHash(hash, std::bit_cast<uint32>(value));
    }

    uint64 MixVector(uint64 hash, const vector3& value)
    {
        hash = MixFloat(hash, value.x);
        hash = MixFloat(hash, value.y);
        return MixFloat(hash, value.z);
    }

    PxFilterFlags OrbedenSimulationFilterShader(
        PxFilterObjectAttributes attributes0, PxFilterData filterData0,
        PxFilterObjectAttributes attributes1, PxFilterData filterData1,
        PxPairFlags& pairFlags, const void*, PxU32)
    {
        if ((filterData0.word0 & filterData1.word1) == 0 || (filterData1.word0 & filterData0.word1) == 0)
        {
            return PxFilterFlag::eSUPPRESS;
        }

        if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
        {
            pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
        }
        else
        {
            pairFlags = PxPairFlag::eCONTACT_DEFAULT |
                PxPairFlag::eDETECT_CCD_CONTACT |
                PxPairFlag::eNOTIFY_TOUCH_FOUND |
                PxPairFlag::eNOTIFY_TOUCH_PERSISTS |
                PxPairFlag::eNOTIFY_TOUCH_LOST |
                PxPairFlag::eNOTIFY_CONTACT_POINTS;
        }
        return PxFilterFlag::eDEFAULT;
    }

    class LayerQueryFilter final : public PxQueryFilterCallback
    {
    private:
        uint32 layerMask;
        uint32 queryLayer;
        bool returnTouch;
        //额外的过滤条件：排除指定 Ens 与触发器
        EnsId ignoredEns;
        bool includeTriggers = true;
        //将形状或 Actor 解析回 Ens，为空时跳过 ignoredEns 判定
        std::function<EnsId(const PxShape*, const PxActor*)> resolveEns;
        const PxRigidActor* ignoredActor = nullptr;

    public:
        LayerQueryFilter(uint32 mask, bool touch, uint32 layer = 0,
            const PxRigidActor* ignored = nullptr, bool triggers = true)
            : layerMask(mask), queryLayer(layer), returnTouch(touch), includeTriggers(triggers), ignoredActor(ignored)
        {
        }

        LayerQueryFilter(uint32 mask, bool touch, const PhysicsQueryFilter& extra,
            std::function<EnsId(const PxShape*, const PxActor*)> resolver)
            : layerMask(mask), queryLayer(0), returnTouch(touch), ignoredEns(extra.ignoredEns),
            includeTriggers(extra.includeTriggers), resolveEns(std::move(resolver))
        {
        }

        PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* shape, const PxRigidActor* actor, PxHitFlags&) override
        {
            if (!shape) return PxQueryHitType::eNONE;
            if (ignoredActor && actor == ignoredActor) return PxQueryHitType::eNONE;
            //触发器默认不参与实体碰撞
            if (!includeTriggers && (shape->getFlags() & PxShapeFlag::eTRIGGER_SHAPE)) return PxQueryHitType::eNONE;
            if (!ignoredEns.IsNull() && resolveEns
                && (resolveEns(shape, actor) == ignoredEns || resolveEns(nullptr, actor) == ignoredEns)) return PxQueryHitType::eNONE;
            PxFilterData shapeFilter = shape->getQueryFilterData();
            if ((shapeFilter.word0 & layerMask) == 0) return PxQueryHitType::eNONE;
            if (queryLayer != 0 && (shapeFilter.word1 & queryLayer) == 0) return PxQueryHitType::eNONE;
            return returnTouch ? PxQueryHitType::eTOUCH : PxQueryHitType::eBLOCK;
        }

        PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&, const PxShape*, const PxRigidActor*) override
        {
            return returnTouch ? PxQueryHitType::eTOUCH : PxQueryHitType::eBLOCK;
        }
    };

    class ControllerPairFilter final : public PxControllerFilterCallback
    {
    public:
        bool filter(const PxController& first, const PxController& second) override
        {
            PxShape* firstShape = nullptr;
            PxShape* secondShape = nullptr;
            if (first.getActor()->getShapes(&firstShape, 1) != 1 || second.getActor()->getShapes(&secondShape, 1) != 1) return true;
            PxFilterData firstFilter = firstShape->getSimulationFilterData();
            PxFilterData secondFilter = secondShape->getSimulationFilterData();
            return (firstFilter.word0 & secondFilter.word1) != 0 && (secondFilter.word0 & firstFilter.word1) != 0;
        }
    };
}

class PhysicsSystem::Impl : private IObjectDestroyListener
{
public:
    enum class BindingKind : uint32
    {
        Body,
        Controller,
    };

    struct NativeBinding
    {
        uint32 magic = BindingMagic;
        BindingKind kind = BindingKind::Body;
        EnsId ens;
        Impl* owner = nullptr;
    };

    struct ShapeRecord
    {
        NativeBinding binding;
        PxShape* shape = nullptr;
        Mesh* sourceMesh = nullptr;
        bool sourceMeshInvalidated = false;
        bool seen = false;
        uint64 configurationHash = 0;
        uint64 poseHash = 0;
        vector3 scale;
        PxTransform localPose = PxTransform(PxIdentity);
        PxFilterData filter;
    };

    struct BodyRecord
    {
        NativeBinding binding;
        PxRigidActor* actor = nullptr;
        uint64 configurationHash = 0;
        PhysicsBodyType bodyType = PhysicsBodyType::Static;
        std::unordered_map<int32, std::unique_ptr<ShapeRecord>> shapes;
        vector3 lastPosition;
        quaternion lastRotation;
        bool lastPoseValid = false;
    };

    struct BodyInput
    {
        Ens* ens = nullptr;
        RigidBody* body = nullptr;
        List<std::pair<Collider*, uint64>> colliders;
    };

    struct HeightFieldRecord
    {
        NativeBinding binding;
        PxRigidActor* actor = nullptr;
        PxHeightField* heightField = nullptr;
        uint64 configurationHash = 0;
        uint32 layerMask = 1u;
        uint32 collisionMask = 0xFFFFFFFFu;
        uint32 generation = 0;
    };

    struct ControllerRecord
    {
        NativeBinding binding;
        PxController* controller = nullptr;
        uint64 configurationHash = 0;
        PxFilterData filter;
        vector3 lastFootPosition;
        bool lastPoseValid = false;
    };

    class ErrorCallback final : public PxErrorCallback
    {
    public:
        void reportError(PxErrorCode::Enum code, const char* message, const char*, int) override
        {
            if (code == PxErrorCode::eDEBUG_INFO)
            {
                Log::Info(message);
            }
            else if (code == PxErrorCode::eDEBUG_WARNING || code == PxErrorCode::ePERF_WARNING)
            {
                Log::Warning(message);
            }
            else
            {
                Log::Error(message);
            }
        }
    };

    class SimulationEvents final : public PxSimulationEventCallback
    {
    public:
        Impl* owner = nullptr;

        void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
        void onWake(PxActor**, PxU32) override {}
        void onSleep(PxActor**, PxU32) override {}
        void onAdvance(const PxRigidBody* const*, const PxTransform*, PxU32) override {}

        void onContact(const PxContactPairHeader& header, const PxContactPair* pairs, PxU32 pairCount) override
        {
            if (!owner) return;
            if (header.flags & (PxContactPairHeaderFlag::eREMOVED_ACTOR_0 | PxContactPairHeaderFlag::eREMOVED_ACTOR_1)) return;
            EnsId first = owner->FindEns(header.actors[0]);
            EnsId second = owner->FindEns(header.actors[1]);
            if (first.IsNull() || second.IsNull()) return;

            for (PxU32 index = 0; index < pairCount; index++)
            {
                const PxContactPair& pair = pairs[index];
                PxContactPairPoint point;
                bool hasPoint = pair.extractContacts(&point, 1) > 0;

                if (pair.events & PxPairFlag::eNOTIFY_TOUCH_FOUND)
                {
                    owner->PushEvent(PhysicsEventType::ContactEnter, first, second, hasPoint ? &point : nullptr);
                }
                if (pair.events & PxPairFlag::eNOTIFY_TOUCH_PERSISTS)
                {
                    owner->PushEvent(PhysicsEventType::ContactStay, first, second, hasPoint ? &point : nullptr);
                }
                if (pair.events & PxPairFlag::eNOTIFY_TOUCH_LOST)
                {
                    owner->PushEvent(PhysicsEventType::ContactExit, first, second, nullptr);
                }
            }
        }

        void onTrigger(PxTriggerPair* pairs, PxU32 count) override
        {
            if (!owner) return;
            for (PxU32 index = 0; index < count; index++)
            {
                const PxTriggerPair& pair = pairs[index];
                if (pair.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER)) continue;

                EnsId first = owner->FindEns(pair.triggerActor);
                EnsId second = owner->FindEns(pair.otherActor);
                if (first.IsNull() || second.IsNull()) continue;

                if (pair.status & PxPairFlag::eNOTIFY_TOUCH_FOUND)
                {
                    owner->PushEvent(PhysicsEventType::TriggerEnter, first, second, nullptr);
                }
                if (pair.status & PxPairFlag::eNOTIFY_TOUCH_LOST)
                {
                    owner->PushEvent(PhysicsEventType::TriggerExit, first, second, nullptr);
                }
            }
        }
    };

    PxDefaultAllocator allocator;
    ErrorCallback errorCallback;
    SimulationEvents simulationEvents;
    PxFoundation* foundation = nullptr;
    PxPhysics* physics = nullptr;
    PxScene* scene = nullptr;
    PxDefaultCpuDispatcher* dispatcher = nullptr;
    PxControllerManager* controllerManager = nullptr;
    std::unique_ptr<PxCookingParams> cookingParams;
    std::unordered_map<uint64, std::unique_ptr<BodyRecord>> bodies;
    std::unordered_map<uint64, BodyInput> bodyInputs;
    std::unordered_map<uint64, EnsId> attachedBodies;
    List<std::tuple<EnsId, RigidBody*, uint64>> bodyTraversal;
    //已经报过「static 被物理驱动」的 Ens，按对象去重
    std::unordered_set<uint64> reportedStaticDrives;
    std::unordered_map<uint64, std::unique_ptr<HeightFieldRecord>> heightFields;
    std::unordered_map<uint64, std::unique_ptr<ControllerRecord>> controllers;
    std::unordered_map<Mesh*, PxConvexMesh*> convexMeshes;
    std::unordered_map<Mesh*, PxTriangleMesh*> triangleMeshes;
    List<PhysicsEvent> events;
    TransformCache transformCache;
    World* world = nullptr;
    bool extensionsInitialized = false;

    Impl()
    {
        simulationEvents.owner = this;
    }

    bool Initialize()
    {
        if (physics) return true;

        foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
        if (!foundation)
        {
            Log::Error("PhysX Foundation initialization failed.");
            return false;
        }

        PxTolerancesScale scale;
        physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, scale, false, nullptr, nullptr);
        if (!physics)
        {
            Log::Error("PhysX CPU SDK initialization failed.");
            Shutdown();
            return false;
        }

        extensionsInitialized = PxInitExtensions(*physics, nullptr);
        if (!extensionsInitialized)
        {
            Log::Error("PhysX Extensions initialization failed.");
            Shutdown();
            return false;
        }

        uint32 hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
        uint32 workerThreads = std::clamp(hardwareThreads > 1 ? hardwareThreads - 1 : 1u, 1u, 8u);
        dispatcher = PxDefaultCpuDispatcherCreate(workerThreads);
        if (!dispatcher)
        {
            Log::Error("PhysX CPU dispatcher initialization failed.");
            Shutdown();
            return false;
        }

        PxSceneDesc sceneDesc(scale);
        sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
        sceneDesc.cpuDispatcher = dispatcher;
        sceneDesc.filterShader = OrbedenSimulationFilterShader;
        sceneDesc.simulationEventCallback = &simulationEvents;
        sceneDesc.flags |= PxSceneFlag::eENABLE_CCD;
        scene = physics->createScene(sceneDesc);
        if (!scene)
        {
            Log::Error("PhysX Scene initialization failed.");
            Shutdown();
            return false;
        }

        cookingParams = std::make_unique<PxCookingParams>(scale);
        controllerManager = PxCreateControllerManager(*scene, false);
        if (!controllerManager)
        {
            Log::Error("PhysX character controller manager initialization failed.");
            Shutdown();
            return false;
        }

        Object::AddDestroyListener(this);
        Log::Info("PhysX 5 CPU-only physics initialized.");
        return true;
    }

    void Shutdown()
    {
        Object::RemoveDestroyListener(this);
        ResetWorld();

        for (auto& entry : convexMeshes) entry.second->release();
        convexMeshes.clear();
        for (auto& entry : triangleMeshes) entry.second->release();
        triangleMeshes.clear();
        cookingParams.reset();

        if (controllerManager)
        {
            controllerManager->release();
            controllerManager = nullptr;
        }
        if (scene)
        {
            scene->release();
            scene = nullptr;
        }
        if (dispatcher)
        {
            dispatcher->release();
            dispatcher = nullptr;
        }
        if (extensionsInitialized)
        {
            PxCloseExtensions();
            extensionsInitialized = false;
        }
        if (physics)
        {
            physics->release();
            physics = nullptr;
        }
        if (foundation)
        {
            foundation->release();
            foundation = nullptr;
        }
    }

    void ResetWorld()
    {
        for (auto& entry : controllers) DestroyController(*entry.second);
        controllers.clear();
        for (auto& entry : bodies) DestroyBody(*entry.second);
        bodies.clear();
        bodyInputs.clear();
        attachedBodies.clear();
        bodyTraversal.clear();
        for (auto& entry : heightFields) DestroyHeightField(*entry.second);
        heightFields.clear();
        for (auto& entry : convexMeshes) entry.second->release();
        convexMeshes.clear();
        for (auto& entry : triangleMeshes) entry.second->release();
        triangleMeshes.clear();
        events.clear();
        world = nullptr;
    }

    NativeBinding* FindBinding(const PxActor* actor) const
    {
        if (!actor || !actor->userData) return nullptr;
        NativeBinding* binding = static_cast<NativeBinding*>(actor->userData);
        return binding->magic == BindingMagic && binding->owner == this ? binding : nullptr;
    }

    EnsId FindEns(const PxActor* actor) const
    {
        NativeBinding* binding = FindBinding(actor);
        return binding ? binding->ens : EnsId();
    }

    //读取碰撞形状所属节点，特殊形状回退到 Actor 绑定
    EnsId FindEns(const PxShape* shape, const PxActor* actor) const
    {
        NativeBinding* binding = shape ? static_cast<NativeBinding*>(shape->userData) : nullptr;
        if (binding && binding->magic == BindingMagic && binding->owner == this)
        {
            return binding->ens;
        }
        return FindEns(actor);
    }

    //查找节点归属的刚体记录
    BodyRecord* FindBody(EnsId ens) const
    {
        auto attached = attachedBodies.find(EnsKey(ens));
        if (attached != attachedBodies.end() && !attached->second.IsNull())
        {
            ens = attached->second;
        }
        else if (attached == attachedBodies.end() && world)
        {
            for (Ens* current = world->GetEns(ens); current; current = world->GetParent(current->GetId()))
            {
                if (current->GetComponent<RigidBody>())
                {
                    ens = current->GetId();
                    break;
                }
            }
        }
        auto found = bodies.find(EnsKey(ens));
        return found != bodies.end() ? found->second.get() : nullptr;
    }

    void PushEvent(PhysicsEventType type, EnsId first, EnsId second, const PxContactPairPoint* contact)
    {
        PhysicsEvent event;
        event.type = type;
        event.first = first;
        event.second = second;
        if (contact)
        {
            event.position = FromPx(contact->position);
            event.normal = FromPx(contact->normal);
            event.impulse = contact->impulse.magnitude();
        }
        events.push_back(event);
    }

    Mesh* GetColliderMesh(const Collider& collider) const
    {
        if (const ConvexMeshCollider* convex = collider.Cast<ConvexMeshCollider>()) return convex->mesh.Get();
        if (const TriangleMeshCollider* triangle = collider.Cast<TriangleMeshCollider>()) return triangle->mesh.Get();
        return nullptr;
    }

    uint64 CalculateColliderHash(const Collider& collider) const
    {
        uint64 hash = 0xCBF29CE484222325ull;
        hash = MixHash(hash, static_cast<uint32>(collider.GetGeometryType()));
        hash = MixHash(hash, collider.isTrigger);
        hash = MixFloat(hash, collider.staticFriction);
        hash = MixFloat(hash, collider.dynamicFriction);
        hash = MixFloat(hash, collider.restitution);

        if (const BoxCollider* box = collider.Cast<BoxCollider>())
        {
            hash = MixVector(hash, box->halfExtents);
        }
        else if (const SphereCollider* sphere = collider.Cast<SphereCollider>())
        {
            hash = MixFloat(hash, sphere->radius);
        }
        else if (const CapsuleCollider* capsule = collider.Cast<CapsuleCollider>())
        {
            hash = MixFloat(hash, capsule->radius);
            hash = MixFloat(hash, capsule->halfHeight);
        }
        else if (Mesh* mesh = GetColliderMesh(collider))
        {
            hash = MixHash(hash, mesh->GetInstanceId().GetHash());
        }
        return hash;
    }

    uint64 CalculateBodyHash(const RigidBody* body) const
    {
        uint64 hash = 0xCBF29CE484222325ull;
        PhysicsBodyType type = body ? body->bodyType : PhysicsBodyType::Static;
        hash = MixHash(hash, static_cast<uint32>(type));
        if (body)
        {
            hash = MixFloat(hash, body->mass);
            hash = MixHash(hash, body->useGravity);
            hash = MixFloat(hash, body->linearDamping);
            hash = MixFloat(hash, body->angularDamping);
            hash = MixHash(hash, body->continuousCollisionDetection);
            hash = MixHash(hash, body->lockFlags);
        }
        return hash;
    }

    PxConvexMesh* GetConvexMesh(Mesh& mesh)
    {
        auto found = convexMeshes.find(&mesh);
        if (found != convexMeshes.end()) return found->second;
        if (mesh.vertices.size() < 4) return nullptr;

        List<PxVec3> vertices;
        vertices.reserve(mesh.vertices.size());
        for (const vector3& vertex : mesh.vertices) vertices.push_back(ToPx(vertex));

        PxConvexMeshDesc desc;
        desc.points.count = static_cast<PxU32>(vertices.size());
        desc.points.stride = sizeof(PxVec3);
        desc.points.data = vertices.data();
        desc.flags = PxConvexFlag::eCOMPUTE_CONVEX;

        PxConvexMeshCookingResult::Enum result;
        PxConvexMesh* cooked = PxCreateConvexMesh(*cookingParams, desc, physics->getPhysicsInsertionCallback(), &result);
        if (!cooked) return nullptr;
        convexMeshes.emplace(&mesh, cooked);
        return cooked;
    }

    PxTriangleMesh* GetTriangleMesh(Mesh& mesh)
    {
        auto found = triangleMeshes.find(&mesh);
        if (found != triangleMeshes.end()) return found->second;
        if (mesh.vertices.size() < 3 || mesh.indices.size() < 3 || mesh.indices.size() % 3 != 0) return nullptr;

        List<PxVec3> vertices;
        vertices.reserve(mesh.vertices.size());
        for (const vector3& vertex : mesh.vertices) vertices.push_back(ToPx(vertex));

        PxTriangleMeshDesc desc;
        desc.points.count = static_cast<PxU32>(vertices.size());
        desc.points.stride = sizeof(PxVec3);
        desc.points.data = vertices.data();
        desc.triangles.count = static_cast<PxU32>(mesh.indices.size() / 3);
        desc.triangles.stride = sizeof(uint32) * 3;
        desc.triangles.data = mesh.indices.data();

        PxTriangleMeshCookingResult::Enum result;
        PxTriangleMesh* cooked = PxCreateTriangleMesh(*cookingParams, desc, physics->getPhysicsInsertionCallback(), &result);
        if (!cooked) return nullptr;
        triangleMeshes.emplace(&mesh, cooked);
        return cooked;
    }

    //释放 Mesh 烘焙网格
    void InvalidateCookedMeshes(Mesh& mesh)
    {
        auto convex = convexMeshes.find(&mesh);
        if (convex != convexMeshes.end())
        {
            convex->second->release();
            convexMeshes.erase(convex);
        }

        auto triangle = triangleMeshes.find(&mesh);
        if (triangle != triangleMeshes.end())
        {
            triangle->second->release();
            triangleMeshes.erase(triangle);
        }
    }

    //处理 Mesh 销毁事件
    void OnObjectDestroyed(Object* object) override
    {
        if (!object || !object->Is(Mesh::StaticType())) return;

        Mesh* mesh = static_cast<Mesh*>(object);
        InvalidateCookedMeshes(*mesh);
        for (auto& entry : bodies)
        {
            for (auto& shape : entry.second->shapes)
            {
                if (shape.second->sourceMesh == mesh)
                {
                    shape.second->sourceMeshInvalidated = true;
                }
            }
        }
    }

    PxShape* CreateShape(const Collider& collider, PhysicsBodyType bodyType, const vector3& scale,
        uint32 layerMask, uint32 collisionMask)
    {
        PxMaterial* material = physics->createMaterial(
            std::max(0.0f, collider.staticFriction),
            std::max(0.0f, collider.dynamicFriction),
            std::clamp(collider.restitution, 0.0f, 1.0f));
        if (!material) return nullptr;

        PxShapeFlags flags = PxShapeFlag::eSCENE_QUERY_SHAPE;
        flags |= collider.isTrigger ? PxShapeFlag::eTRIGGER_SHAPE : PxShapeFlag::eSIMULATION_SHAPE;
        PxShape* shape = nullptr;

        ColliderGeometryType geometryType = collider.GetGeometryType();
        if (const BoxCollider* box = collider.Cast<BoxCollider>())
        {
            PxBoxGeometry geometry(
                Positive(box->halfExtents.x * scale.x),
                Positive(box->halfExtents.y * scale.y),
                Positive(box->halfExtents.z * scale.z));
            shape = physics->createShape(geometry, *material, true, flags);
        }
        else if (const SphereCollider* sphere = collider.Cast<SphereCollider>())
        {
            float32 uniformScale = std::max({ Positive(scale.x), Positive(scale.y), Positive(scale.z) });
            shape = physics->createShape(PxSphereGeometry(Positive(sphere->radius * uniformScale)), *material, true, flags);
        }
        else if (const CapsuleCollider* capsule = collider.Cast<CapsuleCollider>())
        {
            float32 radialScale = std::max(Positive(scale.x), Positive(scale.z));
            PxCapsuleGeometry geometry(Positive(capsule->radius * radialScale), Positive(capsule->halfHeight * scale.y));
            shape = physics->createShape(geometry, *material, true, flags);
        }
        else if (Mesh* mesh = GetColliderMesh(collider))
        {
            if (geometryType == ColliderGeometryType::ConvexMesh)
            {
                PxConvexMesh* convex = GetConvexMesh(*mesh);
                if (convex)
                {
                    PxConvexMeshGeometry geometry(convex, PxMeshScale(ToPx(scale)));
                    shape = physics->createShape(geometry, *material, true, flags);
                }
            }
            else if (geometryType == ColliderGeometryType::TriangleMesh)
            {
                if (bodyType == PhysicsBodyType::Dynamic)
                {
                    Log::Warning("Dynamic rigid bodies cannot use TriangleMesh colliders; use ConvexMesh.");
                }
                else
                {
                    PxTriangleMesh* triangle = GetTriangleMesh(*mesh);
                    if (triangle)
                    {
                        PxTriangleMeshGeometry geometry(triangle, PxMeshScale(ToPx(scale)));
                        shape = physics->createShape(geometry, *material, true, flags);
                    }
                }
            }
        }
        else if (geometryType == ColliderGeometryType::ConvexMesh || geometryType == ColliderGeometryType::TriangleMesh)
        {
            Log::Warning("Mesh collider has no loaded Mesh resource.");
        }

        material->release();
        if (!shape) return nullptr;

        //缓存所属层、有效掩码和组件原始掩码
        PxFilterData filter(layerMask, collider.collisionMask & collisionMask, collider.collisionMask, 0);
        shape->setSimulationFilterData(filter);
        shape->setQueryFilterData(filter);
        return shape;
    }

    //创建所属节点的刚体，碰撞形状由独立同步路径挂载
    std::unique_ptr<BodyRecord> CreateBody(Ens& ens, RigidBody* body, uint64 configurationHash)
    {
        Transform& transform = *ens.Transform();
        PhysicsBodyType bodyType = body ? body->bodyType : PhysicsBodyType::Static;
        if (bodyType != PhysicsBodyType::Static && ens.GetStatic())
        {
            ReportStaticDrive(ens.GetId());
            bodyType = PhysicsBodyType::Static;
        }
        if (bodyType == PhysicsBodyType::Dynamic && !transform.parent.IsNull())
        {
            Log::Warning(("Dynamic rigid bodies must be root entities; the body was skipped (" + ens.GetName() + ").").c_str());
            return nullptr;
        }

        std::unique_ptr<BodyRecord> record = std::make_unique<BodyRecord>();
        record->binding.kind = BindingKind::Body;
        record->binding.ens = ens.GetId();
        record->binding.owner = this;
        record->configurationHash = configurationHash;
        record->bodyType = bodyType;
        PxTransform pose(ToPx(transform.worldPosition), ToPx(transform.worldRotation));
        PxRigidActor* actor = bodyType == PhysicsBodyType::Static
            ? static_cast<PxRigidActor*>(physics->createRigidStatic(pose))
            : static_cast<PxRigidActor*>(physics->createRigidDynamic(pose));
        if (!actor) return nullptr;
        actor->userData = &record->binding;
        record->actor = actor;

        if (bodyType != PhysicsBodyType::Static)
        {
            PxRigidDynamic* dynamic = static_cast<PxRigidDynamic*>(actor);
            bool kinematic = bodyType == PhysicsBodyType::Kinematic;
            dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, kinematic);
            dynamic->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, body && !body->useGravity);
            dynamic->setLinearDamping(body ? std::max(0.0f, body->linearDamping) : 0.05f);
            dynamic->setAngularDamping(body ? std::max(0.0f, body->angularDamping) : 0.05f);
            dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, body && body->continuousCollisionDetection && !kinematic);
            dynamic->setRigidDynamicLockFlags(PxRigidDynamicLockFlags(body ? static_cast<PxU8>(body->lockFlags & 0x3Fu) : 0));
            if (!kinematic)
            {
                float32 mass = body ? Positive(body->mass) : 1.0f;
                dynamic->setMass(mass);
                dynamic->setMassSpaceInertiaTensor(PxVec3(mass));
                dynamic->setLinearVelocity(body ? ToPx(body->linearVelocity) : PxVec3(0.0f), false);
                dynamic->setAngularVelocity(body ? ToPx(body->angularVelocity) : PxVec3(0.0f), false);
            }
        }
        scene->addActor(*actor);
        record->lastPosition = transform.worldPosition;
        record->lastRotation = transform.worldRotation;
        record->lastPoseValid = true;
        return record;
    }

    //摘除形状并释放组件绑定
    void DestroyShape(BodyRecord& body, ShapeRecord& shape)
    {
        shape.shape->userData = nullptr;
        body.actor->detachShape(*shape.shape);
        shape.shape->release();
        shape.shape = nullptr;
    }

    //同步复合刚体的各个碰撞形状
    void SyncBodyShapes(BodyRecord& record, const BodyInput& input,
        const std::unordered_set<Mesh*>& dirtyMeshes, bool queryOnly)
    {
        for (auto& entry : record.shapes)
        {
            entry.second->seen = false;
        }
        Transform& bodyTransform = *input.ens->Transform();
        PxTransform inverseBody = PxTransform(ToPx(bodyTransform.worldPosition), ToPx(bodyTransform.worldRotation)).getInverse();
        bool massChanged = false;
        bool filtersChanged = false;
        for (auto [collider, poseHash] : input.colliders)
        {
            Ens& ens = *collider->GetEns();
            Transform& transform = *ens.Transform();
            int32 id = collider->GetObjectId();
            uint64 hash = CalculateColliderHash(*collider);
            vector3 scale = GetWorldScale(transform);
            Mesh* mesh = GetColliderMesh(*collider);
            auto found = record.shapes.find(id);
            if (found != record.shapes.end() && (found->second->configurationHash != hash
                || !NearlyEqual(found->second->scale, scale) || found->second->sourceMesh != mesh
                || found->second->sourceMeshInvalidated || dirtyMeshes.contains(mesh)))
            {
                DestroyShape(record, *found->second);
                record.shapes.erase(found);
                found = record.shapes.end();
                massChanged = true;
            }

            //保留碰撞体自身的层与过滤掩码
            uint32 layerMask = ens.GetLayerMask();
            uint32 collisionMask = LayerSettings::GetCollisionMask(ens.GetLayer());
            PxFilterData filter(layerMask, collider->collisionMask & collisionMask, collider->collisionMask, 0);
            bool created = found == record.shapes.end();
            if (created)
            {
                PxShape* shape = CreateShape(*collider, record.bodyType, scale, layerMask, collisionMask);
                if (!shape)
                {
                    Log::Warning("PhysX collider geometry creation failed; the shape was skipped.");
                    continue;
                }
                std::unique_ptr<ShapeRecord> entry = std::make_unique<ShapeRecord>();
                entry->binding.ens = ens.GetId();
                entry->binding.owner = this;
                entry->shape = shape;
                entry->sourceMesh = mesh;
                entry->configurationHash = hash;
                entry->scale = scale;
                entry->filter = filter;
                shape->userData = &entry->binding;
                record.actor->attachShape(*shape);
                found = record.shapes.emplace(id, std::move(entry)).first;
                massChanged = true;
            }
            ShapeRecord& entry = *found->second;
            entry.seen = true;
            if (entry.filter.word0 != filter.word0 || entry.filter.word1 != filter.word1 || entry.filter.word2 != filter.word2)
            {
                entry.shape->setSimulationFilterData(filter);
                entry.shape->setQueryFilterData(filter);
                entry.filter = filter;
                filtersChanged = true;
            }

            //仅在局部变换链或中心变化时换算相对位姿
            uint64 localPoseHash = poseHash ? MixVector(poseHash, collider->center) : 0;
            if (created || localPoseHash == 0 || entry.poseHash != localPoseHash)
            {
                vector3 center = RenderMath::TransformPoint(transform.worldMatrix, collider->center);
                PxQuat rotation = ToPx(transform.worldRotation);
                if (collider->GetGeometryType() == ColliderGeometryType::Capsule)
                {
                    rotation = rotation * PxQuat(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f));
                }
                PxTransform localPose = inverseBody * PxTransform(ToPx(center), rotation);
                if (created || !NearlyEqual(FromPx(entry.localPose.p), FromPx(localPose.p))
                    || !NearlyEqual(FromPx(entry.localPose.q), FromPx(localPose.q)))
                {
                    entry.shape->setLocalPose(localPose);
                    entry.localPose = localPose;
                    massChanged = true;
                }
                entry.poseHash = localPoseHash;
            }
        }
        for (auto iterator = record.shapes.begin(); iterator != record.shapes.end();)
        {
            if (iterator->second->seen)
            {
                ++iterator;
                continue;
            }
            DestroyShape(record, *iterator->second);
            iterator = record.shapes.erase(iterator);
            massChanged = true;
        }
        if (filtersChanged)
        {
            scene->resetFiltering(*record.actor);
        }

        //按实体形状更新质量分布，纯悬挂刚体按质量设置惯量
        if (massChanged && record.bodyType == PhysicsBodyType::Dynamic)
        {
            PxRigidDynamic* dynamic = static_cast<PxRigidDynamic*>(record.actor);
            float32 mass = input.body ? Positive(input.body->mass) : 1.0f;
            bool hasSimulationShape = false;
            for (const auto& entry : record.shapes)
            {
                hasSimulationShape |= entry.second->shape->getFlags().isSet(PxShapeFlag::eSIMULATION_SHAPE);
            }
            if (!hasSimulationShape || !PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, mass))
            {
                dynamic->setMass(mass);
                dynamic->setCMassLocalPose(PxTransform(PxIdentity));
                dynamic->setMassSpaceInertiaTensor(PxVec3(mass));
            }
            if (!queryOnly)
            {
                dynamic->wakeUp();
            }
        }
    }

    //释放刚体与其形状绑定
    void DestroyBody(BodyRecord& record)
    {
        if (!record.actor) return;
        for (auto& entry : record.shapes)
        {
            DestroyShape(record, *entry.second);
        }
        record.shapes.clear();
        record.actor->userData = nullptr;
        record.actor->release();
        record.actor = nullptr;
    }

    void DestroyHeightField(HeightFieldRecord& record)
    {
        if (record.actor)
        {
            record.actor->userData = nullptr;
            record.actor->release();
            record.actor = nullptr;
        }
        if (record.heightField)
        {
            record.heightField->release();
            record.heightField = nullptr;
        }
    }

    //计算 HeightField 物理参数哈希。
    uint64 CalculateHeightFieldHash(const HeightField& component) const
    {
        uint64 hash = 0xCBF29CE484222325ull;
        hash = MixHash(hash, static_cast<uint32>(component.seed));
        hash = MixHash(hash, static_cast<uint32>(component.sampleTileX));
        hash = MixHash(hash, static_cast<uint32>(component.sampleTileZ));
        hash = MixFloat(hash, component.flattenBlendDistance);
        hash = MixFloat(hash, component.sizeX);
        hash = MixFloat(hash, component.sizeZ);
        hash = MixHash(hash, static_cast<uint32>(component.rowCount));
        hash = MixHash(hash, static_cast<uint32>(component.columnCount));
        hash = MixFloat(hash, component.amplitude);
        hash = MixFloat(hash, component.frequency);
        hash = MixHash(hash, static_cast<uint32>(component.octaves));
        hash = MixFloat(hash, component.flattenMinX);
        hash = MixFloat(hash, component.flattenMaxX);
        hash = MixFloat(hash, component.flattenMinZ);
        hash = MixFloat(hash, component.flattenMaxZ);
        hash = MixFloat(hash, component.flattenHeight);
        return hash;
    }

    //创建 HeightField 静态碰撞体。
    std::unique_ptr<HeightFieldRecord> CreateHeightField(HeightField& component, Transform& transform,
        uint64 configurationHash, uint32 layerMask, uint32 collisionMask)
    {
        int32 rows = std::max(component.rowCount, 2);
        int32 columns = std::max(component.columnCount, 2);
        const std::vector<float32>& heights = component.GetHeights();
        if (heights.size() != static_cast<size_t>(rows) * columns) return nullptr;

        //同一地形配置使用一致量化尺度，避免相邻块的公共边出现碰撞高度缝隙。
        float32 maxSample = std::max(std::abs(component.amplitude), std::abs(component.flattenHeight));
        for (float32 height : heights) maxSample = std::max(maxSample, std::abs(height));
        float32 heightScale = std::max((maxSample + 0.25f) / 32000.0f, PX_MIN_HEIGHTFIELD_Y_SCALE);

        std::vector<PxHeightFieldSample> samples(heights.size());
        for (size_t index = 0; index < heights.size(); index++)
        {
            float32 clamped = std::clamp(heights[index] / heightScale, -32767.0f, 32767.0f);
            samples[index] = PxHeightFieldSample();
            samples[index].height = static_cast<PxI16>(std::lround(clamped));
            samples[index].materialIndex0 = PxBitAndByte(0);
            samples[index].materialIndex1 = PxBitAndByte(0);
        }

        PxHeightFieldDesc desc;
        desc.nbRows = rows;
        desc.nbColumns = columns;
        desc.format = PxHeightFieldFormat::eS16_TM;
        desc.samples.data = samples.data();
        desc.samples.stride = sizeof(PxHeightFieldSample);
        desc.flags = PxHeightFieldFlag::eNO_BOUNDARY_EDGES;

        PxHeightField* heightField = PxCreateHeightField(desc, physics->getPhysicsInsertionCallback());
        if (!heightField)
        {
            Log::Error("PhysX heightfield creation failed.");
            return nullptr;
        }

        PxHeightFieldGeometry geometry(heightField, PxMeshGeometryFlags(), heightScale, component.GetRowScale(), component.GetColumnScale());

        PxMaterial* material = physics->createMaterial(0.6f, 0.6f, 0.0f);
        if (!material)
        {
            heightField->release();
            return nullptr;
        }

        PxShape* shape = physics->createShape(geometry, *material, true,
            PxShapeFlag::eSCENE_QUERY_SHAPE | PxShapeFlag::eSIMULATION_SHAPE);
        material->release();
        if (!shape)
        {
            heightField->release();
            return nullptr;
        }

        //PhysX 高度场从角落开始采样，渲染网格以中心为原点。
        shape->setLocalPose(PxTransform(PxVec3(-component.sizeX * 0.5f, 0.0f, -component.sizeZ * 0.5f)));

        PxFilterData filter(layerMask, collisionMask, 0xFFFFFFFFu, 0);
        shape->setSimulationFilterData(filter);
        shape->setQueryFilterData(filter);

        PxRigidStatic* actor = physics->createRigidStatic(PxTransform(ToPx(transform.worldPosition), ToPx(transform.worldRotation)));
        if (!actor)
        {
            shape->release();
            heightField->release();
            return nullptr;
        }
        actor->attachShape(*shape);
        shape->release();

        std::unique_ptr<HeightFieldRecord> record = std::make_unique<HeightFieldRecord>();
        record->binding.kind = BindingKind::Body;
        record->binding.ens = component.GetEnsId();
        record->binding.owner = this;
        record->actor = actor;
        record->heightField = heightField;
        record->layerMask = layerMask;
        record->collisionMask = collisionMask;
        record->configurationHash = configurationHash;
        record->generation = component.GetGeneration();
        actor->userData = &record->binding;
        scene->addActor(*actor);
        return record;
    }

    //同步 HeightField 静态体。
    void SyncHeightField(HeightField& component, Transform& transform, uint64 key,
        uint32 layerMask, uint32 collisionMask)
    {
        component.SyncPendingGeneration();

        uint64 hash = CalculateHeightFieldHash(component);
        auto found = heightFields.find(key);
        if (found != heightFields.end()
            && (found->second->configurationHash != hash || found->second->generation != component.GetGeneration()))
        {
            DestroyHeightField(*found->second);
            heightFields.erase(found);
            found = heightFields.end();
        }
        if (found == heightFields.end())
        {
            std::unique_ptr<HeightFieldRecord> created = CreateHeightField(component, transform, hash, layerMask, collisionMask);
            if (created) heightFields.emplace(key, std::move(created));
            return;
        }

        HeightFieldRecord& record = *found->second;
        if (record.layerMask != layerMask || record.collisionMask != collisionMask)
        {
            PxShape* shape = nullptr;
            if (record.actor->getShapes(&shape, 1) == 1)
            {
                PxFilterData filter(layerMask, collisionMask, 0xFFFFFFFFu, 0);
                shape->setSimulationFilterData(filter);
                shape->setQueryFilterData(filter);
                scene->resetFiltering(*record.actor);
            }
            record.layerMask = layerMask;
            record.collisionMask = collisionMask;
        }

        PxTransform pose(ToPx(transform.worldPosition), ToPx(transform.worldRotation));
        if (record.actor) record.actor->setGlobalPose(pose);
    }

    //同步所属刚体位姿、速度与脚本累积力
    void SyncBodyPoseAndVelocity(BodyRecord& record, RigidBody* body, Transform& transform, bool queryOnly = false)
    {
        PxTransform pose(ToPx(transform.worldPosition), ToPx(transform.worldRotation));
        //查询模式只把位姿写进场景，不设 kinematicTarget、不写速度、不施力也不清累积力
        if (queryOnly)
        {
            record.actor->setGlobalPose(pose);
            record.lastPosition = transform.worldPosition;
            record.lastRotation = transform.worldRotation;
            record.lastPoseValid = true;
            return;
        }

        if (record.bodyType == PhysicsBodyType::Static)
        {
            record.actor->setGlobalPose(pose);
        }
        else
        {
            PxRigidDynamic* dynamic = static_cast<PxRigidDynamic*>(record.actor);
            if (record.bodyType == PhysicsBodyType::Kinematic)
            {
                dynamic->setKinematicTarget(pose);
            }
            else
            {
                if (!record.lastPoseValid || !NearlyEqual(transform.worldPosition, record.lastPosition) || !NearlyEqual(transform.worldRotation, record.lastRotation))
                {
                    dynamic->setGlobalPose(pose, true);
                }
                if (body)
                {
                    dynamic->setLinearVelocity(ToPx(body->linearVelocity), false);
                    dynamic->setAngularVelocity(ToPx(body->angularVelocity), false);

                    //施加脚本累积的力/力矩，然后清零；下一物理步积分。
                    if (body->pendingForce.x != 0.0f || body->pendingForce.y != 0.0f || body->pendingForce.z != 0.0f)
                    {
                        dynamic->addForce(ToPx(body->pendingForce), PxForceMode::eFORCE, true);
                        body->pendingForce = vector3();
                    }
                    if (body->pendingTorque.x != 0.0f || body->pendingTorque.y != 0.0f || body->pendingTorque.z != 0.0f)
                    {
                        dynamic->addTorque(ToPx(body->pendingTorque), PxForceMode::eFORCE, true);
                        body->pendingTorque = vector3();
                    }
                }
            }
        }
        record.lastPosition = transform.worldPosition;
        record.lastRotation = transform.worldRotation;
        record.lastPoseValid = true;
    }

    //按最近刚体归属收集形状并同步复合刚体
    void SyncBodies(World& currentWorld, bool queryOnly = false)
    {
        for (auto& entry : bodyInputs)
        {
            entry.second.ens = nullptr;
            entry.second.body = nullptr;
            entry.second.colliders.clear();
        }
        attachedBodies.clear();
        std::unordered_set<uint64> heightFieldSeen;
        std::unordered_set<Mesh*> dirtyMeshes;
        bodyTraversal.clear();
        currentWorld.ForEachEns([&](Ens& ens)
        {
            if (!ens.Transform()->parent.IsNull()) return;
            bodyTraversal.emplace_back(ens.GetId(), nullptr, 0);
        });
        std::reverse(bodyTraversal.begin(), bodyTraversal.end());
        while (!bodyTraversal.empty())
        {
            auto [id, inheritedBody, inheritedPoseHash] = bodyTraversal.back();
            bodyTraversal.pop_back();
            Ens& ens = *currentWorld.GetEns(id);
            Transform& transform = *ens.Transform();
            RigidBody* ownBody = ens.GetComponent<RigidBody>();
            RigidBody* attachedBody = ownBody ? ownBody : inheritedBody;
            uint64 poseHash = inheritedPoseHash;
            if (ownBody)
            {
                poseHash = transform.parent.IsNull() ? MixVector(0xCBF29CE484222325ull, transform.GetLocalScale()) : 0;
            }
            else if (attachedBody && poseHash != 0)
            {
                poseHash = MixVector(poseHash, transform.GetResolvedLocalPosition());
                const quaternion& rotation = transform.GetLocalRotation();
                poseHash = MixVector(poseHash, { rotation.x, rotation.y, rotation.z });
                poseHash = MixFloat(poseHash, rotation.w);
                poseHash = MixVector(poseHash, transform.GetLocalScale());
            }
            if (ownBody)
            {
                attachedBodies[EnsKey(id)] = id;
            }
            for (EnsId child = transform.lastChild; !child.IsNull(); child = currentWorld.GetTransform(child)->prev)
            {
                bodyTraversal.emplace_back(child, attachedBody, poseHash);
            }
            if (!ens.GetWorldActive()) continue;

            //保留独立高度场的静态体同步
            HeightField* heightField = ens.GetComponent<HeightField>();
            if (heightField && heightField->enabled && (!ownBody || (ownBody->enabled && ownBody->bodyType == PhysicsBodyType::Static)))
            {
                uint64 key = EnsKey(id);
                heightFieldSeen.insert(key);
                SyncHeightField(*heightField, transform, key, ens.GetLayerMask(), LayerSettings::GetCollisionMask(ens.GetLayer()));
            }
            if (attachedBody && (!attachedBody->enabled || !attachedBody->GetEns()->GetWorldActive())) continue;

            //刚体节点即使没有普通 Collider 也持有 Actor
            if (ownBody)
            {
                BodyInput& input = bodyInputs[EnsKey(id)];
                input.ens = &ens;
                input.body = ownBody;
            }
            for (Component* component : ens.GetComponents())
            {
                if (component && component->Is(WheelCollider::StaticType()))
                {
                    attachedBodies[EnsKey(id)] = attachedBody ? attachedBody->GetEnsId() : EnsId();
                }
                Collider* collider = component ? component->Cast<Collider>() : nullptr;
                if (!collider || !collider->enabled) continue;
                attachedBodies[EnsKey(id)] = attachedBody ? attachedBody->GetEnsId() : EnsId();
                Ens* owner = attachedBody ? attachedBody->GetEns() : &ens;
                BodyInput& input = bodyInputs[EnsKey(owner->GetId())];
                input.ens = owner;
                input.body = attachedBody;
                input.colliders.emplace_back(collider, poseHash);
                Mesh* mesh = GetColliderMesh(*collider);
                if (mesh && mesh->IsDirty(MeshDirtyFlags::Physics))
                {
                    dirtyMeshes.insert(mesh);
                }
            }
        }
        for (Mesh* mesh : dirtyMeshes)
        {
            InvalidateCookedMeshes(*mesh);
        }

        //保留 Actor，只按各形状的几何与相对变换更新
        for (auto iterator = bodyInputs.begin(); iterator != bodyInputs.end();)
        {
            BodyInput& input = iterator->second;
            if (!input.ens)
            {
                iterator = bodyInputs.erase(iterator);
                continue;
            }
            uint64 key = iterator->first;
            uint64 hash = CalculateBodyHash(input.body);
            auto found = bodies.find(key);
            PhysicsBodyType type = input.body ? input.body->bodyType : PhysicsBodyType::Static;
            if (input.ens->GetStatic()) type = PhysicsBodyType::Static;
            if (found != bodies.end() && (found->second->configurationHash != hash || found->second->bodyType != type
                || (type == PhysicsBodyType::Dynamic && !input.ens->Transform()->parent.IsNull())))
            {
                DestroyBody(*found->second);
                bodies.erase(found);
                found = bodies.end();
            }
            if (found == bodies.end())
            {
                std::unique_ptr<BodyRecord> created = CreateBody(*input.ens, input.body, hash);
                if (created) found = bodies.emplace(key, std::move(created)).first;
            }
            if (found != bodies.end())
            {
                SyncBodyShapes(*found->second, input, dirtyMeshes, queryOnly);
                SyncBodyPoseAndVelocity(*found->second, input.body, *input.ens->Transform(), queryOnly);
            }
            ++iterator;
        }
        for (auto iterator = bodies.begin(); iterator != bodies.end();)
        {
            if (bodyInputs.contains(iterator->first))
            {
                ++iterator;
                continue;
            }
            DestroyBody(*iterator->second);
            iterator = bodies.erase(iterator);
        }
        for (auto iterator = heightFields.begin(); iterator != heightFields.end();)
        {
            if (heightFieldSeen.contains(iterator->first))
            {
                ++iterator;
                continue;
            }
            DestroyHeightField(*iterator->second);
            iterator = heightFields.erase(iterator);
        }
        for (Mesh* mesh : dirtyMeshes)
        {
            mesh->ClearDirty(MeshDirtyFlags::Physics);
        }
    }

    uint64 CalculateControllerHash(const CharacterController& component) const
    {
        uint64 hash = 0xCBF29CE484222325ull;
        hash = MixHash(hash, static_cast<uint32>(component.shape));
        hash = MixFloat(hash, component.radius);
        hash = MixFloat(hash, component.height);
        hash = MixVector(hash, component.halfExtents);
        hash = MixFloat(hash, component.stepOffset);
        hash = MixFloat(hash, component.contactOffset);
        hash = MixFloat(hash, component.slopeLimit);
        return hash;
    }

    std::unique_ptr<ControllerRecord> CreateController(CharacterController& component, Transform& transform,
        uint64 configurationHash, const PxFilterData& filter)
    {
        //static 的 Ens 不允许被角色控制器驱动
        if (world && world->GetEnsStatic(component.GetEnsId()))
        {
            ReportStaticDrive(component.GetEnsId());
            return nullptr;
        }

        if (!transform.parent.IsNull())
        {
            Log::Warning("Character controllers must be root entities; the controller was skipped.");
            return nullptr;
        }

        std::unique_ptr<ControllerRecord> record = std::make_unique<ControllerRecord>();
        record->binding.kind = BindingKind::Controller;
        record->filter = filter;
        record->binding.ens = component.GetEnsId();
        record->binding.owner = this;
        record->configurationHash = configurationHash;

        PxMaterial* material = physics->createMaterial(0.5f, 0.5f, 0.0f);
        if (!material) return nullptr;

        PxController* controller = nullptr;
        if (component.shape == CharacterControllerShape::Capsule)
        {
            PxCapsuleControllerDesc desc;
            desc.radius = Positive(component.radius);
            desc.height = Positive(component.height);
            desc.position = PxExtendedVec3(transform.worldPosition.x, transform.worldPosition.y + desc.radius + desc.height * 0.5f, transform.worldPosition.z);
            desc.stepOffset = std::max(0.0f, component.stepOffset);
            desc.contactOffset = Positive(component.contactOffset);
            desc.slopeLimit = std::max(0.0f, component.slopeLimit);
            desc.upDirection = PxVec3(0.0f, 1.0f, 0.0f);
            desc.material = material;
            desc.userData = &record->binding;
            controller = controllerManager->createController(desc);
        }
        else
        {
            PxBoxControllerDesc desc;
            desc.halfSideExtent = Positive(component.halfExtents.x);
            desc.halfHeight = Positive(component.halfExtents.y);
            desc.halfForwardExtent = Positive(component.halfExtents.z);
            desc.position = PxExtendedVec3(transform.worldPosition.x, transform.worldPosition.y + desc.halfHeight, transform.worldPosition.z);
            desc.stepOffset = std::max(0.0f, component.stepOffset);
            desc.contactOffset = Positive(component.contactOffset);
            desc.slopeLimit = std::max(0.0f, component.slopeLimit);
            desc.upDirection = PxVec3(0.0f, 1.0f, 0.0f);
            desc.material = material;
            desc.userData = &record->binding;
            controller = controllerManager->createController(desc);
        }
        material->release();
        if (!controller) return nullptr;

        record->controller = controller;
        controller->setFootPosition(PxExtendedVec3(transform.worldPosition.x, transform.worldPosition.y, transform.worldPosition.z));
        PxRigidDynamic* actor = controller->getActor();
        actor->userData = &record->binding;
        PxShape* controllerShape = nullptr;
        if (actor->getShapes(&controllerShape, 1) == 1 && controllerShape)
        {
            controllerShape->setSimulationFilterData(filter);
            controllerShape->setQueryFilterData(filter);
        }
        record->lastFootPosition = transform.worldPosition;
        record->lastPoseValid = true;
        return record;
    }

    void DestroyController(ControllerRecord& record)
    {
        if (!record.controller) return;
        PxRigidDynamic* actor = record.controller->getActor();
        if (actor) actor->userData = nullptr;
        record.controller->release();
        record.controller = nullptr;
    }

    void SyncControllers(World& currentWorld)
    {
        std::unordered_set<uint64> seen;
        currentWorld.ForEachComponent<CharacterController>([&](CharacterController* component)
        {
            if (!component || !component->enabled) return;
            Ens* ens = component->GetEns();
            if (!ens || !ens->GetWorldActive()) return;
            Transform* transform = ens->Transform();
            if (!transform) return;

            uint64 key = EnsKey(component->GetEnsId());
            PxFilterData filter(ens->GetLayerMask(),
                component->collisionMask & LayerSettings::GetCollisionMask(ens->GetLayer()), 0, 0);
            seen.insert(key);
            uint64 hash = CalculateControllerHash(*component);
            auto found = controllers.find(key);
            if (found != controllers.end() && found->second->configurationHash != hash)
            {
                DestroyController(*found->second);
                controllers.erase(found);
                found = controllers.end();
            }
            if (found == controllers.end())
            {
                std::unique_ptr<ControllerRecord> created = CreateController(*component, *transform, hash, filter);
                if (created) controllers.emplace(key, std::move(created));
                return;
            }

            ControllerRecord& record = *found->second;
            if (record.filter != filter)
            {
                PxShape* shape = nullptr;
                PxRigidDynamic* actor = record.controller->getActor();
                if (actor->getShapes(&shape, 1) == 1)
                {
                    shape->setSimulationFilterData(filter);
                    shape->setQueryFilterData(filter);
                    scene->resetFiltering(*actor);
                }
                record.controller->invalidateCache();
                record.filter = filter;
            }
            if (!record.lastPoseValid || !NearlyEqual(record.lastFootPosition, transform->worldPosition))
            {
                record.controller->setFootPosition(PxExtendedVec3(transform->worldPosition.x, transform->worldPosition.y, transform->worldPosition.z));
                record.lastFootPosition = transform->worldPosition;
                record.lastPoseValid = true;
            }
        });

        for (auto it = controllers.begin(); it != controllers.end();)
        {
            if (seen.contains(it->first))
            {
                ++it;
                continue;
            }
            DestroyController(*it->second);
            it = controllers.erase(it);
        }
    }

    //static 的 Ens 被物理驱动时按 Ens 去重上报，避免每帧刷屏
    void ReportStaticDrive(EnsId ens)
    {
        if (!reportedStaticDrives.insert(EnsKey(ens)).second) return;

        std::string message = "Physics skipped a driven body or controller: the Ens is static";
        Ens* entity = world ? world->GetEns(ens) : nullptr;
        if (entity) message += " (" + entity->GetName() + ")";
        Log::Warning((message + ".").c_str());
    }

    void WriteDynamicPoses(World& currentWorld)
    {
        for (auto& entry : bodies)
        {
            BodyRecord& record = *entry.second;
            if (record.bodyType != PhysicsBodyType::Dynamic) continue;
            Transform* transform = currentWorld.GetTransform(record.binding.ens);
            Ens* ens = currentWorld.GetEns(record.binding.ens);
            RigidBody* body = ens ? ens->GetComponent<RigidBody>() : nullptr;
            if (!transform || !body) continue;

            PxRigidDynamic* dynamic = static_cast<PxRigidDynamic*>(record.actor);
            //回写前校验：static 的 Ens 不改世界变换，也不继续推进这个 actor，避免显示与物理位置分离
            if (!currentWorld.CanChangeTransform(record.binding.ens))
            {
                dynamic->setLinearVelocity(PxVec3(0.0f, 0.0f, 0.0f));
                dynamic->setAngularVelocity(PxVec3(0.0f, 0.0f, 0.0f));
                ReportStaticDrive(record.binding.ens);
                continue;
            }

            PxTransform pose = dynamic->getGlobalPose();
            transform->SetLocalPosition(FromPx(pose.p));
            transform->SetLocalRotation(FromPx(pose.q));
            body->linearVelocity = FromPx(dynamic->getLinearVelocity());
            body->angularVelocity = FromPx(dynamic->getAngularVelocity());
            record.lastPosition = transform->GetLocalPosition();
            record.lastRotation = transform->GetLocalRotation();
            record.lastPoseValid = true;
        }
    }

    //只同步场景查询所需的位姿：不 simulate、不 fetchResults、不写回 Transform 与速度
    void SynchronizeQueries(World& currentWorld)
    {
        if (!Initialize()) return;
        world = &currentWorld;
        LayerSettings::Refresh();
        transformCache.Update(currentWorld);
        SyncBodies(currentWorld, true);
        SyncControllers(currentWorld);
        scene->flushQueryUpdates();
    }

    void FixedUpdate(World& currentWorld, float32 deltaTime)
    {
        if (!Initialize()) return;
        world = &currentWorld;
        events.clear();

        {
            PROFILE("Physics/Sync");
            transformCache.Update(currentWorld);
            SyncBodies(currentWorld);
            SyncControllers(currentWorld);
            SyncWheels(currentWorld);
        }

        {
            PROFILE("Physics/Simulate");
            scene->simulate(deltaTime);
        }

        {
            PROFILE("Physics/FetchResults");
            scene->fetchResults(true);
            WriteDynamicPoses(currentWorld);
        }
    }

    bool FillHit(const PxLocationHit& nativeHit, const PxShape* shape, const PxRigidActor* actor, PhysicsQueryHit& hit) const
    {
        EnsId ens = FindEns(shape, actor);
        if (ens.IsNull()) return false;
        hit.ens = ens;
        hit.position = FromPx(nativeHit.position);
        hit.normal = FromPx(nativeHit.normal);
        hit.distance = nativeHit.distance;
        return true;
    }

    //沿悬挂方向探测地面并排除所属刚体和触发器
    bool WheelRaycast(const vector3& origin, const vector3& direction, float32 distance,
        PhysicsQueryHit& hit, uint32 layerMask, const PxRigidActor* body) const
    {
        PxVec3 unitDirection = ToPx(direction);
        if (distance <= 0.0f || unitDirection.normalize() <= 0.0f) return false;
        LayerQueryFilter callback(layerMask, false, 0, body, false);
        PxQueryFilterData filter(PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
        PxRaycastBuffer result;
        if (!scene->raycast(ToPx(origin), unitDirection, distance, result, PxHitFlag::eDEFAULT, filter, &callback) || !result.hasBlock) return false;
        return FillHit(result.block, result.block.shape, result.block.actor, hit);
    }

    //模拟全部机轮：弹簧阻尼悬挂 + 地面摩擦 + 转向侧向力。
    void SyncWheels(World& currentWorld)
    {
        currentWorld.ForEachComponent<WheelCollider>([&](WheelCollider* wheel)
        {
            Ens& ens = *wheel->GetEns();
            Transform* transform = ens.Transform();
            wheel->grounded = false;
            wheel->compression = 0.0f;
            wheel->previousCompression = 0.0f;
            if (!ens.GetWorldActive() || !wheel->enabled || !transform) return;
            BodyRecord* record = FindBody(ens.GetId());
            if (!record || record->bodyType != PhysicsBodyType::Dynamic) return;
            PxRigidDynamic* dynamic = static_cast<PxRigidDynamic*>(record->actor);
            Transform& bodyTransform = *currentWorld.GetTransform(record->binding.ens);
            quaternion rotation = bodyTransform.worldRotation;

            //取车轮节点位置，按所属刚体坐标系计算偏移与悬挂
            vector3 mountWorld = Add(transform->worldPosition,
                RenderMath::TransformDirection(bodyTransform.worldMatrix, wheel->wheelOffset));
            vector3 direction = Rotate(rotation, { 0.0f, -1.0f, 0.0f });
            float32 suspensionScale = GetWorldScale(bodyTransform).y;
            PhysicsQueryHit hit;
            if (!WheelRaycast(mountWorld, direction, wheel->raycastDistance * suspensionScale,
                hit, wheel->groundQueryLayer, dynamic)) return;

            float32 compression = std::clamp((wheel->suspensionRestLength + wheel->wheelRadius) * suspensionScale
                - hit.distance, 0.0f, std::max(0.0f, wheel->suspensionTravel * suspensionScale));
            if (compression <= 0.0f) return;

            //接触点速度包含角速度；阻尼抵抗压缩和回弹，避免接地差分尖峰。
            vector3 velocity = FromPx(PxRigidBodyExt::getVelocityAtPos(*dynamic, ToPx(hit.position)));
            float32 normalForce = std::max(wheel->suspensionStiffness * compression
                - wheel->suspensionDamping * Dot(velocity, hit.normal), 0.0f);

            //地面切向速度与轮子朝向（转向轮按 steerAngle 旋转）。
            vector3 normal = hit.normal;
            vector3 tangentVelocity = Add(velocity, Scale(normal, -Dot(velocity, normal)));
            vector3 forwardWorld = Rotate(rotation, { 0.0f, 0.0f, -1.0f });
            vector3 forwardGround = NormalizeSafe(
                Add(forwardWorld, Scale(normal, -Dot(forwardWorld, normal))),
                Rotate(rotation, { 1.0f, 0.0f, 0.0f }));
            if (wheel->steeringWheel && wheel->steerAngle != 0.0f)
            {
                forwardGround = RotateAxis(forwardGround, normal, wheel->steerAngle * Pi / 180.0f);
            }
            vector3 rightGround = Cross(forwardGround, normal);
            float32 forwardComponent = Dot(tangentVelocity, forwardGround);
            float32 lateralComponent = Dot(tangentVelocity, rightGround);

            //纵向滚动阻力 + 横向防滑（转向轮产生转向侧向力，绕质心形成偏航力矩）。
            vector3 friction = Scale(forwardGround, -forwardComponent * wheel->rollingFriction * 150.0f);
            float32 lateralMagnitude = std::clamp(std::abs(lateralComponent) * wheel->lateralFriction * 150.0f,
                0.0f, wheel->lateralFriction * normalForce);
            friction = Add(friction, Scale(rightGround, -std::copysign(lateralMagnitude, lateralComponent)));
            //当前物理步直接提交轮力，避免上一帧变换和延迟悬挂力。
            vector3 force = Add(Scale(hit.normal, normalForce), friction);
            PxRigidBodyExt::addForceAtPos(*dynamic, ToPx(force), ToPx(hit.position), PxForceMode::eFORCE, true);

            wheel->grounded = true;
            wheel->compression = compression;
            wheel->previousCompression = compression;
        });
    }
};

PhysicsSystem::PhysicsSystem()
    : impl(new Impl())
{
}

PhysicsSystem::~PhysicsSystem()
{
    Shutdown();
    delete impl;
    impl = nullptr;
}

//创建并初始化物理系统
//获取当前 Application 拥有的 PhysicsSystem。
PhysicsSystem* PhysicsSystem::Current()
{
    return currentPhysicsSystem;
}

bool PhysicsSystem::OnInitialize(Application& app)
{
    (void)app;
    currentPhysicsSystem = this;
    return Initialize();
}

//关闭并释放物理系统
void PhysicsSystem::OnShutdown()
{
    Shutdown();
    if (currentPhysicsSystem == this) currentPhysicsSystem = nullptr;
}

//创建 PhysX Foundation、Scene、Cooking 和 CCT 状态
bool PhysicsSystem::Initialize()
{
    return impl && impl->Initialize();
}

//释放全部原生物理状态
void PhysicsSystem::Shutdown()
{
    if (impl) impl->Shutdown();
}

//判断物理系统是否可用
bool PhysicsSystem::IsInitialized() const
{
    return impl && impl->physics;
}

//清空当前 World 对应的原生 Actor、CCT 和事件
void PhysicsSystem::ResetWorld()
{
    if (impl) impl->ResetWorld();
}

//执行固定步长同步和模拟
void PhysicsSystem::SynchronizeQueries(World& world)
{
    PROFILE("Physics/QuerySync");

    //编辑态预览用：只把位姿同步给场景查询，不推进刚体
    if (impl) impl->SynchronizeQueries(world);
}

void PhysicsSystem::FixedUpdate(World& world, float fixedDeltaTime)
{
    PROFILE("Physics/Step");

    LayerSettings::Refresh();
    if (impl && fixedDeltaTime > 0.0f) impl->FixedUpdate(world, fixedDeltaTime);
}

//发射射线并返回最近命中
bool PhysicsSystem::Raycast(const vector3& origin, const vector3& direction, float32 distance, PhysicsQueryHit& hit, uint32 layerMask) const
{
    if (!IsInitialized() || distance <= 0.0f) return false;
    PxVec3 unitDirection = ToPx(direction);
    if (unitDirection.normalize() <= 0.0f) return false;

    LayerQueryFilter callback(layerMask, false);
    PxQueryFilterData filter(PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
    PxRaycastBuffer result;
    if (!impl->scene->raycast(ToPx(origin), unitDirection, distance, result, PxHitFlag::eDEFAULT, filter, &callback) || !result.hasBlock) return false;
    return impl->FillHit(result.block, result.block.shape, result.block.actor, hit);
}

//使用球体扫描并返回最近命中
bool PhysicsSystem::SweepSphere(const vector3& origin, float32 radius, const vector3& direction, float32 distance, PhysicsQueryHit& hit, uint32 layerMask) const
{
    if (!IsInitialized() || radius <= 0.0f || distance <= 0.0f) return false;
    PxVec3 unitDirection = ToPx(direction);
    if (unitDirection.normalize() <= 0.0f) return false;

    LayerQueryFilter callback(layerMask, false);
    PxQueryFilterData filter(PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
    PxSweepBuffer result;
    PxSphereGeometry geometry(radius);
    if (!impl->scene->sweep(geometry, PxTransform(ToPx(origin)), unitDirection, distance, result, PxHitFlag::eDEFAULT, filter, &callback) || !result.hasBlock) return false;
    return impl->FillHit(result.block, result.block.shape, result.block.actor, hit);
}

//带附加过滤条件的球体扫描
bool PhysicsSystem::SweepSphereFiltered(const vector3& origin, float32 radius, const vector3& direction,
    float32 distance, PhysicsQueryHit& hit, const PhysicsQueryFilter& queryFilter) const
{
    //半径与距离必须有限，且命中结果也要满足同样的有限性检查
    if (!IsInitialized() || !std::isfinite(radius) || radius <= 0.0f || !std::isfinite(distance) || distance <= 0.0f) return false;
    PxVec3 unitDirection = ToPx(direction);
    if (unitDirection.normalize() <= 0.0f) return false;

    LayerQueryFilter callback(queryFilter.layerMask, false, queryFilter,
        [this](const PxShape* shape, const PxActor* actor) { return impl->FindEns(shape, actor); });
    PxQueryFilterData filter(PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
    PxSweepBuffer result;
    PxSphereGeometry geometry(radius);
    if (!impl->scene->sweep(geometry, PxTransform(ToPx(origin)), unitDirection, distance, result, PxHitFlag::eDEFAULT, filter, &callback) || !result.hasBlock) return false;
    if (!impl->FillHit(result.block, result.block.shape, result.block.actor, hit)) return false;

    //无效命中按未命中处理
    if (!std::isfinite(hit.distance) || !std::isfinite(hit.position.x) || !std::isfinite(hit.position.y) ||
        !std::isfinite(hit.position.z) || !std::isfinite(hit.normal.x) || !std::isfinite(hit.normal.y) ||
        !std::isfinite(hit.normal.z)) return false;
    float32 normalLength = hit.normal.x * hit.normal.x + hit.normal.y * hit.normal.y + hit.normal.z * hit.normal.z;
    return normalLength > 1.0e-6f;
}

//收集与球体重叠的实体
uint32 PhysicsSystem::OverlapSphere(const vector3& center, float32 radius, List<PhysicsQueryHit>& hits, uint32 layerMask) const
{
    hits.clear();
    if (!IsInitialized() || radius <= 0.0f) return 0;

    constexpr PxU32 MaxHits = 128;
    PxOverlapHit nativeHits[MaxHits];
    PxOverlapBuffer result(nativeHits, MaxHits);
    LayerQueryFilter callback(layerMask, true);
    PxQueryFilterData filter(PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
    PxSphereGeometry geometry(radius);
    if (!impl->scene->overlap(geometry, PxTransform(ToPx(center)), result, filter, &callback)) return 0;

    std::unordered_set<uint64> unique;
    for (PxU32 index = 0; index < result.getNbTouches(); index++)
    {
        EnsId ens = impl->FindEns(result.getTouch(index).shape, result.getTouch(index).actor);
        if (ens.IsNull() || !unique.insert(EnsKey(ens)).second) continue;
        PhysicsQueryHit hit;
        hit.ens = ens;
        hit.position = center;
        hits.push_back(hit);
    }
    return static_cast<uint32>(hits.size());
}

//移动角色控制器并返回 CharacterCollisionFlag 位
uint32 PhysicsSystem::MoveCharacter(EnsId ens, const vector3& displacement, float32 deltaTime)
{
    if (!IsInitialized() || !impl->world || deltaTime <= 0.0f) return CharacterCollisionNone;
    //static 的 Ens 不允许被控制器驱动
    if (impl->world->GetEnsStatic(ens))
    {
        impl->ReportStaticDrive(ens);
        return CharacterCollisionNone;
    }

    auto found = impl->controllers.find(EnsKey(ens));
    if (found == impl->controllers.end()) return CharacterCollisionNone;

    Ens* entity = impl->world->GetEns(ens);
    CharacterController* component = entity ? entity->GetComponent<CharacterController>() : nullptr;
    if (!component) return CharacterCollisionNone;

    const PxFilterData& cachedFilter = found->second->filter;
    LayerQueryFilter callback(cachedFilter.word1, false, cachedFilter.word0);
    ControllerPairFilter controllerPairFilter;
    PxFilterData filterData(cachedFilter.word1, 0, 0, 0);
    PxControllerFilters filters(&filterData, &callback, &controllerPairFilter);
    PxControllerCollisionFlags nativeFlags = found->second->controller->move(
        ToPx(displacement), std::max(0.0f, component->minMoveDistance), deltaTime, filters);

    PxExtendedVec3 foot = found->second->controller->getFootPosition();
    vector3 position = { static_cast<float32>(foot.x), static_cast<float32>(foot.y), static_cast<float32>(foot.z) };
    Transform* transform = impl->world->GetTransform(ens);
    if (transform)
    {
        transform->SetLocalPosition(position);
    }
    found->second->lastFootPosition = position;
    found->second->lastPoseValid = true;

    uint32 result = CharacterCollisionNone;
    if (nativeFlags & PxControllerCollisionFlag::eCOLLISION_SIDES) result |= CharacterCollisionSides;
    if (nativeFlags & PxControllerCollisionFlag::eCOLLISION_UP) result |= CharacterCollisionUp;
    if (nativeFlags & PxControllerCollisionFlag::eCOLLISION_DOWN) result |= CharacterCollisionDown;
    return result;
}

//将角色控制器脚底移动到指定世界坐标
bool PhysicsSystem::TeleportCharacter(EnsId ens, const vector3& footPosition)
{
    if (!IsInitialized()) return false;
    auto found = impl->controllers.find(EnsKey(ens));
    if (found == impl->controllers.end()) return false;
    if (!found->second->controller->setFootPosition(PxExtendedVec3(footPosition.x, footPosition.y, footPosition.z))) return false;

    found->second->lastFootPosition = footPosition;
    found->second->lastPoseValid = true;
    if (impl->world)
    {
        Transform* transform = impl->world->GetTransform(ens);
        if (transform)
        {
            transform->SetLocalPosition(footPosition);
        }
    }
    return true;
}

//读取最近一次固定步产生的物理事件
const List<PhysicsEvent>& PhysicsSystem::GetEvents() const
{
    static const List<PhysicsEvent> empty;
    return impl ? impl->events : empty;
}

//立即清空事件队列
void PhysicsSystem::ClearEvents()
{
    if (impl) impl->events.clear();
}

//获取原生 Foundation
PxFoundation* PhysicsSystem::GetFoundation() const
{
    return impl ? impl->foundation : nullptr;
}

//获取原生 Physics
PxPhysics* PhysicsSystem::GetPhysics() const
{
    return impl ? impl->physics : nullptr;
}

//获取原生 Scene
PxScene* PhysicsSystem::GetScene() const
{
    return impl ? impl->scene : nullptr;
}

//获取 PhysX 5.9 无状态 Cooking 参数
const PxCookingParams* PhysicsSystem::GetCookingParams() const
{
    return impl ? impl->cookingParams.get() : nullptr;
}

//获取原生 CCT 管理器
PxControllerManager* PhysicsSystem::GetControllerManager() const
{
    return impl ? impl->controllerManager : nullptr;
}

//查找实体对应的原生刚体 Actor
PxRigidActor* PhysicsSystem::GetRigidActor(EnsId ens) const
{
    if (!impl) return nullptr;
    Impl::BodyRecord* record = impl->FindBody(ens);
    return record ? record->actor : nullptr;
}

//查找实体对应的原生角色控制器
PxController* PhysicsSystem::GetController(EnsId ens) const
{
    if (!impl) return nullptr;
    auto found = impl->controllers.find(EnsKey(ens));
    return found != impl->controllers.end() ? found->second->controller : nullptr;
}

/// <summary>读取 PhysX 场景重力，未初始化时返回默认值。</summary>
vector3 PhysicsSystem::GetGravity() const
{
    return impl && impl->scene ? FromPx(impl->scene->getGravity()) : vector3{ 0.0f, -9.81f, 0.0f };
}

/// <summary>复用 PhysX 烘焙凸包，按多边形边界输出不重复的线段。</summary>
bool PhysicsSystem::GetConvexWireframe(Mesh& mesh, List<vector3>& lines)
{
    lines.clear();
    if (!IsInitialized()) return false;
    if (mesh.IsDirty(MeshDirtyFlags::Physics)) impl->InvalidateCookedMeshes(mesh);
    PxConvexMesh* convex = impl->GetConvexMesh(mesh);
    if (!convex) return false;
    const PxVec3* vertices = convex->getVertices();
    const PxU8* indices = convex->getIndexBuffer();
    for (PxU32 face = 0; face < convex->getNbPolygons(); ++face)
    {
        PxHullPolygon polygon;
        if (!convex->getPolygonData(face, polygon)) continue;
        for (PxU32 edge = 0; edge < polygon.mNbVerts; ++edge)
        {
            PxU8 a = indices[polygon.mIndexBase + edge];
            PxU8 b = indices[polygon.mIndexBase + (edge + 1) % polygon.mNbVerts];
            if (a >= b) continue;
            lines.push_back(FromPx(vertices[a]));
            lines.push_back(FromPx(vertices[b]));
        }
    }
    return true;
}
