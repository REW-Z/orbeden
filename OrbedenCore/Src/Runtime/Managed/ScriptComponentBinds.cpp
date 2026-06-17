#include "Runtime/Managed/ScriptComponentBinds.h"

#include "Runtime/Ens.h"
#include "Runtime/Object/SpaceComponent.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/World.h"

#include <coreclr_delegates.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
    // 获取当前 World 中的 Ens 封装。
    Ens GetScriptEns(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world ? Ens::FromEns(world, ens) : Ens();
    }

    // 获取当前 World 中的 SpaceComponent。
    SpaceComponent* GetScriptSpace(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world ? world->GetSpaceComponent(ens) : nullptr;
    }

    // 获取当前 World 中的 StaticMeshRenderer。
    StaticMeshRenderer* GetScriptStaticMeshRenderer(EnsId ens)
    {
        World* world = World::CurrentWorld();
        Component* component = world ? world->GetComponent(ens, StaticMeshRenderer::StaticType()) : nullptr;
        return component ? component->Cast<StaticMeshRenderer>() : nullptr;
    }

    // 判断 Ens 是否有效。
    uint8 CORECLR_DELEGATE_CALLTYPE ScriptEnsIsAlive(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world && world->IsAlive(ens) ? 1 : 0;
    }

    // 读取 Ens 名称到 UTF-8 缓冲区。
    int32 CORECLR_DELEGATE_CALLTYPE ScriptEnsGetName(EnsId ens, uint8* buffer, int32 bufferSize)
    {
        Ens value = GetScriptEns(ens);
        const std::string& name = value.GetName();
        int32 byteCount = static_cast<int32>(name.size());
        if (buffer && bufferSize > 0 && byteCount > 0)
        {
            int32 copyCount = std::min(byteCount, bufferSize);
            std::memcpy(buffer, name.data(), static_cast<size_t>(copyCount));
        }

        return byteCount;
    }

    // 写入 Ens 名称。
    void CORECLR_DELEGATE_CALLTYPE ScriptEnsSetName(EnsId ens, const uint8* text, int32 length)
    {
        Ens value = GetScriptEns(ens);
        if (!value.IsValid()) return;

        std::string name;
        if (text && length > 0)
        {
            name.assign(reinterpret_cast<const char*>(text), static_cast<size_t>(length));
        }

        value.SetName(name);
    }

    // 判断 Ens 是否拥有 SpaceComponent。
    uint8 CORECLR_DELEGATE_CALLTYPE ScriptEnsHasSpaceComponent(EnsId ens)
    {
        return GetScriptSpace(ens) ? 1 : 0;
    }

    // 判断 Ens 是否拥有 StaticMeshRenderer。
    uint8 CORECLR_DELEGATE_CALLTYPE ScriptEnsHasStaticMeshRenderer(EnsId ens)
    {
        return GetScriptStaticMeshRenderer(ens) ? 1 : 0;
    }

    // 读取父级 Ens。
    EnsId CORECLR_DELEGATE_CALLTYPE ScriptSpaceGetParent(EnsId ens)
    {
        SpaceComponent* space = GetScriptSpace(ens);
        return space ? space->parent : EnsId();
    }

    // 设置父级 Ens。
    void CORECLR_DELEGATE_CALLTYPE ScriptSpaceSetParent(EnsId ens, EnsId parent)
    {
        World* world = World::CurrentWorld();
        if (world) world->SetParent(ens, parent);
    }

    // 读取本地位置。
    vector3 CORECLR_DELEGATE_CALLTYPE ScriptSpaceGetLocalPosition(EnsId ens)
    {
        SpaceComponent* space = GetScriptSpace(ens);
        return space ? space->localPosition : vector3();
    }

    // 写入本地位置。
    void CORECLR_DELEGATE_CALLTYPE ScriptSpaceSetLocalPosition(EnsId ens, vector3 value)
    {
        SpaceComponent* space = GetScriptSpace(ens);
        if (space) space->localPosition = value;
    }

    // 读取本地旋转。
    quaternion CORECLR_DELEGATE_CALLTYPE ScriptSpaceGetLocalRotation(EnsId ens)
    {
        SpaceComponent* space = GetScriptSpace(ens);
        return space ? space->localRotation : quaternion();
    }

    // 写入本地旋转。
    void CORECLR_DELEGATE_CALLTYPE ScriptSpaceSetLocalRotation(EnsId ens, quaternion value)
    {
        SpaceComponent* space = GetScriptSpace(ens);
        if (space) space->localRotation = value;
    }

    // 读取本地缩放。
    vector3 CORECLR_DELEGATE_CALLTYPE ScriptSpaceGetLocalScale(EnsId ens)
    {
        SpaceComponent* space = GetScriptSpace(ens);
        return space ? space->localScale : vector3{ 1.0f, 1.0f, 1.0f };
    }

    // 写入本地缩放。
    void CORECLR_DELEGATE_CALLTYPE ScriptSpaceSetLocalScale(EnsId ens, vector3 value)
    {
        SpaceComponent* space = GetScriptSpace(ens);
        if (space) space->localScale = value;
    }

    // 读取世界位置。
    vector3 CORECLR_DELEGATE_CALLTYPE ScriptSpaceGetWorldPosition(EnsId ens)
    {
        SpaceComponent* space = GetScriptSpace(ens);
        return space ? space->worldPosition : vector3();
    }

    // 读取世界旋转。
    quaternion CORECLR_DELEGATE_CALLTYPE ScriptSpaceGetWorldRotation(EnsId ens)
    {
        SpaceComponent* space = GetScriptSpace(ens);
        return space ? space->worldRotation : quaternion();
    }

    // 读取 StaticMeshRenderer.enabled。
    uint8 CORECLR_DELEGATE_CALLTYPE ScriptStaticMeshRendererGetEnabled(EnsId ens)
    {
        StaticMeshRenderer* renderer = GetScriptStaticMeshRenderer(ens);
        return renderer && renderer->enabled ? 1 : 0;
    }

    // 写入 StaticMeshRenderer.enabled。
    void CORECLR_DELEGATE_CALLTYPE ScriptStaticMeshRendererSetEnabled(EnsId ens, uint8 value)
    {
        StaticMeshRenderer* renderer = GetScriptStaticMeshRenderer(ens);
        if (renderer) renderer->enabled = value != 0;
    }

    // 读取 StaticMeshRenderer.castShadows。
    uint8 CORECLR_DELEGATE_CALLTYPE ScriptStaticMeshRendererGetCastShadows(EnsId ens)
    {
        StaticMeshRenderer* renderer = GetScriptStaticMeshRenderer(ens);
        return renderer && renderer->castShadows ? 1 : 0;
    }

    // 写入 StaticMeshRenderer.castShadows。
    void CORECLR_DELEGATE_CALLTYPE ScriptStaticMeshRendererSetCastShadows(EnsId ens, uint8 value)
    {
        StaticMeshRenderer* renderer = GetScriptStaticMeshRenderer(ens);
        if (renderer) renderer->castShadows = value != 0;
    }

    // 读取 StaticMeshRenderer.receiveShadows。
    uint8 CORECLR_DELEGATE_CALLTYPE ScriptStaticMeshRendererGetReceiveShadows(EnsId ens)
    {
        StaticMeshRenderer* renderer = GetScriptStaticMeshRenderer(ens);
        return renderer && renderer->receiveShadows ? 1 : 0;
    }

    // 写入 StaticMeshRenderer.receiveShadows。
    void CORECLR_DELEGATE_CALLTYPE ScriptStaticMeshRendererSetReceiveShadows(EnsId ens, uint8 value)
    {
        StaticMeshRenderer* renderer = GetScriptStaticMeshRenderer(ens);
        if (renderer) renderer->receiveShadows = value != 0;
    }
}

EnsBind EnsBind::Create()
{
    EnsBind bind;
    bind.IsAlive = reinterpret_cast<void*>(&ScriptEnsIsAlive);
    bind.GetName = reinterpret_cast<void*>(&ScriptEnsGetName);
    bind.SetName = reinterpret_cast<void*>(&ScriptEnsSetName);
    bind.HasSpaceComponent = reinterpret_cast<void*>(&ScriptEnsHasSpaceComponent);
    bind.HasStaticMeshRenderer = reinterpret_cast<void*>(&ScriptEnsHasStaticMeshRenderer);
    return bind;
}

SpaceComponentBind SpaceComponentBind::Create()
{
    SpaceComponentBind bind;
    bind.GetParent = reinterpret_cast<void*>(&ScriptSpaceGetParent);
    bind.SetParent = reinterpret_cast<void*>(&ScriptSpaceSetParent);
    bind.GetLocalPosition = reinterpret_cast<void*>(&ScriptSpaceGetLocalPosition);
    bind.SetLocalPosition = reinterpret_cast<void*>(&ScriptSpaceSetLocalPosition);
    bind.GetLocalRotation = reinterpret_cast<void*>(&ScriptSpaceGetLocalRotation);
    bind.SetLocalRotation = reinterpret_cast<void*>(&ScriptSpaceSetLocalRotation);
    bind.GetLocalScale = reinterpret_cast<void*>(&ScriptSpaceGetLocalScale);
    bind.SetLocalScale = reinterpret_cast<void*>(&ScriptSpaceSetLocalScale);
    bind.GetWorldPosition = reinterpret_cast<void*>(&ScriptSpaceGetWorldPosition);
    bind.GetWorldRotation = reinterpret_cast<void*>(&ScriptSpaceGetWorldRotation);
    return bind;
}

StaticMeshRendererBind StaticMeshRendererBind::Create()
{
    StaticMeshRendererBind bind;
    bind.GetEnabled = reinterpret_cast<void*>(&ScriptStaticMeshRendererGetEnabled);
    bind.SetEnabled = reinterpret_cast<void*>(&ScriptStaticMeshRendererSetEnabled);
    bind.GetCastShadows = reinterpret_cast<void*>(&ScriptStaticMeshRendererGetCastShadows);
    bind.SetCastShadows = reinterpret_cast<void*>(&ScriptStaticMeshRendererSetCastShadows);
    bind.GetReceiveShadows = reinterpret_cast<void*>(&ScriptStaticMeshRendererGetReceiveShadows);
    bind.SetReceiveShadows = reinterpret_cast<void*>(&ScriptStaticMeshRendererSetReceiveShadows);
    return bind;
}
