#include "Scripting/ScriptBehaviour.h"

#include "Runtime/Reflection.h"
#include "Scripting/ScriptSystem.h"

#include <unordered_map>

OBJECT_TYPE_IMPLEMENT(ScriptBehaviour, Component)

namespace
{
    std::unordered_map<TypeId, ScriptCallbackTable>& GetScriptCallbackRegistry()
    {
        static std::unordered_map<TypeId, ScriptCallbackTable> registry;
        return registry;
    }

    template<typename TCallback>
    TCallback ResolveInheritedCallback(Type* type, TCallback ScriptCallbackTable::* member)
    {
        auto& registry = GetScriptCallbackRegistry();
        for (Type* current = type; current && current->Is(ScriptBehaviour::StaticType()); current = current->GetBaseType())
        {
            auto found = registry.find(current->GetId());
            if (found != registry.end() && found->second.*member)
            {
                return found->second.*member;
            }
        }

        return nullptr;
    }
}

void RegisterScriptCallbacks(Type* type, const ScriptCallbackTable& callbacks)
{
    if (!type || !type->Is(ScriptBehaviour::StaticType())) return;
    GetScriptCallbackRegistry()[type->GetId()] = callbacks;
}

void UnregisterScriptCallbacks(Type* type)
{
    if (type) GetScriptCallbackRegistry().erase(type->GetId());
}

ScriptCallbackTable ResolveScriptCallbacks(Type* type)
{
    ScriptCallbackTable callbacks;
    if (!type || !type->Is(ScriptBehaviour::StaticType())) return callbacks;

    callbacks.start = ResolveInheritedCallback(type, &ScriptCallbackTable::start);
    callbacks.update = ResolveInheritedCallback(type, &ScriptCallbackTable::update);
    callbacks.fixedUpdate = ResolveInheritedCallback(type, &ScriptCallbackTable::fixedUpdate);
    callbacks.lateUpdate = ResolveInheritedCallback(type, &ScriptCallbackTable::lateUpdate);
    callbacks.drawGUI = ResolveInheritedCallback(type, &ScriptCallbackTable::drawGUI);
    callbacks.end = ResolveInheritedCallback(type, &ScriptCallbackTable::end);
    return callbacks;
}

void ScriptBehaviour::OnAttach()
{
    domain = GetType() == StaticType() ? ScriptDomain::Managed : ScriptDomain::Native;
    if (domain == ScriptDomain::Managed)
    {
        ScriptInterop::NotifyManagedHostAttached(this);
        return;
    }
    if (ScriptSystem* system = ScriptSystem::Current()) system->AttachNativeScript(this);
}

void ScriptBehaviour::OnDetach()
{
    if (domain == ScriptDomain::Managed)
    {
        ScriptInterop::NotifyManagedHostDetached(this);
        return;
    }
    if (ScriptSystem* system = ScriptSystem::Current()) system->DetachNativeScript(this);
}

void ScriptBehaviour::OnWorldActiveChanged(bool worldActive)
{
    (void)worldActive;
    if (domain == ScriptDomain::Managed) return;
    if (ScriptSystem* system = ScriptSystem::Current()) system->RefreshNativeScript(this);
}

void ScriptBehaviour::RegisterReflection()
{
    static bool registered = false;
    if (registered) return;
    registered = true;

    Reflection::RegisterTypeFields(StaticType(),
        {
            Reflection::FieldInfo("enabled", "bool", Reflection::FieldKind::Bool, true,
                &ScriptBehaviour::GetEnabledField, &ScriptBehaviour::SetEnabledField, nullptr,
                &ScriptBehaviour::GetEnabledValue, &ScriptBehaviour::SetEnabledValue),
        });
}

std::string ScriptBehaviour::GetEnabledField(Object* object)
{
    ScriptBehaviour* script = object ? object->Cast<ScriptBehaviour>() : nullptr;
    return Reflection::ToXmlValue(script && script->GetEnabled());
}

bool ScriptBehaviour::SetEnabledField(Object* object, const std::string& value)
{
    ScriptBehaviour* script = object ? object->Cast<ScriptBehaviour>() : nullptr;
    if (!script) return false;

    bool parsed = false;
    if (!Reflection::SetFromXmlValue(parsed, value)) return false;
    script->SetEnabled(parsed);
    return true;
}

//读取 enabled 类型化字段
Reflection::Value ScriptBehaviour::GetEnabledValue(Object* object)
{
    ScriptBehaviour* script = object ? object->Cast<ScriptBehaviour>() : nullptr;
    return script ? Reflection::Value(script->GetEnabled()) : Reflection::Value();
}

//通过业务 setter 写入 enabled 类型化字段
bool ScriptBehaviour::SetEnabledValue(Object* object, const Reflection::Value& value)
{
    ScriptBehaviour* script = object ? object->Cast<ScriptBehaviour>() : nullptr;
    if (!script) return false;

    bool enabledValue = false;
    if (!value.TryGet(enabledValue)) return false;
    script->SetEnabled(enabledValue);
    return true;
}

bool ScriptBehaviour::GetEnabled() const
{
    return enabled;
}

void ScriptBehaviour::SetEnabled(bool value)
{
    if (enabled == value) return;

    enabled = value;
    if (domain == ScriptDomain::Managed)
    {
        ScriptInterop::NotifyManagedHostEnabledChanged(this);
        return;
    }
    if (ScriptSystem* system = ScriptSystem::Current()) system->RefreshNativeScript(this);
}

ScriptDomain ScriptBehaviour::GetDomain() const
{
    return domain;
}

bool ScriptBehaviour::IsManagedHost() const
{
    return domain == ScriptDomain::Managed && GetType() == StaticType();
}

const std::string& ScriptBehaviour::GetManagedTypeName() const
{
    return managedTypeName;
}

bool ScriptBehaviour::SetManagedTypeName(const std::string& value)
{
    if (!IsManagedHost() || value.empty()) return false;
    if (managedTypeName == value) return true;

    managedTypeName = value;
    ScriptInterop::NotifyManagedHostAttached(this);
    return true;
}

const List<ManagedScriptField>& ScriptBehaviour::GetManagedFields() const
{
    return managedFields;
}

const ManagedScriptField* ScriptBehaviour::FindManagedField(const std::string& name) const
{
    for (const ManagedScriptField& field : managedFields)
    {
        if (field.name == name) return &field;
    }
    return nullptr;
}

bool ScriptBehaviour::SetManagedField(const std::string& name,
    const std::string& typeName,
    Reflection::FieldKind kind,
    const std::string& value,
    bool inspectorVisible)
{
    if (!IsManagedHost() || name.empty() || typeName.empty() || kind == Reflection::FieldKind::Unsupported) return false;
    if (name == "domain" || name == "managedTypeName" || name == "enabled") return false;

    for (ManagedScriptField& field : managedFields)
    {
        if (field.name != name) continue;
        field.typeName = typeName;
        field.kind = kind;
        field.value = value;
        field.inspectorVisible = inspectorVisible;
        ScriptInterop::NotifyManagedHostFieldChanged(this, name);
        return true;
    }

    managedFields.push_back({ name, typeName, kind, value, inspectorVisible });
    ScriptInterop::NotifyManagedHostFieldChanged(this, name);
    return true;
}

bool ScriptBehaviour::SetManagedFieldValue(const std::string& name, const std::string& value)
{
    for (ManagedScriptField& field : managedFields)
    {
        if (field.name != name) continue;
        field.value = value;
        ScriptInterop::NotifyManagedHostFieldChanged(this, name);
        return true;
    }
    return false;
}

Reflection::FieldKind ScriptBehaviour::GetManagedFieldKind(const std::string& typeName)
{
    if (typeName == "bool") return Reflection::FieldKind::Bool;
    if (typeName == "int" || typeName == "int32") return Reflection::FieldKind::Int32;
    if (typeName == "uint" || typeName == "uint32") return Reflection::FieldKind::UInt32;
    if (typeName == "ulong" || typeName == "uint64") return Reflection::FieldKind::UInt64;
    if (typeName == "float" || typeName == "float32") return Reflection::FieldKind::Float32;
    if (typeName == "string" || typeName == "System.String") return Reflection::FieldKind::String;
    if (typeName == "StringId") return Reflection::FieldKind::StringId;
    if (typeName == "vector3") return Reflection::FieldKind::Vector3;
    if (typeName == "color" || typeName == "color4") return Reflection::FieldKind::Color;
    if (typeName == "quaternion") return Reflection::FieldKind::Quaternion;
    if (typeName == "EnsId") return Reflection::FieldKind::EnsId;
    if (typeName == "Object" || typeName.starts_with("Ref<")) return Reflection::FieldKind::ObjectRef;
    return Reflection::FieldKind::Unsupported;
}
