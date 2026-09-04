#include "Runtime/Native/OrbedenNativeApi.h"

#include "Runtime/Ens.h"
#include "Runtime/World.h"
#include "Scripting/ScriptBehaviour.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace
{
    std::string ReadUtf8(const uint8* text, int32 length)
    {
        return text && length > 0
            ? std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(length))
            : std::string();
    }

    int32 CopyUtf8(std::string_view text, uint8* buffer, int32 bufferSize)
    {
        int32 required = static_cast<int32>(text.size());
        if (!buffer || bufferSize <= 0) return required;
        int32 count = std::min(required, bufferSize);
        if (count > 0) std::memcpy(buffer, text.data(), static_cast<std::size_t>(count));
        return count;
    }

    ScriptBehaviour* ResolveManagedHost(World* world, void* pointer)
    {
        ScriptBehaviour* host = static_cast<ScriptBehaviour*>(pointer);
        return world && host && host->GetWorld() == world && host->IsManagedHost() ? host : nullptr;
    }

    List<ScriptBehaviour*> CollectManagedHosts(World* world)
    {
        List<ScriptBehaviour*> hosts;
        if (!world) return hosts;
        world->ForEachEns([&hosts](Ens& ens)
            {
                for (Component* component : ens.GetComponents())
                {
                    ScriptBehaviour* host = component ? component->Cast<ScriptBehaviour>() : nullptr;
                    if (host && host->IsManagedHost()) hosts.push_back(host);
                }
            });
        return hosts;
    }

    int32 ORBEDEN_NATIVE_CALL GetScriptHostCount(void* context)
    {
        return static_cast<int32>(CollectManagedHosts(static_cast<World*>(context)).size());
    }

    void* ORBEDEN_NATIVE_CALL GetScriptHostAt(void* context, int32 index)
    {
        List<ScriptBehaviour*> hosts = CollectManagedHosts(static_cast<World*>(context));
        return index >= 0 && index < static_cast<int32>(hosts.size()) ? hosts[index] : nullptr;
    }

    void* ORBEDEN_NATIVE_CALL CreateScriptHost(void* context, EnsId ensId, const uint8* typeName, int32 typeNameLength)
    {
        World* world = static_cast<World*>(context);
        Ens* ens = world ? world->GetEns(ensId) : nullptr;
        std::string managedTypeName = ReadUtf8(typeName, typeNameLength);
        if (!ens || managedTypeName.empty()) return nullptr;

        ScriptBehaviour* host = ens->AddComponentInstance<ScriptBehaviour>();
        if (!host || !host->SetManagedTypeName(managedTypeName))
        {
            if (host) ens->RemoveComponent(host);
            return nullptr;
        }
        return host;
    }

    uint8 ORBEDEN_NATIVE_CALL RemoveScriptHost(void* context, void* pointer)
    {
        World* world = static_cast<World*>(context);
        ScriptBehaviour* host = ResolveManagedHost(world, pointer);
        return host && world->RemoveComponent(host) ? 1 : 0;
    }

    EnsId ORBEDEN_NATIVE_CALL GetScriptHostEns(void* context, void* pointer)
    {
        ScriptBehaviour* host = ResolveManagedHost(static_cast<World*>(context), pointer);
        return host ? host->GetEnsId() : EnsId();
    }

    int32 ORBEDEN_NATIVE_CALL GetScriptHostTypeName(void* context, void* pointer, uint8* buffer, int32 bufferSize)
    {
        ScriptBehaviour* host = ResolveManagedHost(static_cast<World*>(context), pointer);
        return host ? CopyUtf8(host->GetManagedTypeName(), buffer, bufferSize) : 0;
    }

    uint8 ORBEDEN_NATIVE_CALL GetScriptHostEnabled(void* context, void* pointer)
    {
        ScriptBehaviour* host = ResolveManagedHost(static_cast<World*>(context), pointer);
        return host && host->GetEnabled() ? 1 : 0;
    }

    void ORBEDEN_NATIVE_CALL SetScriptHostEnabled(void* context, void* pointer, uint8 value)
    {
        ScriptBehaviour* host = ResolveManagedHost(static_cast<World*>(context), pointer);
        if (host) host->SetEnabled(value != 0);
    }

    int32 ORBEDEN_NATIVE_CALL GetScriptHostFieldCount(void* context, void* pointer)
    {
        ScriptBehaviour* host = ResolveManagedHost(static_cast<World*>(context), pointer);
        return host ? static_cast<int32>(host->GetManagedFields().size()) : 0;
    }

    const ManagedScriptField* GetScriptHostField(void* context, void* pointer, int32 index)
    {
        ScriptBehaviour* host = ResolveManagedHost(static_cast<World*>(context), pointer);
        if (!host || index < 0 || index >= static_cast<int32>(host->GetManagedFields().size())) return nullptr;
        return &host->GetManagedFields()[index];
    }

    int32 ORBEDEN_NATIVE_CALL GetScriptHostFieldName(void* context, void* pointer, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ManagedScriptField* field = GetScriptHostField(context, pointer, index);
        return field ? CopyUtf8(field->name, buffer, bufferSize) : 0;
    }

    int32 ORBEDEN_NATIVE_CALL GetScriptHostFieldTypeName(void* context, void* pointer, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ManagedScriptField* field = GetScriptHostField(context, pointer, index);
        return field ? CopyUtf8(field->typeName, buffer, bufferSize) : 0;
    }

    int32 ORBEDEN_NATIVE_CALL GetScriptHostFieldKind(void* context, void* pointer, int32 index)
    {
        const ManagedScriptField* field = GetScriptHostField(context, pointer, index);
        return field ? static_cast<int32>(field->kind) : 0;
    }

    int32 ORBEDEN_NATIVE_CALL GetScriptHostFieldValue(void* context, void* pointer, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ManagedScriptField* field = GetScriptHostField(context, pointer, index);
        return field ? CopyUtf8(field->value, buffer, bufferSize) : 0;
    }

    uint8 ORBEDEN_NATIVE_CALL SetScriptHostField(void* context,
        void* pointer,
        const uint8* fieldName,
        int32 fieldNameLength,
        const uint8* typeName,
        int32 typeNameLength,
        const uint8* value,
        int32 valueLength,
        uint8 inspectorVisible)
    {
        ScriptBehaviour* host = ResolveManagedHost(static_cast<World*>(context), pointer);
        std::string field = ReadUtf8(fieldName, fieldNameLength);
        std::string fieldType = ReadUtf8(typeName, typeNameLength);
        Reflection::FieldKind kind = ScriptBehaviour::GetManagedFieldKind(fieldType);
        return host && host->SetManagedField(field, fieldType, kind, ReadUtf8(value, valueLength), inspectorVisible != 0) ? 1 : 0;
    }
}

OrbedenEngineNativeApi OrbedenEngineNativeApi::Create()
{
    OrbedenEngineNativeApi api;
    api.World = WorldBind::Create();
    api.PathDefines = PathDefinesBind::Create();
    api.Ens = EnsBind::Create();
    api.TransformComponent = TransformComponentBind::Create();
    api.StaticMeshRenderer = StaticMeshRendererBind::Create();
    api.Object = ObjectBind::Create();
    api.Mesh = MeshBind::Create();
    api.Material = MaterialBind::Create();
    api.Shader = ShaderBind::Create();
    api.RigidBody = RigidBodyBind::Create();
    api.Collider = ColliderBind::Create();
    api.CharacterController = CharacterControllerBind::Create();
    api.ObjectExtension = ObjectExtensionBind::Create();
    return api;
}

ScriptBehaviourBindApi ScriptBehaviourBindApi::Create(World* world)
{
    ScriptBehaviourBindApi api;
    api.Context = world;
    api.GetHostCount = reinterpret_cast<void*>(&GetScriptHostCount);
    api.GetHostAt = reinterpret_cast<void*>(&GetScriptHostAt);
    api.CreateHost = reinterpret_cast<void*>(&CreateScriptHost);
    api.RemoveHost = reinterpret_cast<void*>(&RemoveScriptHost);
    api.GetEns = reinterpret_cast<void*>(&GetScriptHostEns);
    api.GetTypeName = reinterpret_cast<void*>(&GetScriptHostTypeName);
    api.GetEnabled = reinterpret_cast<void*>(&GetScriptHostEnabled);
    api.SetEnabled = reinterpret_cast<void*>(&SetScriptHostEnabled);
    api.GetFieldCount = reinterpret_cast<void*>(&GetScriptHostFieldCount);
    api.GetFieldName = reinterpret_cast<void*>(&GetScriptHostFieldName);
    api.GetFieldTypeName = reinterpret_cast<void*>(&GetScriptHostFieldTypeName);
    api.GetFieldKind = reinterpret_cast<void*>(&GetScriptHostFieldKind);
    api.GetFieldValue = reinterpret_cast<void*>(&GetScriptHostFieldValue);
    api.SetField = reinterpret_cast<void*>(&SetScriptHostField);
    return api;
}


OrbedenNativeApi OrbedenNativeApi::Create(World* world)
{
    OrbedenNativeApi api;
    api.Gui = RuntimeGuiBridge::GetApi();
    api.World = WorldBind::Create();
    api.PathDefines = PathDefinesBind::Create();
    api.Ens = EnsBind::Create();
    api.TransformComponent = TransformComponentBind::Create();
    api.StaticMeshRenderer = StaticMeshRendererBind::Create();
    api.Object = ObjectBind::Create();
    api.Mesh = MeshBind::Create();
    api.Material = MaterialBind::Create();
    api.Shader = ShaderBind::Create();
    api.RigidBody = RigidBodyBind::Create();
    api.Collider = ColliderBind::Create();
    api.CharacterController = CharacterControllerBind::Create();
    api.GuiExtension = RuntimeGuiBridge::GetExtensionApi();
    api.GuiAdvanced = RuntimeGuiBridge::GetAdvancedApi();
    api.ObjectExtension = ObjectExtensionBind::Create();
    api.ScriptInterop = ScriptInterop::ScriptInteropApi::Create();
    api.ScriptBehaviour = ScriptBehaviourBindApi::Create(world);
    return api;
}
