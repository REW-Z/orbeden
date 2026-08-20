#include "Runtime/Native/RuntimeComponentBinds.h"

#include "FileSystem/PathDefines.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Runtime/Ens.h"
#include "Runtime/Native/NativeCall.h"
#include "Runtime/Object/TransformComponent.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/World.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
    //从 UTF-8 字节创建字符串。
    std::string ReadUtf8Text(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return std::string();
        return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(length));
    }

    //复制 UTF-8 字符串到 C# 缓冲区
    int32 CopyText(const std::string& text, uint8* buffer, int32 bufferSize)
    {
        int32 byteCount = static_cast<int32>(text.size());
        if (buffer && bufferSize > 0 && byteCount > 0)
        {
            int32 copyCount = std::min(byteCount, bufferSize);
            std::memcpy(buffer, text.data(), static_cast<size_t>(copyCount));
        }

        return byteCount;
    }

    //获取当前 World 中的唯一 Ens 实例。
    Ens* GetNativeEns(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world ? world->GetEns(ens) : nullptr;
    }

    //获取当前 World 中的 TransformComponent。
    TransformComponent* GetNativeTransform(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world ? world->GetTransformComponent(ens) : nullptr;
    }

    //获取当前 World 中的 StaticMeshRenderer。
    StaticMeshRenderer* GetNativeStaticMeshRenderer(EnsId ens)
    {
        World* world = World::CurrentWorld();
        Component* component = world ? world->GetComponent(ens, StaticMeshRenderer::StaticType()) : nullptr;
        return component ? component->Cast<StaticMeshRenderer>() : nullptr;
    }

    //获取当前 World 中的 RigidBodyComponent。
    RigidBodyComponent* GetNativeRigidBody(EnsId ens)
    {
        World* world = World::CurrentWorld();
        Component* component = world ? world->GetComponent(ens, RigidBodyComponent::StaticType()) : nullptr;
        return component ? component->Cast<RigidBodyComponent>() : nullptr;
    }

    //获取原生 ColliderComponent。
    ColliderComponent* GetNativeCollider(void* pointer)
    {
        Object* object = static_cast<Object*>(pointer);
        if (!object || !Object::IsObjectAlive(object->GetObjectId())) return nullptr;
        return object->Cast<ColliderComponent>();
    }

    //收集 Ens 挂载的全部 ColliderComponent。
    void GetNativeColliders(EnsId ens, List<ColliderComponent*>& output)
    {
        output.clear();
        Ens* value = GetNativeEns(ens);
        if (!value) return;

        for (Component* component : value->GetComponents())
        {
            ColliderComponent* collider = component ? component->Cast<ColliderComponent>() : nullptr;
            if (collider) output.push_back(collider);
        }
    }

    //获取当前 World 中的 CharacterControllerComponent。
    CharacterControllerComponent* GetNativeCharacterController(EnsId ens)
    {
        World* world = World::CurrentWorld();
        Component* component = world ? world->GetComponent(ens, CharacterControllerComponent::StaticType()) : nullptr;
        return component ? component->Cast<CharacterControllerComponent>() : nullptr;
    }

    //创建 Ens。
    EnsId ORBEDEN_NATIVE_CALL NativeWorldCreateEns(const uint8* name, int32 length)
    {
        World* world = World::CurrentWorld();
        Ens* ens = world ? world->CreateEns(ReadUtf8Text(name, length)) : nullptr;
        return ens ? ens->GetId() : EnsId();
    }

    //使用稳定 ID 创建 Ens。
    EnsId ORBEDEN_NATIVE_CALL NativeWorldCreateEnsWithStableId(const uint8* stableId, int32 stableIdLength, const uint8* name, int32 nameLength)
    {
        World* world = World::CurrentWorld();
        Ens* ens = world ? world->CreateEnsWithStableId(ReadUtf8Text(stableId, stableIdLength), ReadUtf8Text(name, nameLength)) : nullptr;
        return ens ? ens->GetId() : EnsId();
    }

    //按稳定 ID 查找 Ens。
    EnsId ORBEDEN_NATIVE_CALL NativeWorldFindEns(const uint8* stableId, int32 stableIdLength)
    {
        World* world = World::CurrentWorld();
        Ens* ens = world ? world->FindEns(StringId(ReadUtf8Text(stableId, stableIdLength))) : nullptr;
        return ens ? ens->GetId() : EnsId();
    }

    //销毁 Ens。
    uint8 ORBEDEN_NATIVE_CALL NativeWorldDestroyEns(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world && world->DestroyEns(ens) ? 1 : 0;
    }

    //读取当前内容根目录。
    int32 ORBEDEN_NATIVE_CALL NativePathDefinesGetContentRoot(uint8* buffer, int32 bufferSize)
    {
        return CopyText(PathDefines::GetContentRoot(), buffer, bufferSize);
    }

    //解析内容相对路径。
    int32 ORBEDEN_NATIVE_CALL NativePathDefinesGetContentFilePath(const uint8* path, int32 length, uint8* buffer, int32 bufferSize)
    {
        return CopyText(PathDefines::GetContentFilePath(ReadUtf8Text(path, length)), buffer, bufferSize);
    }

    //判断 Ens 是否有效。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsIsAlive(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world && world->IsAlive(ens) ? 1 : 0;
    }

    //读取 Ens 的 localActive。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsGetLocalActive(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value && value->GetLocalActive() ? 1 : 0;
    }

    //读取 Ens 的 worldActive。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsGetWorldActive(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value && value->GetWorldActive() ? 1 : 0;
    }

    //设置 Ens 的 localActive。
    void ORBEDEN_NATIVE_CALL NativeEnsSetLocalActive(EnsId ens, uint8 active)
    {
        Ens* value = GetNativeEns(ens);
        if (value) value->SetLocalActive(active != 0);
    }

    //读取 Ens 名称到 UTF-8 缓冲区。
    int32 ORBEDEN_NATIVE_CALL NativeEnsGetName(EnsId ens, uint8* buffer, int32 bufferSize)
    {
        Ens* value = GetNativeEns(ens);
        static const std::string emptyName;
        const std::string& name = value ? value->GetName() : emptyName;
        return CopyText(name, buffer, bufferSize);
    }

    //写入 Ens 名称。
    void ORBEDEN_NATIVE_CALL NativeEnsSetName(EnsId ens, const uint8* text, int32 length)
    {
        Ens* value = GetNativeEns(ens);
        if (value) value->SetName(ReadUtf8Text(text, length));
    }

    //判断 Ens 是否拥有 TransformComponent。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsHasTransformComponent(EnsId ens)
    {
        return GetNativeTransform(ens) ? 1 : 0;
    }

    //判断 Ens 是否拥有 StaticMeshRenderer。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsHasStaticMeshRenderer(EnsId ens)
    {
        return GetNativeStaticMeshRenderer(ens) ? 1 : 0;
    }

    //判断 Ens 是否拥有 RigidBodyComponent。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsHasRigidBody(EnsId ens)
    {
        return GetNativeRigidBody(ens) ? 1 : 0;
    }

    //判断 Ens 是否拥有 CharacterControllerComponent。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsHasCharacterController(EnsId ens)
    {
        return GetNativeCharacterController(ens) ? 1 : 0;
    }

    //添加 StaticMeshRenderer。
    void* ORBEDEN_NATIVE_CALL NativeEnsAddStaticMeshRenderer(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value ? value->AddComponent<StaticMeshRenderer>() : nullptr;
    }

    //添加 RigidBodyComponent。
    void* ORBEDEN_NATIVE_CALL NativeEnsAddRigidBody(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value ? value->AddComponent<RigidBodyComponent>() : nullptr;
    }

    //添加 BoxColliderComponent。
    void* ORBEDEN_NATIVE_CALL NativeEnsAddBoxCollider(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value ? value->AddComponentInstance<BoxColliderComponent>() : nullptr;
    }

    //添加 SphereColliderComponent。
    void* ORBEDEN_NATIVE_CALL NativeEnsAddSphereCollider(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value ? value->AddComponentInstance<SphereColliderComponent>() : nullptr;
    }

    //添加 CapsuleColliderComponent。
    void* ORBEDEN_NATIVE_CALL NativeEnsAddCapsuleCollider(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value ? value->AddComponentInstance<CapsuleColliderComponent>() : nullptr;
    }

    //添加 ConvexMeshColliderComponent。
    void* ORBEDEN_NATIVE_CALL NativeEnsAddConvexMeshCollider(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value ? value->AddComponentInstance<ConvexMeshColliderComponent>() : nullptr;
    }

    //添加 TriangleMeshColliderComponent。
    void* ORBEDEN_NATIVE_CALL NativeEnsAddTriangleMeshCollider(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value ? value->AddComponentInstance<TriangleMeshColliderComponent>() : nullptr;
    }

    //添加 CharacterControllerComponent。
    void* ORBEDEN_NATIVE_CALL NativeEnsAddCharacterController(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value ? value->AddComponent<CharacterControllerComponent>() : nullptr;
    }

    //获取 TransformComponent。
    void* ORBEDEN_NATIVE_CALL NativeEnsGetTransformComponent(EnsId ens)
    {
        return GetNativeTransform(ens);
    }

    //获取 StaticMeshRenderer。
    void* ORBEDEN_NATIVE_CALL NativeEnsGetStaticMeshRenderer(EnsId ens)
    {
        return GetNativeStaticMeshRenderer(ens);
    }

    //获取 RigidBodyComponent。
    void* ORBEDEN_NATIVE_CALL NativeEnsGetRigidBody(EnsId ens)
    {
        return GetNativeRigidBody(ens);
    }

    //获取 Ens 挂载的 ColliderComponent 数量。
    int32 ORBEDEN_NATIVE_CALL NativeEnsGetColliderCount(EnsId ens)
    {
        List<ColliderComponent*> colliders;
        GetNativeColliders(ens, colliders);
        return static_cast<int32>(colliders.size());
    }

    //获取 Ens 挂载的指定 ColliderComponent。
    void* ORBEDEN_NATIVE_CALL NativeEnsGetColliderAt(EnsId ens, int32 index)
    {
        List<ColliderComponent*> colliders;
        GetNativeColliders(ens, colliders);
        return index >= 0 && static_cast<usize>(index) < colliders.size() ? colliders[index] : nullptr;
    }

    //获取 ColliderComponent 的具体几何类型。
    uint32 ORBEDEN_NATIVE_CALL NativeColliderGetGeometryType(void* pointer)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        return collider ? static_cast<uint32>(collider->GetGeometryType()) : static_cast<uint32>(ColliderGeometryType::Box);
    }

    //获取 CharacterControllerComponent。
    void* ORBEDEN_NATIVE_CALL NativeEnsGetCharacterController(EnsId ens)
    {
        return GetNativeCharacterController(ens);
    }

    //读取父级 Ens。
    EnsId ORBEDEN_NATIVE_CALL NativeTransformGetParent(EnsId ens)
    {
        TransformComponent* transform = GetNativeTransform(ens);
        return transform ? transform->parent : EnsId();
    }

    //设置父级 Ens。
    void ORBEDEN_NATIVE_CALL NativeTransformSetParent(EnsId ens, EnsId parent)
    {
        World* world = World::CurrentWorld();
        if (world) world->SetParent(ens, parent);
    }

    //读取本地位置。
    vector3 ORBEDEN_NATIVE_CALL NativeTransformGetLocalPosition(EnsId ens)
    {
        TransformComponent* transform = GetNativeTransform(ens);
        return transform ? transform->GetLocalPosition() : vector3();
    }

    //写入本地位置。
    void ORBEDEN_NATIVE_CALL NativeTransformSetLocalPosition(EnsId ens, vector3 value)
    {
        TransformComponent* transform = GetNativeTransform(ens);
        if (transform) transform->SetLocalPosition(value);
    }

    //读取本地旋转。
    quaternion ORBEDEN_NATIVE_CALL NativeTransformGetLocalRotation(EnsId ens)
    {
        TransformComponent* transform = GetNativeTransform(ens);
        return transform ? transform->GetLocalRotation() : quaternion();
    }

    //写入本地旋转。
    void ORBEDEN_NATIVE_CALL NativeTransformSetLocalRotation(EnsId ens, quaternion value)
    {
        TransformComponent* transform = GetNativeTransform(ens);
        if (transform) transform->SetLocalRotation(value);
    }

    //读取本地缩放。
    vector3 ORBEDEN_NATIVE_CALL NativeTransformGetLocalScale(EnsId ens)
    {
        TransformComponent* transform = GetNativeTransform(ens);
        return transform ? transform->GetLocalScale() : vector3{ 1.0f, 1.0f, 1.0f };
    }

    //写入本地缩放。
    void ORBEDEN_NATIVE_CALL NativeTransformSetLocalScale(EnsId ens, vector3 value)
    {
        TransformComponent* transform = GetNativeTransform(ens);
        if (transform) transform->SetLocalScale(value);
    }

    //读取世界位置。
    vector3 ORBEDEN_NATIVE_CALL NativeTransformGetWorldPosition(EnsId ens)
    {
        TransformComponent* transform = GetNativeTransform(ens);
        return transform ? transform->worldPosition : vector3();
    }

    //读取世界旋转。
    quaternion ORBEDEN_NATIVE_CALL NativeTransformGetWorldRotation(EnsId ens)
    {
        TransformComponent* transform = GetNativeTransform(ens);
        return transform ? transform->worldRotation : quaternion();
    }

    //读取 StaticMeshRenderer.enabled。
    uint8 ORBEDEN_NATIVE_CALL NativeStaticMeshRendererGetEnabled(EnsId ens)
    {
        StaticMeshRenderer* renderer = GetNativeStaticMeshRenderer(ens);
        return renderer && renderer->GetEnabled() ? 1 : 0;
    }

    //写入 StaticMeshRenderer.enabled。
    void ORBEDEN_NATIVE_CALL NativeStaticMeshRendererSetEnabled(EnsId ens, uint8 value)
    {
        StaticMeshRenderer* renderer = GetNativeStaticMeshRenderer(ens);
        if (renderer) renderer->SetEnabled(value != 0);
    }

    //读取 StaticMeshRenderer.mesh。
    void* ORBEDEN_NATIVE_CALL NativeStaticMeshRendererGetMesh(EnsId ens)
    {
        StaticMeshRenderer* renderer = GetNativeStaticMeshRenderer(ens);
        return renderer ? renderer->mesh.Get() : nullptr;
    }

    //写入 StaticMeshRenderer.mesh。
    uint8 ORBEDEN_NATIVE_CALL NativeStaticMeshRendererSetMesh(EnsId ens, void* meshPointer)
    {
        StaticMeshRenderer* renderer = GetNativeStaticMeshRenderer(ens);
        if (!renderer) return 0;

        if (!meshPointer)
        {
            renderer->mesh.SetInstanceId(StringId());
            return 1;
        }

        Mesh* mesh = static_cast<Object*>(meshPointer)->Cast<Mesh>();
        if (!mesh || Object::FindObjectById(mesh->GetObjectId()) != mesh) return 0;

        renderer->mesh.Set(mesh);
        return 1;
    }

    //读取 StaticMeshRenderer.drawQueue。
    uint32 ORBEDEN_NATIVE_CALL NativeStaticMeshRendererGetDrawQueue(EnsId ens)
    {
        StaticMeshRenderer* renderer = GetNativeStaticMeshRenderer(ens);
        return renderer ? static_cast<uint32>(renderer->drawQueue) : static_cast<uint32>(DrawQueue::Opaque);
    }

    //写入 StaticMeshRenderer.drawQueue。
    void ORBEDEN_NATIVE_CALL NativeStaticMeshRendererSetDrawQueue(EnsId ens, uint32 value)
    {
        if (value > static_cast<uint32>(DrawQueue::Refraction)) return;

        StaticMeshRenderer* renderer = GetNativeStaticMeshRenderer(ens);
        if (renderer) renderer->drawQueue = static_cast<DrawQueue>(value);
    }

    //读取 StaticMeshRenderer.castShadows。
    uint8 ORBEDEN_NATIVE_CALL NativeStaticMeshRendererGetCastShadows(EnsId ens)
    {
        StaticMeshRenderer* renderer = GetNativeStaticMeshRenderer(ens);
        return renderer && renderer->castShadows ? 1 : 0;
    }

    //写入 StaticMeshRenderer.castShadows。
    void ORBEDEN_NATIVE_CALL NativeStaticMeshRendererSetCastShadows(EnsId ens, uint8 value)
    {
        StaticMeshRenderer* renderer = GetNativeStaticMeshRenderer(ens);
        if (renderer) renderer->castShadows = value != 0;
    }

    //读取 StaticMeshRenderer.receiveShadows。
    uint8 ORBEDEN_NATIVE_CALL NativeStaticMeshRendererGetReceiveShadows(EnsId ens)
    {
        StaticMeshRenderer* renderer = GetNativeStaticMeshRenderer(ens);
        return renderer && renderer->receiveShadows ? 1 : 0;
    }

    //写入 StaticMeshRenderer.receiveShadows。
    void ORBEDEN_NATIVE_CALL NativeStaticMeshRendererSetReceiveShadows(EnsId ens, uint8 value)
    {
        StaticMeshRenderer* renderer = GetNativeStaticMeshRenderer(ens);
        if (renderer) renderer->receiveShadows = value != 0;
    }

    //读取 RigidBody.enabled。
    uint8 ORBEDEN_NATIVE_CALL NativeRigidBodyGetEnabled(EnsId ens)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        return body && body->enabled ? 1 : 0;
    }

    //写入 RigidBody.enabled。
    void ORBEDEN_NATIVE_CALL NativeRigidBodySetEnabled(EnsId ens, uint8 value)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        if (body) body->enabled = value != 0;
    }

    //读取 RigidBody.bodyType。
    uint32 ORBEDEN_NATIVE_CALL NativeRigidBodyGetBodyType(EnsId ens)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        return body ? static_cast<uint32>(body->bodyType) : 0;
    }

    //写入 RigidBody.bodyType。
    void ORBEDEN_NATIVE_CALL NativeRigidBodySetBodyType(EnsId ens, uint32 value)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        if (body) body->bodyType = static_cast<PhysicsBodyType>(value);
    }

    //读取 RigidBody.mass。
    float32 ORBEDEN_NATIVE_CALL NativeRigidBodyGetMass(EnsId ens)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        return body ? body->mass : 0.0f;
    }

    //写入 RigidBody.mass。
    void ORBEDEN_NATIVE_CALL NativeRigidBodySetMass(EnsId ens, float32 value)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        if (body) body->mass = value;
    }

    //读取 RigidBody.useGravity。
    uint8 ORBEDEN_NATIVE_CALL NativeRigidBodyGetUseGravity(EnsId ens)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        return body && body->useGravity ? 1 : 0;
    }

    //写入 RigidBody.useGravity。
    void ORBEDEN_NATIVE_CALL NativeRigidBodySetUseGravity(EnsId ens, uint8 value)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        if (body) body->useGravity = value != 0;
    }

    //读取 RigidBody.linearDamping。
    float32 ORBEDEN_NATIVE_CALL NativeRigidBodyGetLinearDamping(EnsId ens)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        return body ? body->linearDamping : 0.0f;
    }

    //写入 RigidBody.linearDamping。
    void ORBEDEN_NATIVE_CALL NativeRigidBodySetLinearDamping(EnsId ens, float32 value)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        if (body) body->linearDamping = value;
    }

    //读取 RigidBody.angularDamping。
    float32 ORBEDEN_NATIVE_CALL NativeRigidBodyGetAngularDamping(EnsId ens)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        return body ? body->angularDamping : 0.0f;
    }

    //写入 RigidBody.angularDamping。
    void ORBEDEN_NATIVE_CALL NativeRigidBodySetAngularDamping(EnsId ens, float32 value)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        if (body) body->angularDamping = value;
    }

    //读取 RigidBody.linearVelocity。
    vector3 ORBEDEN_NATIVE_CALL NativeRigidBodyGetLinearVelocity(EnsId ens)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        return body ? body->linearVelocity : vector3();
    }

    //写入 RigidBody.linearVelocity。
    void ORBEDEN_NATIVE_CALL NativeRigidBodySetLinearVelocity(EnsId ens, vector3 value)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        if (body) body->linearVelocity = value;
    }

    //读取 RigidBody.angularVelocity。
    vector3 ORBEDEN_NATIVE_CALL NativeRigidBodyGetAngularVelocity(EnsId ens)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        return body ? body->angularVelocity : vector3();
    }

    //写入 RigidBody.angularVelocity。
    void ORBEDEN_NATIVE_CALL NativeRigidBodySetAngularVelocity(EnsId ens, vector3 value)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        if (body) body->angularVelocity = value;
    }

    //读取 RigidBody.continuousCollisionDetection。
    uint8 ORBEDEN_NATIVE_CALL NativeRigidBodyGetContinuousCollisionDetection(EnsId ens)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        return body && body->continuousCollisionDetection ? 1 : 0;
    }

    //写入 RigidBody.continuousCollisionDetection。
    void ORBEDEN_NATIVE_CALL NativeRigidBodySetContinuousCollisionDetection(EnsId ens, uint8 value)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        if (body) body->continuousCollisionDetection = value != 0;
    }

    //读取 RigidBody.lockFlags。
    uint32 ORBEDEN_NATIVE_CALL NativeRigidBodyGetLockFlags(EnsId ens)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        return body ? body->lockFlags : PhysicsLockNone;
    }

    //写入 RigidBody.lockFlags。
    void ORBEDEN_NATIVE_CALL NativeRigidBodySetLockFlags(EnsId ens, uint32 value)
    {
        RigidBodyComponent* body = GetNativeRigidBody(ens);
        if (body) body->lockFlags = value;
    }

    //读取 Collider.enabled。
    uint8 ORBEDEN_NATIVE_CALL NativeColliderGetEnabled(void* pointer)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        return collider && collider->enabled ? 1 : 0;
    }

    //写入 Collider.enabled。
    void ORBEDEN_NATIVE_CALL NativeColliderSetEnabled(void* pointer, uint8 value)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        if (collider) collider->enabled = value != 0;
    }

    //读取 Collider.isTrigger。
    uint8 ORBEDEN_NATIVE_CALL NativeColliderGetIsTrigger(void* pointer)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        return collider && collider->isTrigger ? 1 : 0;
    }

    //写入 Collider.isTrigger。
    void ORBEDEN_NATIVE_CALL NativeColliderSetIsTrigger(void* pointer, uint8 value)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        if (collider) collider->isTrigger = value != 0;
    }

    //读取 Collider.center。
    vector3 ORBEDEN_NATIVE_CALL NativeColliderGetCenter(void* pointer)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        return collider ? collider->center : vector3();
    }

    //写入 Collider.center。
    void ORBEDEN_NATIVE_CALL NativeColliderSetCenter(void* pointer, vector3 value)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        if (collider) collider->center = value;
    }

    //读取 BoxCollider.halfExtents。
    vector3 ORBEDEN_NATIVE_CALL NativeColliderGetHalfExtents(void* pointer)
    {
        ColliderComponent* base = GetNativeCollider(pointer);
        BoxColliderComponent* collider = base ? base->Cast<BoxColliderComponent>() : nullptr;
        return collider ? collider->halfExtents : vector3();
    }

    //写入 BoxCollider.halfExtents。
    void ORBEDEN_NATIVE_CALL NativeColliderSetHalfExtents(void* pointer, vector3 value)
    {
        ColliderComponent* base = GetNativeCollider(pointer);
        BoxColliderComponent* collider = base ? base->Cast<BoxColliderComponent>() : nullptr;
        if (collider) collider->halfExtents = value;
    }

    //读取 SphereCollider 或 CapsuleCollider.radius。
    float32 ORBEDEN_NATIVE_CALL NativeColliderGetRadius(void* pointer)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        if (SphereColliderComponent* sphere = collider ? collider->Cast<SphereColliderComponent>() : nullptr) return sphere->radius;
        if (CapsuleColliderComponent* capsule = collider ? collider->Cast<CapsuleColliderComponent>() : nullptr) return capsule->radius;
        return 0.0f;
    }

    //写入 SphereCollider 或 CapsuleCollider.radius。
    void ORBEDEN_NATIVE_CALL NativeColliderSetRadius(void* pointer, float32 value)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        if (SphereColliderComponent* sphere = collider ? collider->Cast<SphereColliderComponent>() : nullptr) sphere->radius = value;
        if (CapsuleColliderComponent* capsule = collider ? collider->Cast<CapsuleColliderComponent>() : nullptr) capsule->radius = value;
    }

    //读取 CapsuleCollider.halfHeight。
    float32 ORBEDEN_NATIVE_CALL NativeColliderGetHalfHeight(void* pointer)
    {
        ColliderComponent* base = GetNativeCollider(pointer);
        CapsuleColliderComponent* collider = base ? base->Cast<CapsuleColliderComponent>() : nullptr;
        return collider ? collider->halfHeight : 0.0f;
    }

    //写入 CapsuleCollider.halfHeight。
    void ORBEDEN_NATIVE_CALL NativeColliderSetHalfHeight(void* pointer, float32 value)
    {
        ColliderComponent* base = GetNativeCollider(pointer);
        CapsuleColliderComponent* collider = base ? base->Cast<CapsuleColliderComponent>() : nullptr;
        if (collider) collider->halfHeight = value;
    }

    //读取 MeshCollider.mesh。
    void* ORBEDEN_NATIVE_CALL NativeColliderGetMesh(void* pointer)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        if (ConvexMeshColliderComponent* convex = collider ? collider->Cast<ConvexMeshColliderComponent>() : nullptr) return convex->mesh.Get();
        if (TriangleMeshColliderComponent* triangle = collider ? collider->Cast<TriangleMeshColliderComponent>() : nullptr) return triangle->mesh.Get();
        return nullptr;
    }

    //写入 MeshCollider.mesh。
    uint8 ORBEDEN_NATIVE_CALL NativeColliderSetMesh(void* pointer, void* meshPointer)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        if (!collider) return 0;

        Mesh* mesh = nullptr;
        if (meshPointer)
        {
            Object* object = static_cast<Object*>(meshPointer);
            mesh = object && Object::IsObjectAlive(object->GetObjectId()) ? object->Cast<Mesh>() : nullptr;
            if (!mesh) return 0;
        }

        if (ConvexMeshColliderComponent* convex = collider->Cast<ConvexMeshColliderComponent>())
        {
            if (mesh) convex->mesh.Set(mesh);
            else convex->mesh.SetInstanceId(StringId());
            return 1;
        }
        if (TriangleMeshColliderComponent* triangle = collider->Cast<TriangleMeshColliderComponent>())
        {
            if (mesh) triangle->mesh.Set(mesh);
            else triangle->mesh.SetInstanceId(StringId());
            return 1;
        }
        return 0;
    }

    //读取 Collider.staticFriction。
    float32 ORBEDEN_NATIVE_CALL NativeColliderGetStaticFriction(void* pointer)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        return collider ? collider->staticFriction : 0.0f;
    }

    //写入 Collider.staticFriction。
    void ORBEDEN_NATIVE_CALL NativeColliderSetStaticFriction(void* pointer, float32 value)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        if (collider) collider->staticFriction = value;
    }

    //读取 Collider.dynamicFriction。
    float32 ORBEDEN_NATIVE_CALL NativeColliderGetDynamicFriction(void* pointer)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        return collider ? collider->dynamicFriction : 0.0f;
    }

    //写入 Collider.dynamicFriction。
    void ORBEDEN_NATIVE_CALL NativeColliderSetDynamicFriction(void* pointer, float32 value)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        if (collider) collider->dynamicFriction = value;
    }

    //读取 Collider.restitution。
    float32 ORBEDEN_NATIVE_CALL NativeColliderGetRestitution(void* pointer)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        return collider ? collider->restitution : 0.0f;
    }

    //写入 Collider.restitution。
    void ORBEDEN_NATIVE_CALL NativeColliderSetRestitution(void* pointer, float32 value)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        if (collider) collider->restitution = value;
    }

    //读取 Collider.collisionLayer。
    uint32 ORBEDEN_NATIVE_CALL NativeColliderGetCollisionLayer(void* pointer)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        return collider ? collider->collisionLayer : 0;
    }

    //写入 Collider.collisionLayer。
    void ORBEDEN_NATIVE_CALL NativeColliderSetCollisionLayer(void* pointer, uint32 value)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        if (collider) collider->collisionLayer = value;
    }

    //读取 Collider.collisionMask。
    uint32 ORBEDEN_NATIVE_CALL NativeColliderGetCollisionMask(void* pointer)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        return collider ? collider->collisionMask : 0;
    }

    //写入 Collider.collisionMask。
    void ORBEDEN_NATIVE_CALL NativeColliderSetCollisionMask(void* pointer, uint32 value)
    {
        ColliderComponent* collider = GetNativeCollider(pointer);
        if (collider) collider->collisionMask = value;
    }

    //读取 CharacterController.enabled。
    uint8 ORBEDEN_NATIVE_CALL NativeCharacterControllerGetEnabled(EnsId ens)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        return controller && controller->enabled ? 1 : 0;
    }

    //写入 CharacterController.enabled。
    void ORBEDEN_NATIVE_CALL NativeCharacterControllerSetEnabled(EnsId ens, uint8 value)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        if (controller) controller->enabled = value != 0;
    }

    //读取 CharacterController.shape。
    uint32 ORBEDEN_NATIVE_CALL NativeCharacterControllerGetShape(EnsId ens)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        return controller ? static_cast<uint32>(controller->shape) : 0;
    }

    //写入 CharacterController.shape。
    void ORBEDEN_NATIVE_CALL NativeCharacterControllerSetShape(EnsId ens, uint32 value)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        if (controller) controller->shape = static_cast<CharacterControllerShape>(value);
    }

    //读取 CharacterController.radius。
    float32 ORBEDEN_NATIVE_CALL NativeCharacterControllerGetRadius(EnsId ens)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        return controller ? controller->radius : 0.0f;
    }

    //写入 CharacterController.radius。
    void ORBEDEN_NATIVE_CALL NativeCharacterControllerSetRadius(EnsId ens, float32 value)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        if (controller) controller->radius = value;
    }

    //读取 CharacterController.height。
    float32 ORBEDEN_NATIVE_CALL NativeCharacterControllerGetHeight(EnsId ens)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        return controller ? controller->height : 0.0f;
    }

    //写入 CharacterController.height。
    void ORBEDEN_NATIVE_CALL NativeCharacterControllerSetHeight(EnsId ens, float32 value)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        if (controller) controller->height = value;
    }

    //读取 CharacterController.halfExtents。
    vector3 ORBEDEN_NATIVE_CALL NativeCharacterControllerGetHalfExtents(EnsId ens)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        return controller ? controller->halfExtents : vector3();
    }

    //写入 CharacterController.halfExtents。
    void ORBEDEN_NATIVE_CALL NativeCharacterControllerSetHalfExtents(EnsId ens, vector3 value)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        if (controller) controller->halfExtents = value;
    }

    //读取 CharacterController.stepOffset。
    float32 ORBEDEN_NATIVE_CALL NativeCharacterControllerGetStepOffset(EnsId ens)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        return controller ? controller->stepOffset : 0.0f;
    }

    //写入 CharacterController.stepOffset。
    void ORBEDEN_NATIVE_CALL NativeCharacterControllerSetStepOffset(EnsId ens, float32 value)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        if (controller) controller->stepOffset = value;
    }

    //读取 CharacterController.contactOffset。
    float32 ORBEDEN_NATIVE_CALL NativeCharacterControllerGetContactOffset(EnsId ens)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        return controller ? controller->contactOffset : 0.0f;
    }

    //写入 CharacterController.contactOffset。
    void ORBEDEN_NATIVE_CALL NativeCharacterControllerSetContactOffset(EnsId ens, float32 value)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        if (controller) controller->contactOffset = value;
    }

    //读取 CharacterController.slopeLimit。
    float32 ORBEDEN_NATIVE_CALL NativeCharacterControllerGetSlopeLimit(EnsId ens)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        return controller ? controller->slopeLimit : 0.0f;
    }

    //写入 CharacterController.slopeLimit。
    void ORBEDEN_NATIVE_CALL NativeCharacterControllerSetSlopeLimit(EnsId ens, float32 value)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        if (controller) controller->slopeLimit = value;
    }

    //读取 CharacterController.minMoveDistance。
    float32 ORBEDEN_NATIVE_CALL NativeCharacterControllerGetMinMoveDistance(EnsId ens)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        return controller ? controller->minMoveDistance : 0.0f;
    }

    //写入 CharacterController.minMoveDistance。
    void ORBEDEN_NATIVE_CALL NativeCharacterControllerSetMinMoveDistance(EnsId ens, float32 value)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        if (controller) controller->minMoveDistance = value;
    }

    //读取 CharacterController.collisionLayer。
    uint32 ORBEDEN_NATIVE_CALL NativeCharacterControllerGetCollisionLayer(EnsId ens)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        return controller ? controller->collisionLayer : 0;
    }

    //写入 CharacterController.collisionLayer。
    void ORBEDEN_NATIVE_CALL NativeCharacterControllerSetCollisionLayer(EnsId ens, uint32 value)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        if (controller) controller->collisionLayer = value;
    }

    //读取 CharacterController.collisionMask。
    uint32 ORBEDEN_NATIVE_CALL NativeCharacterControllerGetCollisionMask(EnsId ens)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        return controller ? controller->collisionMask : 0;
    }

    //写入 CharacterController.collisionMask。
    void ORBEDEN_NATIVE_CALL NativeCharacterControllerSetCollisionMask(EnsId ens, uint32 value)
    {
        CharacterControllerComponent* controller = GetNativeCharacterController(ens);
        if (controller) controller->collisionMask = value;
    }
}

WorldBind WorldBind::Create()
{
    WorldBind bind;
    bind.CreateEns = reinterpret_cast<void*>(&NativeWorldCreateEns);
    bind.CreateEnsWithStableId = reinterpret_cast<void*>(&NativeWorldCreateEnsWithStableId);
    bind.FindEns = reinterpret_cast<void*>(&NativeWorldFindEns);
    bind.DestroyEns = reinterpret_cast<void*>(&NativeWorldDestroyEns);
    return bind;
}

PathDefinesBind PathDefinesBind::Create()
{
    PathDefinesBind bind;
    bind.GetContentRoot = reinterpret_cast<void*>(&NativePathDefinesGetContentRoot);
    bind.GetContentFilePath = reinterpret_cast<void*>(&NativePathDefinesGetContentFilePath);
    return bind;
}

EnsBind EnsBind::Create()
{
    EnsBind bind;
    bind.IsAlive = reinterpret_cast<void*>(&NativeEnsIsAlive);
    bind.GetLocalActive = reinterpret_cast<void*>(&NativeEnsGetLocalActive);
    bind.GetWorldActive = reinterpret_cast<void*>(&NativeEnsGetWorldActive);
    bind.SetLocalActive = reinterpret_cast<void*>(&NativeEnsSetLocalActive);
    bind.GetName = reinterpret_cast<void*>(&NativeEnsGetName);
    bind.SetName = reinterpret_cast<void*>(&NativeEnsSetName);
    bind.HasTransformComponent = reinterpret_cast<void*>(&NativeEnsHasTransformComponent);
    bind.HasStaticMeshRenderer = reinterpret_cast<void*>(&NativeEnsHasStaticMeshRenderer);
    bind.AddStaticMeshRenderer = reinterpret_cast<void*>(&NativeEnsAddStaticMeshRenderer);
    bind.GetTransformComponent = reinterpret_cast<void*>(&NativeEnsGetTransformComponent);
    bind.GetStaticMeshRenderer = reinterpret_cast<void*>(&NativeEnsGetStaticMeshRenderer);
    return bind;
}

TransformComponentBind TransformComponentBind::Create()
{
    TransformComponentBind bind;
    bind.GetParent = reinterpret_cast<void*>(&NativeTransformGetParent);
    bind.SetParent = reinterpret_cast<void*>(&NativeTransformSetParent);
    bind.GetLocalPosition = reinterpret_cast<void*>(&NativeTransformGetLocalPosition);
    bind.SetLocalPosition = reinterpret_cast<void*>(&NativeTransformSetLocalPosition);
    bind.GetLocalRotation = reinterpret_cast<void*>(&NativeTransformGetLocalRotation);
    bind.SetLocalRotation = reinterpret_cast<void*>(&NativeTransformSetLocalRotation);
    bind.GetLocalScale = reinterpret_cast<void*>(&NativeTransformGetLocalScale);
    bind.SetLocalScale = reinterpret_cast<void*>(&NativeTransformSetLocalScale);
    bind.GetWorldPosition = reinterpret_cast<void*>(&NativeTransformGetWorldPosition);
    bind.GetWorldRotation = reinterpret_cast<void*>(&NativeTransformGetWorldRotation);
    return bind;
}

StaticMeshRendererBind StaticMeshRendererBind::Create()
{
    StaticMeshRendererBind bind;
    bind.GetEnabled = reinterpret_cast<void*>(&NativeStaticMeshRendererGetEnabled);
    bind.SetEnabled = reinterpret_cast<void*>(&NativeStaticMeshRendererSetEnabled);
    bind.GetMesh = reinterpret_cast<void*>(&NativeStaticMeshRendererGetMesh);
    bind.SetMesh = reinterpret_cast<void*>(&NativeStaticMeshRendererSetMesh);
    bind.GetDrawQueue = reinterpret_cast<void*>(&NativeStaticMeshRendererGetDrawQueue);
    bind.SetDrawQueue = reinterpret_cast<void*>(&NativeStaticMeshRendererSetDrawQueue);
    bind.GetCastShadows = reinterpret_cast<void*>(&NativeStaticMeshRendererGetCastShadows);
    bind.SetCastShadows = reinterpret_cast<void*>(&NativeStaticMeshRendererSetCastShadows);
    bind.GetReceiveShadows = reinterpret_cast<void*>(&NativeStaticMeshRendererGetReceiveShadows);
    bind.SetReceiveShadows = reinterpret_cast<void*>(&NativeStaticMeshRendererSetReceiveShadows);
    return bind;
}

RigidBodyBind RigidBodyBind::Create()
{
    RigidBodyBind bind;
    bind.HasComponent = reinterpret_cast<void*>(&NativeEnsHasRigidBody);
    bind.AddComponent = reinterpret_cast<void*>(&NativeEnsAddRigidBody);
    bind.GetComponent = reinterpret_cast<void*>(&NativeEnsGetRigidBody);
    bind.GetEnabled = reinterpret_cast<void*>(&NativeRigidBodyGetEnabled);
    bind.SetEnabled = reinterpret_cast<void*>(&NativeRigidBodySetEnabled);
    bind.GetBodyType = reinterpret_cast<void*>(&NativeRigidBodyGetBodyType);
    bind.SetBodyType = reinterpret_cast<void*>(&NativeRigidBodySetBodyType);
    bind.GetMass = reinterpret_cast<void*>(&NativeRigidBodyGetMass);
    bind.SetMass = reinterpret_cast<void*>(&NativeRigidBodySetMass);
    bind.GetUseGravity = reinterpret_cast<void*>(&NativeRigidBodyGetUseGravity);
    bind.SetUseGravity = reinterpret_cast<void*>(&NativeRigidBodySetUseGravity);
    bind.GetLinearDamping = reinterpret_cast<void*>(&NativeRigidBodyGetLinearDamping);
    bind.SetLinearDamping = reinterpret_cast<void*>(&NativeRigidBodySetLinearDamping);
    bind.GetAngularDamping = reinterpret_cast<void*>(&NativeRigidBodyGetAngularDamping);
    bind.SetAngularDamping = reinterpret_cast<void*>(&NativeRigidBodySetAngularDamping);
    bind.GetLinearVelocity = reinterpret_cast<void*>(&NativeRigidBodyGetLinearVelocity);
    bind.SetLinearVelocity = reinterpret_cast<void*>(&NativeRigidBodySetLinearVelocity);
    bind.GetAngularVelocity = reinterpret_cast<void*>(&NativeRigidBodyGetAngularVelocity);
    bind.SetAngularVelocity = reinterpret_cast<void*>(&NativeRigidBodySetAngularVelocity);
    bind.GetContinuousCollisionDetection = reinterpret_cast<void*>(&NativeRigidBodyGetContinuousCollisionDetection);
    bind.SetContinuousCollisionDetection = reinterpret_cast<void*>(&NativeRigidBodySetContinuousCollisionDetection);
    bind.GetLockFlags = reinterpret_cast<void*>(&NativeRigidBodyGetLockFlags);
    bind.SetLockFlags = reinterpret_cast<void*>(&NativeRigidBodySetLockFlags);
    return bind;
}

ColliderBind ColliderBind::Create()
{
    ColliderBind bind;
    bind.AddBoxCollider = reinterpret_cast<void*>(&NativeEnsAddBoxCollider);
    bind.AddSphereCollider = reinterpret_cast<void*>(&NativeEnsAddSphereCollider);
    bind.AddCapsuleCollider = reinterpret_cast<void*>(&NativeEnsAddCapsuleCollider);
    bind.AddConvexMeshCollider = reinterpret_cast<void*>(&NativeEnsAddConvexMeshCollider);
    bind.AddTriangleMeshCollider = reinterpret_cast<void*>(&NativeEnsAddTriangleMeshCollider);
    bind.GetColliderCount = reinterpret_cast<void*>(&NativeEnsGetColliderCount);
    bind.GetColliderAt = reinterpret_cast<void*>(&NativeEnsGetColliderAt);
    bind.GetGeometryType = reinterpret_cast<void*>(&NativeColliderGetGeometryType);
    bind.GetEnabled = reinterpret_cast<void*>(&NativeColliderGetEnabled);
    bind.SetEnabled = reinterpret_cast<void*>(&NativeColliderSetEnabled);
    bind.GetIsTrigger = reinterpret_cast<void*>(&NativeColliderGetIsTrigger);
    bind.SetIsTrigger = reinterpret_cast<void*>(&NativeColliderSetIsTrigger);
    bind.GetCenter = reinterpret_cast<void*>(&NativeColliderGetCenter);
    bind.SetCenter = reinterpret_cast<void*>(&NativeColliderSetCenter);
    bind.GetHalfExtents = reinterpret_cast<void*>(&NativeColliderGetHalfExtents);
    bind.SetHalfExtents = reinterpret_cast<void*>(&NativeColliderSetHalfExtents);
    bind.GetRadius = reinterpret_cast<void*>(&NativeColliderGetRadius);
    bind.SetRadius = reinterpret_cast<void*>(&NativeColliderSetRadius);
    bind.GetHalfHeight = reinterpret_cast<void*>(&NativeColliderGetHalfHeight);
    bind.SetHalfHeight = reinterpret_cast<void*>(&NativeColliderSetHalfHeight);
    bind.GetMesh = reinterpret_cast<void*>(&NativeColliderGetMesh);
    bind.SetMesh = reinterpret_cast<void*>(&NativeColliderSetMesh);
    bind.GetStaticFriction = reinterpret_cast<void*>(&NativeColliderGetStaticFriction);
    bind.SetStaticFriction = reinterpret_cast<void*>(&NativeColliderSetStaticFriction);
    bind.GetDynamicFriction = reinterpret_cast<void*>(&NativeColliderGetDynamicFriction);
    bind.SetDynamicFriction = reinterpret_cast<void*>(&NativeColliderSetDynamicFriction);
    bind.GetRestitution = reinterpret_cast<void*>(&NativeColliderGetRestitution);
    bind.SetRestitution = reinterpret_cast<void*>(&NativeColliderSetRestitution);
    bind.GetCollisionLayer = reinterpret_cast<void*>(&NativeColliderGetCollisionLayer);
    bind.SetCollisionLayer = reinterpret_cast<void*>(&NativeColliderSetCollisionLayer);
    bind.GetCollisionMask = reinterpret_cast<void*>(&NativeColliderGetCollisionMask);
    bind.SetCollisionMask = reinterpret_cast<void*>(&NativeColliderSetCollisionMask);
    return bind;
}

CharacterControllerBind CharacterControllerBind::Create()
{
    CharacterControllerBind bind;
    bind.HasComponent = reinterpret_cast<void*>(&NativeEnsHasCharacterController);
    bind.AddComponent = reinterpret_cast<void*>(&NativeEnsAddCharacterController);
    bind.GetComponent = reinterpret_cast<void*>(&NativeEnsGetCharacterController);
    bind.GetEnabled = reinterpret_cast<void*>(&NativeCharacterControllerGetEnabled);
    bind.SetEnabled = reinterpret_cast<void*>(&NativeCharacterControllerSetEnabled);
    bind.GetShape = reinterpret_cast<void*>(&NativeCharacterControllerGetShape);
    bind.SetShape = reinterpret_cast<void*>(&NativeCharacterControllerSetShape);
    bind.GetRadius = reinterpret_cast<void*>(&NativeCharacterControllerGetRadius);
    bind.SetRadius = reinterpret_cast<void*>(&NativeCharacterControllerSetRadius);
    bind.GetHeight = reinterpret_cast<void*>(&NativeCharacterControllerGetHeight);
    bind.SetHeight = reinterpret_cast<void*>(&NativeCharacterControllerSetHeight);
    bind.GetHalfExtents = reinterpret_cast<void*>(&NativeCharacterControllerGetHalfExtents);
    bind.SetHalfExtents = reinterpret_cast<void*>(&NativeCharacterControllerSetHalfExtents);
    bind.GetStepOffset = reinterpret_cast<void*>(&NativeCharacterControllerGetStepOffset);
    bind.SetStepOffset = reinterpret_cast<void*>(&NativeCharacterControllerSetStepOffset);
    bind.GetContactOffset = reinterpret_cast<void*>(&NativeCharacterControllerGetContactOffset);
    bind.SetContactOffset = reinterpret_cast<void*>(&NativeCharacterControllerSetContactOffset);
    bind.GetSlopeLimit = reinterpret_cast<void*>(&NativeCharacterControllerGetSlopeLimit);
    bind.SetSlopeLimit = reinterpret_cast<void*>(&NativeCharacterControllerSetSlopeLimit);
    bind.GetMinMoveDistance = reinterpret_cast<void*>(&NativeCharacterControllerGetMinMoveDistance);
    bind.SetMinMoveDistance = reinterpret_cast<void*>(&NativeCharacterControllerSetMinMoveDistance);
    bind.GetCollisionLayer = reinterpret_cast<void*>(&NativeCharacterControllerGetCollisionLayer);
    bind.SetCollisionLayer = reinterpret_cast<void*>(&NativeCharacterControllerSetCollisionLayer);
    bind.GetCollisionMask = reinterpret_cast<void*>(&NativeCharacterControllerGetCollisionMask);
    bind.SetCollisionMask = reinterpret_cast<void*>(&NativeCharacterControllerSetCollisionMask);
    return bind;
}
