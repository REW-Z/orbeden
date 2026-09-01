#include "Platform/DynamicLibrary.h"
#include "Runtime/Object/Object.h"
#include "Runtime/Reflection.h"
#include "Scripting/NativeGameModule.h"
#include "Scripting/ScriptBehaviour.h"

#include <filesystem>

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 3) return 10;

    int moduleOwner = 0;
    Object::BeginModuleTypeRegistration(&moduleOwner);
    DynamicLibrary library = LoadDynamicLibrary(std::filesystem::path(arguments[1]));
    bool typesRegistered = Object::EndModuleTypeRegistration(library.handle != nullptr);
    if (!library.handle || !typesRegistered) return 11;

    auto getApi = reinterpret_cast<GetOrbedenNativeGameModuleApi>(
        GetDynamicLibrarySymbol(library, OrbedenNativeGameModuleEntryPoint));
    const OrbedenNativeGameModuleApi* api = getApi ? getApi() : nullptr;
    if (!api || api->abiVersion != OrbedenNativeGameModuleAbiVersion || !api->registerReflection) return 12;
    api->registerReflection();

    Type* baseType = Object::FindType("TestNativeBehaviour");
    Type* derivedType = Object::FindType("TestDerivedNativeBehaviour");
    if (!baseType || !derivedType || baseType->GetModuleOwner() != &moduleOwner || derivedType->GetModuleOwner() != &moduleOwner) return 13;

    ScriptCallbackTable baseCallbacks = ResolveScriptCallbacks(baseType);
    ScriptCallbackTable derivedCallbacks = ResolveScriptCallbacks(derivedType);
    if (!baseCallbacks.start || !baseCallbacks.update || !baseCallbacks.end) return 14;
    if (derivedCallbacks.update != baseCallbacks.update || !derivedCallbacks.lateUpdate) return 15;

    Object* object = derivedType->CreateObject();
    if (!object) return 16;
    ScriptBehaviour* script = static_cast<ScriptBehaviour*>(object);
    derivedCallbacks.start(script);
    derivedCallbacks.update(script, 0.5f);
    derivedCallbacks.lateUpdate(script, 0.25f);
    derivedCallbacks.end(script);

    const Reflection::FieldInfo* speedField = Reflection::FindField(derivedType, "speed");
    const Reflection::FieldInfo* privateField = Reflection::FindField(derivedType, "privateValue");
    if (!speedField || speedField->GetValueAsString(object) != "2.5") return 17;
    if (!privateField || privateField->GetValueAsString(object) != "3.25") return 18;
    derivedType->DestroyObject(object);

    if (!Object::UnregisterModuleTypes(&moduleOwner)) return 19;
    if (Object::FindType("TestNativeBehaviour") || Object::FindType("TestDerivedNativeBehaviour")) return 20;
    UnloadDynamicLibrary(library);

    Object::BeginModuleTypeRegistration(&moduleOwner);
    DynamicLibrary duplicateLibrary = LoadDynamicLibrary(std::filesystem::path(arguments[2]));
    bool duplicateTypesRegistered = Object::EndModuleTypeRegistration(duplicateLibrary.handle != nullptr);
    if (!duplicateLibrary.handle) return 21;
    if (duplicateTypesRegistered) return 22;
    UnloadDynamicLibrary(duplicateLibrary);
    return 0;
}
