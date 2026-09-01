#include "Editor/NativeGameModule.h"

#include "FileSystem/Utf8Path.h"
#include "Runtime/Object/Object.h"
#include "Runtime/EnsId.h"
#include "Scripting/NativeGameModule.h"

#include <filesystem>

namespace
{
    std::string ToCleanPath(const std::filesystem::path& path)
    {
        return Utf8Path::ToUtf8(path.lexically_normal());
    }

    bool CopyModuleShadow(const std::string& sourcePath,
        const std::string& shadowDirectory,
        uint64 version,
        std::string& outputPath,
        std::string& error)
    {
        std::filesystem::path source = Utf8Path::FromUtf8(sourcePath);
        if (!std::filesystem::exists(source))
        {
            error = "Native game DLL was not found: " + sourcePath;
            return false;
        }

        std::error_code fileError;
        std::filesystem::path directory = Utf8Path::FromUtf8(shadowDirectory);
        std::filesystem::create_directories(directory, fileError);
        if (fileError)
        {
            error = "Create native module shadow directory failed: " + fileError.message();
            return false;
        }

        std::filesystem::path shadow = directory
            / (source.stem().string() + "." + std::to_string(version) + source.extension().string());
        std::filesystem::copy_file(source, shadow, std::filesystem::copy_options::overwrite_existing, fileError);
        if (fileError)
        {
            error = "Copy native game DLL failed: " + fileError.message();
            return false;
        }

        std::filesystem::path sourcePdb = source;
        sourcePdb.replace_extension(".pdb");
        if (std::filesystem::exists(sourcePdb))
        {
            std::filesystem::path shadowPdb = shadow;
            shadowPdb.replace_extension(".pdb");
            fileError.clear();
            std::filesystem::copy_file(sourcePdb, shadowPdb, std::filesystem::copy_options::overwrite_existing, fileError);
        }

        outputPath = ToCleanPath(std::filesystem::absolute(shadow));
        return true;
    }
}

NativeGameModule::~NativeGameModule()
{
    std::string ignored;
    Unload(ignored);
}

bool NativeGameModule::LoadCandidate(const std::string& sourcePath,
    const std::string& shadowDirectory,
    std::string& error)
{
    std::string candidatePath;
    if (!CopyModuleShadow(sourcePath, shadowDirectory, ++shadowVersion, candidatePath, error)) return false;

    Object::BeginModuleTypeRegistration(this);
    DynamicLibrary candidate = LoadDynamicLibrary(Utf8Path::FromUtf8(candidatePath));
    bool typesRegistered = Object::EndModuleTypeRegistration(candidate.handle != nullptr);
    if (!candidate.handle)
    {
        error = "Load native game DLL failed: " + candidatePath;
        return false;
    }
    if (!typesRegistered)
    {
        UnloadDynamicLibrary(candidate);
        error = "Native game DLL contains duplicate type names: " + candidatePath;
        return false;
    }

    auto getApi = reinterpret_cast<GetOrbedenNativeGameModuleApi>(
        GetDynamicLibrarySymbol(candidate, OrbedenNativeGameModuleEntryPoint));
    const OrbedenNativeGameModuleApi* api = getApi ? getApi() : nullptr;
    if (!api
        || api->abiVersion != OrbedenNativeGameModuleAbiVersion
        || api->structSize < sizeof(OrbedenNativeGameModuleApi)
        || !api->registerReflection)
    {
        Object::UnregisterModuleTypes(this);
        UnloadDynamicLibrary(candidate);
        error = "Native game DLL ABI mismatch: " + candidatePath;
        return false;
    }

    api->registerReflection();
    library = candidate;
    shadowPath = candidatePath;
    return true;
}

bool NativeGameModule::Reload(const std::string& sourcePath,
    const std::string& shadowDirectory,
    const List<std::string>& requiredTypes,
    std::string& error)
{
    error.clear();
    std::string previousPath = shadowPath;
    if (IsLoaded() && !Unload(error)) return false;

    if (LoadCandidate(sourcePath, shadowDirectory, error))
    {
        bool missingType = false;
        for (const std::string& typeName : requiredTypes)
        {
            Type* type = Object::FindType(typeName);
            if (!type || type->GetModuleOwner() != this || !type->Is(Component::StaticType()))
            {
                error = "Native module is missing World component type: " + typeName;
                missingType = true;
                break;
            }
        }
        if (!missingType) return true;

        std::string ignored;
        Unload(ignored);
    }

    if (!previousPath.empty())
    {
        std::string rollbackError;
        if (LoadCandidate(previousPath, shadowDirectory, rollbackError))
        {
            error += " Previous native module was restored.";
            return false;
        }
        error += " Rollback also failed: " + rollbackError;
    }
    return false;
}

bool NativeGameModule::Unload(std::string& error)
{
    error.clear();
    if (!IsLoaded()) return true;
    if (!Object::UnregisterModuleTypes(this))
    {
        error = "Native game module still owns live objects.";
        return false;
    }

    UnloadDynamicLibrary(library);
    shadowPath.clear();
    return true;
}

bool NativeGameModule::IsLoaded() const
{
    return library.handle != nullptr;
}

const std::string& NativeGameModule::GetShadowPath() const
{
    return shadowPath;
}
