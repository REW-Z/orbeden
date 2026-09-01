#include "Scripting/ScriptBehaviour.h"

#include "Runtime/Reflection.h"
#include "Scripting/ScriptSystem.h"

#include <unordered_map>

OBJECT_TYPE_IMPLEMENT_ABSTRACT(ScriptBehaviour, Component)

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
    if (ScriptSystem* system = ScriptSystem::Current()) system->AttachNativeScript(this);
}

void ScriptBehaviour::OnDetach()
{
    if (ScriptSystem* system = ScriptSystem::Current()) system->DetachNativeScript(this);
}

void ScriptBehaviour::OnWorldActiveChanged(bool worldActive)
{
    (void)worldActive;
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
                &ScriptBehaviour::GetEnabledField, &ScriptBehaviour::SetEnabledField),
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

bool ScriptBehaviour::GetEnabled() const
{
    return enabled;
}

void ScriptBehaviour::SetEnabled(bool value)
{
    if (enabled == value) return;

    enabled = value;
    if (ScriptSystem* system = ScriptSystem::Current()) system->RefreshNativeScript(this);
}
