#include "Scripting/Script.h"

#include "Runtime/Reflection.h"
#include "Scripting/ScriptSystem.h"
#include "Scripting/ScriptInterop.h"

#include <unordered_map>

OBJECT_TYPE_IMPLEMENT(Script, Component)

namespace
{
    std::unordered_map<TypeRuntimeId, ScriptCallbackTable>& GetScriptCallbackRegistry()
    {
        static std::unordered_map<TypeRuntimeId, ScriptCallbackTable> registry;
        return registry;
    }

    template<typename TCallback>
    TCallback ResolveInheritedCallback(Type* type, TCallback ScriptCallbackTable::* member)
    {
        auto& registry = GetScriptCallbackRegistry();
        for (Type* current = type; current && current->Is(Script::StaticType()); current = current->GetBaseType())
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
    if (!type || !type->Is(Script::StaticType())) return;
    GetScriptCallbackRegistry()[type->GetId()] = callbacks;
}

void UnregisterScriptCallbacks(Type* type)
{
    if (type) GetScriptCallbackRegistry().erase(type->GetId());
}

ScriptCallbackTable ResolveScriptCallbacks(Type* type)
{
    ScriptCallbackTable callbacks;
    if (!type || !type->Is(Script::StaticType())) return callbacks;

    callbacks.start = ResolveInheritedCallback(type, &ScriptCallbackTable::start);
    callbacks.update = ResolveInheritedCallback(type, &ScriptCallbackTable::update);
    callbacks.fixedUpdate = ResolveInheritedCallback(type, &ScriptCallbackTable::fixedUpdate);
    callbacks.lateUpdate = ResolveInheritedCallback(type, &ScriptCallbackTable::lateUpdate);
    callbacks.drawGUI = ResolveInheritedCallback(type, &ScriptCallbackTable::drawGUI);
    callbacks.end = ResolveInheritedCallback(type, &ScriptCallbackTable::end);
    return callbacks;
}

void Script::OnAttach()
{
    domain = GetType() == StaticType() ? ScriptDomain::Managed : ScriptDomain::Native;
    if (domain == ScriptDomain::Managed)
    {
        ScriptInterop::NotifyManagedHostAttached(this);
        return;
    }
    if (ScriptSystem* system = ScriptSystem::Current()) system->AttachNativeScript(this);
}

void Script::OnDetach()
{
    if (domain == ScriptDomain::Managed)
    {
        ScriptInterop::NotifyManagedHostDetached(this);
        return;
    }
    if (ScriptSystem* system = ScriptSystem::Current()) system->DetachNativeScript(this);
}

void Script::OnWorldActiveChanged(bool worldActive)
{
    (void)worldActive;
    if (domain == ScriptDomain::Managed) return;
    if (ScriptSystem* system = ScriptSystem::Current()) system->RefreshNativeScript(this);
}

void Script::RegisterReflection()
{
    static bool registered = false;
    if (registered) return;
    registered = true;

    Reflection::RegisterTypeFields(StaticType(),
        {
            Reflection::FieldInfo("enabled", "bool", Reflection::FieldKind::Bool, true,
                &Script::GetEnabledField, &Script::SetEnabledField, nullptr,
                &Script::GetEnabledValue, &Script::SetEnabledValue),
        });
}

std::string Script::GetEnabledField(Object* object)
{
    Script* script = object ? object->Cast<Script>() : nullptr;
    return Reflection::ToXmlValue(script && script->GetEnabled());
}

bool Script::SetEnabledField(Object* object, const std::string& value)
{
    Script* script = object ? object->Cast<Script>() : nullptr;
    if (!script) return false;

    bool parsed = false;
    if (!Reflection::SetFromXmlValue(parsed, value)) return false;
    script->SetEnabled(parsed);
    return true;
}

//读取 enabled 类型化字段
Reflection::Value Script::GetEnabledValue(Object* object)
{
    Script* script = object ? object->Cast<Script>() : nullptr;
    return script ? Reflection::Value(script->GetEnabled()) : Reflection::Value();
}

//通过业务 setter 写入 enabled 类型化字段
bool Script::SetEnabledValue(Object* object, const Reflection::Value& value)
{
    Script* script = object ? object->Cast<Script>() : nullptr;
    if (!script) return false;

    bool enabledValue = false;
    if (!value.TryGet(enabledValue)) return false;
    script->SetEnabled(enabledValue);
    return true;
}

bool Script::GetEnabled() const
{
    return enabled;
}

void Script::SetEnabled(bool value)
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

ScriptDomain Script::GetDomain() const
{
    return GetType() == StaticType() ? ScriptDomain::Managed : ScriptDomain::Native;
}

bool Script::IsManagedHost() const
{
    return GetType() == StaticType();
}

const std::string& Script::GetManagedTypeName() const
{
    return managedTypeName;
}

bool Script::SetManagedTypeName(const std::string& value)
{
    if (!IsManagedHost()) return false;
    if (managedTypeName == value) return true;

    ScriptInterop::NotifyManagedHostDetached(this);
    managedTypeName = value;
    ScriptInterop::NotifyManagedHostAttached(this);
    return true;
}

const List<ManagedScriptField>& Script::GetManagedFields() const
{
    return managedFields;
}

const ManagedScriptField* Script::FindManagedField(const std::string& name) const
{
    for (const ManagedScriptField& field : managedFields)
    {
        if (field.name == name) return &field;
    }
    return nullptr;
}

bool Script::SetManagedField(const std::string& name,
    const std::string& typeName,
    Reflection::FieldKind kind,
    const std::string& value,
    bool inspectorVisible)
{
    if (!IsManagedHost() || name.empty() || typeName.empty() || kind == Reflection::FieldKind::Unsupported) return false;
    if (name == "domain" || name == "managedTypeName" || name == "enabled") return false;
    Reflection::Value parsed;
    if (kind != Reflection::FieldKind::EnsId && !Reflection::Value::FromString(kind, value, parsed)) return false;

    for (ManagedScriptField& field : managedFields)
    {
        if (field.name != name) continue;
        if (field.typeName == typeName && field.kind == kind && field.value == value
            && field.inspectorVisible == inspectorVisible) return true;
        ManagedScriptField previous = field;
        field.typeName = typeName;
        field.kind = kind;
        field.value = value;
        field.inspectorVisible = inspectorVisible;
        if (ScriptInterop::NotifyManagedHostFieldChanged(this, name)) return true;
        field = std::move(previous);
        return false;
    }

    managedFields.push_back({ name, typeName, kind, value, inspectorVisible });
    if (ScriptInterop::NotifyManagedHostFieldChanged(this, name)) return true;
    managedFields.pop_back();
    return false;
}

bool Script::SetManagedFieldValue(const std::string& name, const std::string& value)
{
    for (ManagedScriptField& field : managedFields)
    {
        if (field.name != name) continue;
        Reflection::Value parsed;
        if (field.kind != Reflection::FieldKind::EnsId && !Reflection::Value::FromString(field.kind, value, parsed)) return false;
        std::string previous = field.value;
        field.value = value;
        if (ScriptInterop::NotifyManagedHostFieldChanged(this, name)) return true;
        field.value = std::move(previous);
        return false;
    }
    return false;
}

Reflection::FieldKind Script::GetManagedFieldKind(const std::string& typeName)
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
