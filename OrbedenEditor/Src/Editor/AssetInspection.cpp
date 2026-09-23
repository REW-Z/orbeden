#include "Editor/AssetInspection.h"

#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/AssetPipeline.h"
#include "Runtime/CookedAssetSerializer.h"
#include "Runtime/Reflection.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Texture2D.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>
std::string AssetInspection::Inspect(const std::string& sourceKey, const std::string& outputDirectory)
{
    std::string result;
    List<std::string> sourceFiles;
    std::string key = sourceKey;
    auto diagnostic = [&](const std::string& text)
    {
        result += std::string("message") + '\0' + text + '\0' + '\0' + '\0';
    };
    std::filesystem::path relative = Utf8Path::FromUtf8(key);
    if (relative.is_absolute() || relative.has_root_name() || key.find(":") != std::string::npos
        || key.find("//") != std::string::npos) return result;
    for (const auto& part : relative) if (part == "..") return result;
    std::string fullPath = PathDefines::GetContentFilePath(key);
    sourceFiles.push_back(fullPath);
    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(Utf8Path::FromUtf8(fullPath), errorCode))
    {
        diagnostic("Source file is missing or inaccessible.");
        return result;
    }
    //GLSL 源对以不含 .vert/.frag.glsl 的基名注册为一个 Shader。
    for (const std::string suffix : { ".vert.glsl", ".frag.glsl" })
        if (key.ends_with(suffix))
        {
            key.resize(key.size() - suffix.size());
            sourceFiles.push_back(PathDefines::GetContentFilePath(key + ".vert.glsl"));
            sourceFiles.push_back(PathDefines::GetContentFilePath(key + ".frag.glsl"));
            break;
        }
    std::string extension = Utf8Path::ToUtf8(relative.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    bool cooked = extension == ".orbo";
    List<Object*> objects;
    auto collect = [&]()
    {
        objects.clear();
        for (TypeRuntimeId index = 0; index < Object::GetTypeCount(); ++index)
        {
            Type* type = Object::FindType(index);
            if (!type) continue;
            type->ForEachLiveObject([&](Object* object)
            {
                const std::string& objectKey = object->GetInstanceId().GetPath();
                if (ResourceManager::FindLoaded(objectKey) != object) return;
                bool matches = cooked
                    ? CookedAssetSerializer::GetBlobFileName(objectKey) == Utf8Path::ToUtf8(relative.filename())
                    : ResourceManager::GetSourceKey(objectKey) == key;
                if (matches && std::find(objects.begin(), objects.end(), object) == objects.end()) objects.push_back(object);
            });
        }
    };
    collect();
    if (!cooked || objects.empty())
    {
        if (cooked)
        {
            List<std::string> references;
            std::string error;
            if (!CookedAssetSerializer::Read(fullPath, references, error)) result += std::string("error") + '\0' + error + '\0' + '\0' + '\0';
        }
        else if (AssetPipeline::SelectImporter(key) != AssetImporter::None)
        {
            AssetCollection collection = AssetPipeline::ImportSource(key);
            sourceFiles.insert(sourceFiles.end(), collection.sourceFiles.begin(), collection.sourceFiles.end());
            objects.clear();
            for (Object* object : collection.objects)
                if (object && ResourceManager::GetSourceKey(object->GetInstanceId().GetPath()) == key
                    && std::find(objects.begin(), objects.end(), object) == objects.end()) objects.push_back(object);
            for (const auto& warning : collection.warnings) diagnostic(warning);
            for (const auto& error : collection.errors) result += std::string("error") + '\0' + error + '\0' + '\0' + '\0';
        }
        if (cooked) collect();
    }
    std::sort(objects.begin(), objects.end(), [](Object* left, Object* right)
        { return left->GetInstanceId().GetPath() < right->GetInstanceId().GetPath(); });
    //每个子资源及其可达依赖分别写入产物，供主进程按对象加载。
    if (!outputDirectory.empty())
    {
        List<Object*> pending = objects;
        List<std::string> written;
        for (usize index = 0; index < pending.size(); ++index)
        {
            Object* object = pending[index];
            if (!object) continue;
            const std::string& objectKey = object->GetInstanceId().GetPath();
            if (std::find(written.begin(), written.end(), objectKey) != written.end()) continue;
            written.push_back(objectKey);
            const auto* record = ResourceManager::FindRecord(objectKey);
            List<std::string> dependencies = record ? record->dependencies : List<std::string>();
            std::string error;
            std::string blob = Utf8Path::ToUtf8(Utf8Path::FromUtf8(outputDirectory) / CookedAssetSerializer::GetBlobFileName(objectKey));
            if (!CookedAssetSerializer::Write(blob, object, ResourceManager::GetSourceKey(objectKey), dependencies, error))
                result += std::string("error") + '\0' + error + '\0' + '\0' + '\0';
            for (const std::string& dependency : dependencies)
            {
                Object* dependent = ResourceManager::FindLoaded(dependency);
                if (!dependent) dependent = ResourceManager::Load(nullptr, dependency);
                if (dependent) pending.push_back(dependent);
                std::string dependencySource = ResourceManager::GetSourceKey(dependency);
                sourceFiles.push_back(PathDefines::GetContentFilePath(dependencySource));
            }
        }
    }
    for (const std::string& source : sourceFiles)
        result += std::string("dependency") + '\0' + source + '\0' + '\0' + '\0';
    for (Object* object : objects)
    {
        result += std::string("object") + '\0' + CookedAssetSerializer::GetBlobFileName(object->GetInstanceId().GetPath()) + '\0'
            + object->GetInstanceId().GetPath() + '\0' + object->GetType()->GetName() + '\0';
        List<const Reflection::FieldInfo*> fields;
        Reflection::CollectFields(object->GetType(), fields);
        for (const Reflection::FieldInfo* field : fields)
        {
            if (!field || !field->name) continue;
            std::string value;
            if (!TryGetFieldSummary(object, field->name, value))
            {
                if (!field->getter) continue;
                value = field->GetValueAsString(object);
            }
            if (value.size() > 4096) value = value.substr(0, 4096) + "\n... (truncated)";
            std::replace(value.begin(), value.end(), '\0', ' ');
            result += std::string("field") + '\0' + field->name + '\0'
                + (field->typeName ? field->typeName : "") + '\0' + value + '\0';
        }
    }
    return result;
}
//复杂资源容器没有通用反射 getter，提供体量摘要及有意义的槽内容。
bool AssetInspection::TryGetFieldSummary(Object* object, const char* fieldName, std::string& result)
{
    std::string_view name(fieldName);
    auto count = [&](usize size) { result = std::to_string(size) + " elements"; return true; };
    if (Mesh* mesh = object->Cast<Mesh>())
    {
        if (name == "vertices") return count(mesh->vertices.size());
        if (name == "texcoords") return count(mesh->texcoords.size());
        if (name == "normals") return count(mesh->normals.size());
        if (name == "tangents") return count(mesh->tangents.size());
        if (name == "indices") return count(mesh->indices.size());
        if (name == "subMeshes")
        {
            count(mesh->subMeshes.size());
            for (const auto& item : mesh->subMeshes)
                result += "\n" + item.name + ": " + std::to_string(item.indexCount) + " indices @ " + std::to_string(item.indexStart);
            return true;
        }
    }
    if (Texture2D* texture = object->Cast<Texture2D>())
        if (name == "pixels") { result = std::to_string(texture->pixels.size()) + " bytes"; return true; }
    if (Material* material = object->Cast<Material>())
    {
        if (name == "textureSlots")
        {
            count(material->textureSlots.size());
            for (const auto& slot : material->textureSlots) result += "\n" + slot.name + ": " + slot.texture.GetInstanceId().GetPath();
            return true;
        }
        if (name == "colorSlots")
        {
            count(material->colorSlots.size());
            for (const auto& slot : material->colorSlots) result += "\n" + slot.name + ": " + Reflection::Value(slot.value).ToString();
            return true;
        }
        if (name == "floatSlots")
        {
            count(material->floatSlots.size());
            for (const auto& slot : material->floatSlots) result += "\n" + slot.name + ": " + std::to_string(slot.value);
            return true;
        }
    }
    if (Shader* shader = object->Cast<Shader>())
    {
        if (name == "passes")
        {
            count(shader->passes.size());
            for (const auto& pass : shader->passes) result += "\n" + pass.name;
            return true;
        }
        if (name == "textureSlots") return count(shader->textureSlots.size());
        if (name == "colorSlots") return count(shader->colorSlots.size());
        if (name == "floatSlots") return count(shader->floatSlots.size());
    }
    return false;
}
