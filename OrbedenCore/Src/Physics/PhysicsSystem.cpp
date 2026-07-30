#include "Physics/PhysicsSystem.h"

#include "Log/Log.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Rendering/SpaceCache.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/SpaceComponent.h"

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
#include <bit>
#include <cmath>
#include <memory>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using namespace physx;

namespace
{
    constexpr float32 MinimumDimension = 0.001f;
    constexpr uint32 BindingMagic = 0x4F524250u;

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

    vector3 GetWorldScale(const SpaceComponent& space)
    {
        const float32* m = space.worldMatrix.m;
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

    public:
        LayerQueryFilter(uint32 mask, bool touch, uint32 layer = 0)
            : layerMask(mask), queryLayer(layer), returnTouch(touch)
        {
        }

        PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* shape, const PxRigidActor*, PxHitFlags&) override
        {
            if (!shape) return PxQueryHitType::eNONE;
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

class PhysicsSystem::Impl
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

    struct BodyRecord
    {
        NativeBinding binding;
        PxRigidActor* actor = nullptr;
        uint64 configurationHash = 0;
        PhysicsBodyType bodyType = PhysicsBodyType::Static;
        vector3 lastPosition;
        quaternion lastRotation;
        bool lastPoseValid = false;
    };

    struct ControllerRecord
    {
        NativeBinding binding;
        PxController* controller = nullptr;
        uint64 configurationHash = 0;
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
    std::unordered_map<uint64, std::unique_ptr<ControllerRecord>> controllers;
    std::unordered_map<uint64, PxConvexMesh*> convexMeshes;
    std::unordered_map<uint64, PxTriangleMesh*> triangleMeshes;
    List<PhysicsEvent> events;
    SpaceCache spaceCache;
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

        Log::Info("PhysX 5 CPU-only physics initialized.");
        return true;
    }

    void Shutdown()
    {
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

    uint64 CalculateBodyHash(const ColliderComponent& collider, const RigidBodyComponent* body, const SpaceComponent& space) const
    {
        uint64 hash = 0xCBF29CE484222325ull;
        hash = MixHash(hash, static_cast<uint32>(collider.shape));
        hash = MixHash(hash, collider.isTrigger);
        hash = MixVector(hash, collider.center);
        hash = MixVector(hash, collider.halfExtents);
        hash = MixFloat(hash, collider.radius);
        hash = MixFloat(hash, collider.halfHeight);
        hash = MixFloat(hash, collider.staticFriction);
        hash = MixFloat(hash, collider.dynamicFriction);
        hash = MixFloat(hash, collider.restitution);
        hash = MixHash(hash, collider.collisionLayer);
        hash = MixHash(hash, collider.collisionMask);
        hash = MixVector(hash, GetWorldScale(space));
        hash = MixHash(hash, collider.mesh.GetInstanceId().GetHash());
        Mesh* mesh = collider.mesh.Get();
        hash = MixHash(hash, mesh ? mesh->GetRevision() : 0);

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
        uint64 key = MixHash(mesh.GetInstanceId().GetHash(), mesh.GetRevision());
        auto found = convexMeshes.find(key);
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
        convexMeshes.emplace(key, cooked);
        return cooked;
    }

    PxTriangleMesh* GetTriangleMesh(Mesh& mesh)
    {
        uint64 key = MixHash(mesh.GetInstanceId().GetHash(), mesh.GetRevision());
        auto found = triangleMeshes.find(key);
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
        triangleMeshes.emplace(key, cooked);
        return cooked;
    }

    PxShape* CreateShape(const ColliderComponent& collider, PhysicsBodyType bodyType, const vector3& scale)
    {
        PxMaterial* material = physics->createMaterial(
            std::max(0.0f, collider.staticFriction),
            std::max(0.0f, collider.dynamicFriction),
            std::clamp(collider.restitution, 0.0f, 1.0f));
        if (!material) return nullptr;

        PxShapeFlags flags = PxShapeFlag::eSCENE_QUERY_SHAPE;
        flags |= collider.isTrigger ? PxShapeFlag::eTRIGGER_SHAPE : PxShapeFlag::eSIMULATION_SHAPE;
        PxShape* shape = nullptr;

        if (collider.shape == ColliderShape::Box)
        {
            PxBoxGeometry geometry(
                Positive(collider.halfExtents.x * scale.x),
                Positive(collider.halfExtents.y * scale.y),
                Positive(collider.halfExtents.z * scale.z));
            shape = physics->createShape(geometry, *material, true, flags);
        }
        else if (collider.shape == ColliderShape::Sphere)
        {
            float32 uniformScale = std::max({ Positive(scale.x), Positive(scale.y), Positive(scale.z) });
            shape = physics->createShape(PxSphereGeometry(Positive(collider.radius * uniformScale)), *material, true, flags);
        }
        else if (collider.shape == ColliderShape::Capsule)
        {
            float32 radialScale = std::max(Positive(scale.x), Positive(scale.z));
            PxCapsuleGeometry geometry(Positive(collider.radius * radialScale), Positive(collider.halfHeight * scale.y));
            shape = physics->createShape(geometry, *material, true, flags);
        }
        else
        {
            Mesh* mesh = collider.mesh.Get();
            if (!mesh)
            {
                Log::Warning("Mesh collider has no loaded Mesh resource.");
            }
            else if (collider.shape == ColliderShape::ConvexMesh)
            {
                PxConvexMesh* convex = GetConvexMesh(*mesh);
                if (convex)
                {
                    PxConvexMeshGeometry geometry(convex, PxMeshScale(ToPx(scale)));
                    shape = physics->createShape(geometry, *material, true, flags);
                }
            }
            else if (collider.shape == ColliderShape::TriangleMesh)
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

        material->release();
        if (!shape) return nullptr;

        PxFilterData filter(collider.collisionLayer, collider.collisionMask, 0, 0);
        shape->setSimulationFilterData(filter);
        shape->setQueryFilterData(filter);
        PxVec3 center(collider.center.x * scale.x, collider.center.y * scale.y, collider.center.z * scale.z);
        PxQuat localRotation = collider.shape == ColliderShape::Capsule
            ? PxQuat(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f))
            : PxQuat(PxIdentity);
        shape->setLocalPose(PxTransform(center, localRotation));
        return shape;
    }

    std::unique_ptr<BodyRecord> CreateBody(ColliderComponent& collider, RigidBodyComponent* body, SpaceComponent& space, uint64 configurationHash)
    {
        PhysicsBodyType bodyType = body ? body->bodyType : PhysicsBodyType::Static;
        if (bodyType == PhysicsBodyType::Dynamic && !space.parent.IsNull())
        {
            Log::Warning("Dynamic rigid bodies must be root entities; the collider was skipped.");
            return nullptr;
        }

        std::unique_ptr<BodyRecord> record = std::make_unique<BodyRecord>();
        record->binding.kind = BindingKind::Body;
        record->binding.ens = collider.GetEnsId();
        record->binding.owner = this;
        record->configurationHash = configurationHash;
        record->bodyType = bodyType;

        PxTransform pose(ToPx(space.worldPosition), ToPx(space.worldRotation));
        PxRigidActor* actor = bodyType == PhysicsBodyType::Static
            ? static_cast<PxRigidActor*>(physics->createRigidStatic(pose))
            : static_cast<PxRigidActor*>(physics->createRigidDynamic(pose));
        if (!actor) return nullptr;

        PxShape* shape = CreateShape(collider, bodyType, GetWorldScale(space));
        if (!shape)
        {
            actor->release();
            Log::Warning("PhysX collider geometry creation failed.");
            return nullptr;
        }

        actor->attachShape(*shape);
        shape->release();
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
                PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, body ? Positive(body->mass) : 1.0f);
                dynamic->setLinearVelocity(body ? ToPx(body->linearVelocity) : PxVec3(0.0f), false);
                dynamic->setAngularVelocity(body ? ToPx(body->angularVelocity) : PxVec3(0.0f), false);
            }
        }

        scene->addActor(*actor);
        record->lastPosition = space.worldPosition;
        record->lastRotation = space.worldRotation;
        record->lastPoseValid = true;
        return record;
    }

    void DestroyBody(BodyRecord& record)
    {
        if (!record.actor) return;
        record.actor->userData = nullptr;
        record.actor->release();
        record.actor = nullptr;
    }

    void SyncBodyPoseAndVelocity(BodyRecord& record, RigidBodyComponent* body, SpaceComponent& space)
    {
        PxTransform pose(ToPx(space.worldPosition), ToPx(space.worldRotation));
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
                if (!record.lastPoseValid || !NearlyEqual(space.worldPosition, record.lastPosition) || !NearlyEqual(space.worldRotation, record.lastRotation))
                {
                    dynamic->setGlobalPose(pose, true);
                }
                if (body)
                {
                    dynamic->setLinearVelocity(ToPx(body->linearVelocity), false);
                    dynamic->setAngularVelocity(ToPx(body->angularVelocity), false);
                }
            }
        }
        record.lastPosition = space.worldPosition;
        record.lastRotation = space.worldRotation;
        record.lastPoseValid = true;
    }

    void SyncBodies(World& currentWorld)
    {
        std::unordered_set<uint64> seen;
        currentWorld.ForEachComponent<ColliderComponent>([&](ColliderComponent* collider)
        {
            if (!collider || !collider->enabled) return;
            Ens* ens = collider->GetEns();
            SpaceComponent* space = ens ? ens->Space() : nullptr;
            RigidBodyComponent* body = ens ? ens->GetComponent<RigidBodyComponent>() : nullptr;
            if (!space || (body && !body->enabled)) return;

            uint64 key = EnsKey(collider->GetEnsId());
            seen.insert(key);
            uint64 hash = CalculateBodyHash(*collider, body, *space);
            auto found = bodies.find(key);
            if (found != bodies.end() && found->second->configurationHash != hash)
            {
                DestroyBody(*found->second);
                bodies.erase(found);
                found = bodies.end();
            }
            if (found == bodies.end())
            {
                std::unique_ptr<BodyRecord> created = CreateBody(*collider, body, *space, hash);
                if (created) bodies.emplace(key, std::move(created));
                return;
            }
            SyncBodyPoseAndVelocity(*found->second, body, *space);
        });

        for (auto it = bodies.begin(); it != bodies.end();)
        {
            if (seen.contains(it->first))
            {
                ++it;
                continue;
            }
            DestroyBody(*it->second);
            it = bodies.erase(it);
        }
    }

    uint64 CalculateControllerHash(const CharacterControllerComponent& component) const
    {
        uint64 hash = 0xCBF29CE484222325ull;
        hash = MixHash(hash, static_cast<uint32>(component.shape));
        hash = MixFloat(hash, component.radius);
        hash = MixFloat(hash, component.height);
        hash = MixVector(hash, component.halfExtents);
        hash = MixFloat(hash, component.stepOffset);
        hash = MixFloat(hash, component.contactOffset);
        hash = MixFloat(hash, component.slopeLimit);
        hash = MixHash(hash, component.collisionLayer);
        return MixHash(hash, component.collisionMask);
    }

    std::unique_ptr<ControllerRecord> CreateController(CharacterControllerComponent& component, SpaceComponent& space, uint64 configurationHash)
    {
        if (!space.parent.IsNull())
        {
            Log::Warning("Character controllers must be root entities; the controller was skipped.");
            return nullptr;
        }

        std::unique_ptr<ControllerRecord> record = std::make_unique<ControllerRecord>();
        record->binding.kind = BindingKind::Controller;
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
            desc.position = PxExtendedVec3(space.worldPosition.x, space.worldPosition.y + desc.radius + desc.height * 0.5f, space.worldPosition.z);
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
            desc.position = PxExtendedVec3(space.worldPosition.x, space.worldPosition.y + desc.halfHeight, space.worldPosition.z);
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
        controller->setFootPosition(PxExtendedVec3(space.worldPosition.x, space.worldPosition.y, space.worldPosition.z));
        PxRigidDynamic* actor = controller->getActor();
        actor->userData = &record->binding;
        PxShape* controllerShape = nullptr;
        if (actor->getShapes(&controllerShape, 1) == 1 && controllerShape)
        {
            PxFilterData filter(component.collisionLayer, component.collisionMask, 0, 0);
            controllerShape->setSimulationFilterData(filter);
            controllerShape->setQueryFilterData(filter);
        }
        record->lastFootPosition = space.worldPosition;
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
        currentWorld.ForEachComponent<CharacterControllerComponent>([&](CharacterControllerComponent* component)
        {
            if (!component || !component->enabled) return;
            Ens* ens = component->GetEns();
            SpaceComponent* space = ens ? ens->Space() : nullptr;
            if (!space) return;

            uint64 key = EnsKey(component->GetEnsId());
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
                std::unique_ptr<ControllerRecord> created = CreateController(*component, *space, hash);
                if (created) controllers.emplace(key, std::move(created));
                return;
            }

            ControllerRecord& record = *found->second;
            if (!record.lastPoseValid || !NearlyEqual(record.lastFootPosition, space->worldPosition))
            {
                record.controller->setFootPosition(PxExtendedVec3(space->worldPosition.x, space->worldPosition.y, space->worldPosition.z));
                record.lastFootPosition = space->worldPosition;
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

    void WriteDynamicPoses(World& currentWorld)
    {
        for (auto& entry : bodies)
        {
            BodyRecord& record = *entry.second;
            if (record.bodyType != PhysicsBodyType::Dynamic) continue;
            SpaceComponent* space = currentWorld.GetSpaceComponent(record.binding.ens);
            Ens* ens = currentWorld.GetEns(record.binding.ens);
            RigidBodyComponent* body = ens ? ens->GetComponent<RigidBodyComponent>() : nullptr;
            if (!space || !body) continue;

            PxRigidDynamic* dynamic = static_cast<PxRigidDynamic*>(record.actor);
            PxTransform pose = dynamic->getGlobalPose();
            space->SetLocalPosition(FromPx(pose.p));
            space->SetLocalRotation(FromPx(pose.q));
            body->linearVelocity = FromPx(dynamic->getLinearVelocity());
            body->angularVelocity = FromPx(dynamic->getAngularVelocity());
            record.lastPosition = space->GetLocalPosition();
            record.lastRotation = space->GetLocalRotation();
            record.lastPoseValid = true;
        }
    }

    void FixedUpdate(World& currentWorld, float32 deltaTime)
    {
        if (!Initialize()) return;
        world = &currentWorld;
        events.clear();
        spaceCache.Update(currentWorld);
        SyncBodies(currentWorld);
        SyncControllers(currentWorld);
        scene->simulate(deltaTime);
        scene->fetchResults(true);
        WriteDynamicPoses(currentWorld);
    }

    bool FillHit(const PxLocationHit& nativeHit, const PxRigidActor* actor, PhysicsQueryHit& hit) const
    {
        EnsId ens = FindEns(actor);
        if (ens.IsNull()) return false;
        hit.ens = ens;
        hit.position = FromPx(nativeHit.position);
        hit.normal = FromPx(nativeHit.normal);
        hit.distance = nativeHit.distance;
        return true;
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
bool PhysicsSystem::OnInitialize(Application& app)
{
    (void)app;
    return Initialize();
}

//关闭并释放物理系统
void PhysicsSystem::OnShutdown()
{
    Shutdown();
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
void PhysicsSystem::FixedUpdate(World& world, float fixedDeltaTime)
{
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
    return impl->FillHit(result.block, result.block.actor, hit);
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
    return impl->FillHit(result.block, result.block.actor, hit);
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
        EnsId ens = impl->FindEns(result.getTouch(index).actor);
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
    auto found = impl->controllers.find(EnsKey(ens));
    if (found == impl->controllers.end()) return CharacterCollisionNone;

    Ens* entity = impl->world->GetEns(ens);
    CharacterControllerComponent* component = entity ? entity->GetComponent<CharacterControllerComponent>() : nullptr;
    if (!component) return CharacterCollisionNone;

    LayerQueryFilter callback(component->collisionMask, false, component->collisionLayer);
    ControllerPairFilter controllerPairFilter;
    PxFilterData filterData(component->collisionMask, 0, 0, 0);
    PxControllerFilters filters(&filterData, &callback, &controllerPairFilter);
    PxControllerCollisionFlags nativeFlags = found->second->controller->move(
        ToPx(displacement), std::max(0.0f, component->minMoveDistance), deltaTime, filters);

    PxExtendedVec3 foot = found->second->controller->getFootPosition();
    vector3 position = { static_cast<float32>(foot.x), static_cast<float32>(foot.y), static_cast<float32>(foot.z) };
    SpaceComponent* space = impl->world->GetSpaceComponent(ens);
    if (space)
    {
        space->SetLocalPosition(position);
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
        SpaceComponent* space = impl->world->GetSpaceComponent(ens);
        if (space)
        {
            space->SetLocalPosition(footPosition);
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
    auto found = impl->bodies.find(EnsKey(ens));
    return found != impl->bodies.end() ? found->second->actor : nullptr;
}

//查找实体对应的原生角色控制器
PxController* PhysicsSystem::GetController(EnsId ens) const
{
    if (!impl) return nullptr;
    auto found = impl->controllers.find(EnsKey(ens));
    return found != impl->controllers.end() ? found->second->controller : nullptr;
}
