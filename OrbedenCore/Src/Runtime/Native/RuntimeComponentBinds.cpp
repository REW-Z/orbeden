#include "Runtime/Native/RuntimeComponentBinds.h"

#include "Runtime/Ens.h"
#include "Runtime/Native/NativeCall.h"
#include "Runtime/Object/SpaceComponent.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/World.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
    //从 UTF-8 字节创建字符串。
    std::string MakeText(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return std::string();
        return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(length));
    }

    //获取当前 World 中的唯一 Ens 实例。
    Ens* GetNativeEns(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world ? world->GetEns(ens) : nullptr;
    }

    //获取当前 World 中的 SpaceComponent。
    SpaceComponent* GetNativeSpace(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world ? world->GetSpaceComponent(ens) : nullptr;
    }

    //获取当前 World 中的 StaticMeshRenderer。
    StaticMeshRenderer* GetNativeStaticMeshRenderer(EnsId ens)
    {
        World* world = World::CurrentWorld();
        Component* component = world ? world->GetComponent(ens, StaticMeshRenderer::StaticType()) : nullptr;
        return component ? component->Cast<StaticMeshRenderer>() : nullptr;
    }

    //创建 Ens。
    EnsId ORBEDEN_NATIVE_CALL NativeWorldCreateEns(const uint8* name, int32 length)
    {
        World* world = World::CurrentWorld();
        Ens* ens = world ? world->CreateEns(MakeText(name, length)) : nullptr;
        return ens ? ens->GetId() : EnsId();
    }

    //使用稳定 ID 创建 Ens。
    EnsId ORBEDEN_NATIVE_CALL NativeWorldCreateEnsWithStableId(const uint8* stableId, int32 stableIdLength, const uint8* name, int32 nameLength)
    {
        World* world = World::CurrentWorld();
        Ens* ens = world ? world->CreateEnsWithStableId(MakeText(stableId, stableIdLength), MakeText(name, nameLength)) : nullptr;
        return ens ? ens->GetId() : EnsId();
    }

    //按稳定 ID 查找 Ens。
    EnsId ORBEDEN_NATIVE_CALL NativeWorldFindEns(const uint8* stableId, int32 stableIdLength)
    {
        World* world = World::CurrentWorld();
        Ens* ens = world ? world->FindEns(StringId(MakeText(stableId, stableIdLength))) : nullptr;
        return ens ? ens->GetId() : EnsId();
    }

    //销毁 Ens。
    uint8 ORBEDEN_NATIVE_CALL NativeWorldDestroyEns(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world && world->DestroyEns(ens) ? 1 : 0;
    }

    //判断 Ens 是否有效。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsIsAlive(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world && world->IsAlive(ens) ? 1 : 0;
    }

    //读取 Ens 名称到 UTF-8 缓冲区。
    int32 ORBEDEN_NATIVE_CALL NativeEnsGetName(EnsId ens, uint8* buffer, int32 bufferSize)
    {
        Ens* value = GetNativeEns(ens);
        static const std::string emptyName;
        const std::string& name = value ? value->GetName() : emptyName;
        int32 byteCount = static_cast<int32>(name.size());
        if (buffer && bufferSize > 0 && byteCount > 0)
        {
            int32 copyCount = std::min(byteCount, bufferSize);
            std::memcpy(buffer, name.data(), static_cast<size_t>(copyCount));
        }

        return byteCount;
    }

    //写入 Ens 名称。
    void ORBEDEN_NATIVE_CALL NativeEnsSetName(EnsId ens, const uint8* text, int32 length)
    {
        Ens* value = GetNativeEns(ens);
        if (value) value->SetName(MakeText(text, length));
    }

    //判断 Ens 是否拥有 SpaceComponent。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsHasSpaceComponent(EnsId ens)
    {
        return GetNativeSpace(ens) ? 1 : 0;
    }

    //判断 Ens 是否拥有 StaticMeshRenderer。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsHasStaticMeshRenderer(EnsId ens)
    {
        return GetNativeStaticMeshRenderer(ens) ? 1 : 0;
    }

    //添加 StaticMeshRenderer。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsAddStaticMeshRenderer(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value && value->AddComponent<StaticMeshRenderer>() ? 1 : 0;
    }

    //读取父级 Ens。
    EnsId ORBEDEN_NATIVE_CALL NativeSpaceGetParent(EnsId ens)
    {
        SpaceComponent* space = GetNativeSpace(ens);
        return space ? space->parent : EnsId();
    }

    //设置父级 Ens。
    void ORBEDEN_NATIVE_CALL NativeSpaceSetParent(EnsId ens, EnsId parent)
    {
        World* world = World::CurrentWorld();
        if (world) world->SetParent(ens, parent);
    }

    //读取本地位置。
    vector3 ORBEDEN_NATIVE_CALL NativeSpaceGetLocalPosition(EnsId ens)
    {
        SpaceComponent* space = GetNativeSpace(ens);
        return space ? space->localPosition : vector3();
    }

    //写入本地位置。
    void ORBEDEN_NATIVE_CALL NativeSpaceSetLocalPosition(EnsId ens, vector3 value)
    {
        SpaceComponent* space = GetNativeSpace(ens);
        if (space) space->localPosition = value;
    }

    //读取本地旋转。
    quaternion ORBEDEN_NATIVE_CALL NativeSpaceGetLocalRotation(EnsId ens)
    {
        SpaceComponent* space = GetNativeSpace(ens);
        return space ? space->localRotation : quaternion();
    }

    //写入本地旋转。
    void ORBEDEN_NATIVE_CALL NativeSpaceSetLocalRotation(EnsId ens, quaternion value)
    {
        SpaceComponent* space = GetNativeSpace(ens);
        if (space) space->localRotation = value;
    }

    //读取本地缩放。
    vector3 ORBEDEN_NATIVE_CALL NativeSpaceGetLocalScale(EnsId ens)
    {
        SpaceComponent* space = GetNativeSpace(ens);
        return space ? space->localScale : vector3{ 1.0f, 1.0f, 1.0f };
    }

    //写入本地缩放。
    void ORBEDEN_NATIVE_CALL NativeSpaceSetLocalScale(EnsId ens, vector3 value)
    {
        SpaceComponent* space = GetNativeSpace(ens);
        if (space) space->localScale = value;
    }

    //读取世界位置。
    vector3 ORBEDEN_NATIVE_CALL NativeSpaceGetWorldPosition(EnsId ens)
    {
        SpaceComponent* space = GetNativeSpace(ens);
        return space ? space->worldPosition : vector3();
    }

    //读取世界旋转。
    quaternion ORBEDEN_NATIVE_CALL NativeSpaceGetWorldRotation(EnsId ens)
    {
        SpaceComponent* space = GetNativeSpace(ens);
        return space ? space->worldRotation : quaternion();
    }

    //读取 StaticMeshRenderer.enabled。
    uint8 ORBEDEN_NATIVE_CALL NativeStaticMeshRendererGetEnabled(EnsId ens)
    {
        StaticMeshRenderer* renderer = GetNativeStaticMeshRenderer(ens);
        return renderer && renderer->enabled ? 1 : 0;
    }

    //写入 StaticMeshRenderer.enabled。
    void ORBEDEN_NATIVE_CALL NativeStaticMeshRendererSetEnabled(EnsId ens, uint8 value)
    {
        StaticMeshRenderer* renderer = GetNativeStaticMeshRenderer(ens);
        if (renderer) renderer->enabled = value != 0;
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

EnsBind EnsBind::Create()
{
    EnsBind bind;
    bind.IsAlive = reinterpret_cast<void*>(&NativeEnsIsAlive);
    bind.GetName = reinterpret_cast<void*>(&NativeEnsGetName);
    bind.SetName = reinterpret_cast<void*>(&NativeEnsSetName);
    bind.HasSpaceComponent = reinterpret_cast<void*>(&NativeEnsHasSpaceComponent);
    bind.HasStaticMeshRenderer = reinterpret_cast<void*>(&NativeEnsHasStaticMeshRenderer);
    bind.AddStaticMeshRenderer = reinterpret_cast<void*>(&NativeEnsAddStaticMeshRenderer);
    return bind;
}

SpaceComponentBind SpaceComponentBind::Create()
{
    SpaceComponentBind bind;
    bind.GetParent = reinterpret_cast<void*>(&NativeSpaceGetParent);
    bind.SetParent = reinterpret_cast<void*>(&NativeSpaceSetParent);
    bind.GetLocalPosition = reinterpret_cast<void*>(&NativeSpaceGetLocalPosition);
    bind.SetLocalPosition = reinterpret_cast<void*>(&NativeSpaceSetLocalPosition);
    bind.GetLocalRotation = reinterpret_cast<void*>(&NativeSpaceGetLocalRotation);
    bind.SetLocalRotation = reinterpret_cast<void*>(&NativeSpaceSetLocalRotation);
    bind.GetLocalScale = reinterpret_cast<void*>(&NativeSpaceGetLocalScale);
    bind.SetLocalScale = reinterpret_cast<void*>(&NativeSpaceSetLocalScale);
    bind.GetWorldPosition = reinterpret_cast<void*>(&NativeSpaceGetWorldPosition);
    bind.GetWorldRotation = reinterpret_cast<void*>(&NativeSpaceGetWorldRotation);
    return bind;
}

StaticMeshRendererBind StaticMeshRendererBind::Create()
{
    StaticMeshRendererBind bind;
    bind.GetEnabled = reinterpret_cast<void*>(&NativeStaticMeshRendererGetEnabled);
    bind.SetEnabled = reinterpret_cast<void*>(&NativeStaticMeshRendererSetEnabled);
    bind.GetCastShadows = reinterpret_cast<void*>(&NativeStaticMeshRendererGetCastShadows);
    bind.SetCastShadows = reinterpret_cast<void*>(&NativeStaticMeshRendererSetCastShadows);
    bind.GetReceiveShadows = reinterpret_cast<void*>(&NativeStaticMeshRendererGetReceiveShadows);
    bind.SetReceiveShadows = reinterpret_cast<void*>(&NativeStaticMeshRendererSetReceiveShadows);
    return bind;
}
