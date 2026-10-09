#include "Runtime/OrbEvent.h"
#include "Runtime/Object/Ens.h"
#include "Log/Log.h"
#include <algorithm>
#include <stdexcept>

OrbEvent::OrbEvent(List<Reflection::ValueKind> signature) : parameterKinds(std::move(signature))
{
    for (auto kind : parameterKinds)
        if (kind == Reflection::ValueKind::Empty || kind == Reflection::ValueKind::Array
            || kind > Reflection::ValueKind::Vector2) throw std::invalid_argument("Unsupported OrbEvent parameter");
}

OrbEvent::OrbEvent(const OrbEvent& value)
    : parameterKinds(value.parameterKinds), persistentCalls(value.persistentCalls) {}

OrbEvent& OrbEvent::operator=(const OrbEvent& value)
{
    if (parameterKinds != value.parameterKinds) throw std::invalid_argument("OrbEvent signature mismatch");
    persistentCalls = value.persistentCalls;
    return *this;
}

const List<Reflection::ValueKind>& OrbEvent::GetParameterKinds() const { return parameterKinds; }
List<OrbEventCall>& OrbEvent::GetPersistentCalls() { return persistentCalls; }
const List<OrbEventCall>& OrbEvent::GetPersistentCalls() const { return persistentCalls; }

uint64 OrbEvent::Subscribe(std::function<void(std::span<const Reflection::Value>)> listener)
{
    if (!listener) throw std::invalid_argument("Empty OrbEvent listener");
    uint64 id = nextListenerId++;
    listeners.emplace_back(id, std::move(listener));
    return id;
}

bool OrbEvent::Unsubscribe(uint64 listenerId)
{
    return std::erase_if(listeners, [listenerId](const auto& listener) { return listener.first == listenerId; }) != 0;
}

void OrbEvent::ClearSubscriptions() { listeners.clear(); }

bool OrbEvent::Dispatch(std::span<const Reflection::Value> arguments)
{
    if (arguments.size() != parameterKinds.size() || invocationDepth >= 32) return false;
    for (usize index = 0; index < arguments.size(); ++index)
        if (arguments[index].GetKind() != parameterKinds[index]) return false;

    //快照全部监听器
    auto persistent = persistentCalls;
    auto runtime = listeners;
    ++invocationDepth;
    struct InvocationScope { uint32& depth; ~InvocationScope() { --depth; } } scope{ invocationDepth };
    bool success = true;
    for (const auto& listener : persistent)
    {
        if (!listener.enabled) continue;
        try
        {
            Object* object = Object::FindObject(StringId(listener.targetKey));
            Ens* owner = object ? object->Cast<Ens>() : nullptr;
            if (!owner || !owner->IsValid() || listener.occurrence < 0
                || listener.targetType.empty() || listener.method.empty())
            { Log::Error("OrbEvent: missing listener target or method"); success = false; continue; }
            ScriptInterop::ComponentProxy proxy = listener.domain == OrbEventDomain::Native
                ? ScriptInterop::FindNativeComponent(owner->GetId(), listener.targetType, listener.occurrence)
                : ScriptInterop::FindManagedComponent(owner->GetId(), listener.targetType, listener.occurrence);
            Reflection::Value result;
            auto status = proxy.Invoke(listener.method, listener.useArguments ? arguments : std::span<const Reflection::Value>{}, result);
            if (status != ScriptInterop::InteropStatus::Ok)
            { Log::Error(("OrbEvent: invocation failed: " + listener.targetType + "." + listener.method).c_str()); success = false; }
        }
        catch (...) { Log::Error("OrbEvent: persistent listener threw"); success = false; }
    }
    for (const auto& listener : runtime)
    {
        try { listener.second(arguments); }
        catch (...) { Log::Error("OrbEvent: runtime listener threw"); success = false; }
    }
    return success;
}

std::string OrbEvent::Serialize() const
{
    List<std::string> signature;
    for (auto kind : parameterKinds) signature.push_back(std::to_string(static_cast<uint32>(kind)));
    List<std::string> entries{ "1", Reflection::FormatArrayValues(signature) };
    for (const auto& listener : persistentCalls)
        entries.push_back(Reflection::FormatArrayValues({ listener.targetKey,
            std::to_string(static_cast<uint32>(listener.domain)), listener.targetType,
            std::to_string(listener.occurrence), listener.method, listener.enabled ? "1" : "0", listener.useArguments ? "1" : "0" }));
    return Reflection::FormatArrayValues(entries);
}

bool OrbEvent::ReadSignature(const std::string& text, List<Reflection::ValueKind>& signature)
{
    List<std::string> entries, kinds;
    if (!Reflection::ParseArrayValues(text, entries) || entries.size() < 2 || entries[0] != "1"
        || !Reflection::ParseArrayValues(entries[1], kinds)) return false;
    List<Reflection::ValueKind> parsed;
    for (const auto& entry : kinds)
    {
        uint32 kind;
        if (!Reflection::SetFromXmlValue(kind, entry) || kind == 0 || kind == static_cast<uint32>(Reflection::ValueKind::Array)
            || kind > static_cast<uint32>(Reflection::ValueKind::Vector2)) return false;
        parsed.push_back(static_cast<Reflection::ValueKind>(kind));
    }
    signature = std::move(parsed);
    return true;
}

bool OrbEvent::Deserialize(const std::string& text)
{
    List<Reflection::ValueKind> signature;
    List<std::string> entries;
    if (!ReadSignature(text, signature) || signature != parameterKinds || !Reflection::ParseArrayValues(text, entries)) return false;
    List<OrbEventCall> parsed;
    for (usize index = 2; index < entries.size(); ++index)
    {
        List<std::string> fields;
        uint32 domain;
        int32 occurrence;
        if (!Reflection::ParseArrayValues(entries[index], fields) || fields.size() != 7
            || !Reflection::SetFromXmlValue(domain, fields[1]) || (domain != 1 && domain != 2)
            || !Reflection::SetFromXmlValue(occurrence, fields[3]) || occurrence < 0
            || (fields[5] != "0" && fields[5] != "1") || (fields[6] != "0" && fields[6] != "1")) return false;
        parsed.push_back({ fields[0], static_cast<OrbEventDomain>(domain), fields[2], occurrence, fields[4], fields[5] == "1", fields[6] == "1" });
    }
    persistentCalls = std::move(parsed);
    return true;
}

bool OrbEvent::RemapTargets(std::string& text, const std::function<std::string(const std::string&)>& remap)
{
    List<Reflection::ValueKind> signature;
    if (!ReadSignature(text, signature)) return false;
    OrbEvent event(std::move(signature));
    if (!event.Deserialize(text)) return false;
    for (auto& listener : event.persistentCalls) listener.targetKey = remap(listener.targetKey);
    text = event.Serialize();
    return true;
}

namespace Reflection
{
    Value ToValue(const OrbEvent& value) { return Value(value.Serialize()); }
    bool SetFromValue(OrbEvent& target, const Value& value)
    {
        std::string text;
        return value.TryGet(text) && target.Deserialize(text);
    }
    std::string ToXmlValue(const OrbEvent& value) { return value.Serialize(); }
    bool SetFromXmlValue(OrbEvent& target, const std::string& text) { return target.Deserialize(text); }
}
