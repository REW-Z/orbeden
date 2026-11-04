#include "ResourceManager/ResourceLoadOperation.h"

#include "FileSystem/PathDefines.h"
#include "Runtime/Native/InteropText.h"
#include "Runtime/Fonts/FontRasterizer.h"
#include "Runtime/Object/Font.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Skybox.h"
#include "Runtime/Object/TextResource.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

thread_local ResourceLoadOperation::State* ResourceLoadOperation::current = nullptr;

ResourceLoadOperation::ResourceLoadOperation(const std::string& key, Type* type)
    : key(ResourceManager::ToResourceKey(key)), type(type)
{
}

std::shared_ptr<ResourceLoadOperation> ResourceLoadOperation::Load(uint32 typeId, const std::string& key)
{
    return ResourceManager::LoadAsync(Object::FindType(typeId), key);
}

ResourceLoadOperation::State* ResourceLoadOperation::Current()
{
    return current;
}

Object* ResourceLoadOperation::GetResult() const
{
    return finished ? Object::FindObjectById(resultId) : nullptr;
}

ResourceLoadOperation::State::State(const std::string& key)
    : key(key), contentRoot(PathDefines::GetContentRoot())
{
    for (Type* type : {Texture2D::StaticType(), Mesh::StaticType(), Material::StaticType(),
        Shader::StaticType(), Skybox::StaticType(), Font::StaticType(), TextResource::StaticType()})
    {
        types.emplace(type->GetName(), type);
    }
}

ResourceLoadOperation::State::~State()
{
    Discard();
}

void ResourceLoadOperation::State::Prepare()
{
    current = this;
    try
    {
        if (!ResourceManager::TryLoad(nullptr, key) && records.empty() && error.empty())
        {
            error = "Resource could not be loaded: " + key;
        }
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
    }
    catch (...)
    {
        error = "Resource preparation failed with a non-standard exception.";
    }
    FontRasterizer::Shutdown();
    current = nullptr;
}

std::string ResourceLoadOperation::State::GetFilePath(const std::string& path) const
{
    return InteropText::PathToUtf8((InteropText::PathFromUtf8(contentRoot) / InteropText::PathFromUtf8(path)).lexically_normal());
}

bool ResourceLoadOperation::State::Finalize()
{
    //复用主线程已经发布的资源
    for (auto& [key, record] : records)
    {
        if (Object* existing = ResourceManager::FindLoaded(key))
        {
            if (existing->GetType() != record.type)
            {
                error = "Published resource type changed: " + key;
                return false;
            }
            objects[key] = existing;
            record.object = existing;
        }
    }

    //在私有资源图中校验主线程依赖
    current = this;
    try
    {
        for (const auto& [key, record] : records)
        {
            for (const std::string& dependency : record.dependencies)
            {
                if (!objects.contains(dependency))
                {
                    error = "Resource dependency could not be loaded: " + key + " -> " + dependency;
                    break;
                }
            }
            if (!error.empty())
            {
                break;
            }
            if (record.object->IsPublished())
            {
                continue;
            }
            if (Skybox* skybox = record.object->Cast<Skybox>())
            {
                if (!skybox->ValidateFaces(error))
                {
                    break;
                }
            }
            publishObjects.push_back(record.object);
        }
    }
    catch (...)
    {
        current = nullptr;
        throw;
    }
    current = nullptr;
    finalized = error.empty();
    return finalized;
}

bool ResourceLoadOperation::State::Commit(float64 budgetMilliseconds)
{
    if (!finalized && !Finalize())
    {
        return true;
    }

    //分批登记完整对象
    auto start = std::chrono::steady_clock::now();
    while (commitIndex < publishObjects.size())
    {
        if (!Object::RegisterResourceInstance(publishObjects[commitIndex]))
        {
            error = "Resource registration conflicted: " + publishObjects[commitIndex]->GetInstanceId().GetPath();
            return true;
        }
        Object* object = publishObjects[commitIndex];
        if (!ResourceManager::RegisterObject(object->GetInstanceId().GetPath(), object))
        {
            error = "Resource cache registration conflicted: " + object->GetInstanceId().GetPath();
            return true;
        }
        ++commitIndex;
        if (std::chrono::duration<float64, std::milli>(std::chrono::steady_clock::now() - start).count() >= budgetMilliseconds)
        {
            return false;
        }
    }

    //补齐新资源的依赖记录
    for (const auto& [key, record] : records)
    {
        if (record.object->IsPublished())
        {
            continue;
        }
        for (const std::string& dependency : record.dependencies)
        {
            ResourceManager::RegisterDependency(key, dependency);
        }
    }

    //预留分配区登记空间
    for (const auto& [type, chunk] : chunks)
    {
        type->resourceChunks.reserve(type->resourceChunks.size() + 1);
    }

    //释放已被主线程缓存替代的对象
    for (usize index = 0; index < ownedObjects.size();)
    {
        Object* object = ownedObjects[index];
        if (objects[object->GetInstanceId().GetPath()] != object)
        {
            ownedObjects.erase(ownedObjects.begin() + index);
            Object::DestroyUnpublishedResource(object);
        }
        else
        {
            ++index;
        }
    }

    //移交分配区并开放完整资源图
    for (auto& [type, chunk] : chunks)
    {
        bool used = std::any_of(publishObjects.begin(), publishObjects.end(), [type](Object* object)
        {
            return object->GetType() == type;
        });
        if (used)
        {
            type->AdoptResourceChunk(chunk);
        }
        else
        {
            type->DestroyResourceChunk(chunk);
        }
    }
    chunks.clear();
    Object::PublishResourceInstances(publishObjects);
    ownedObjects.clear();
    committed = true;
    return true;
}

void ResourceLoadOperation::State::Discard()
{
    for (Object* object : ownedObjects)
    {
        if (!object->indexed || !ResourceManager::DestroyObject(object))
        {
            Object::DestroyUnpublishedResource(object);
        }
    }
    ownedObjects.clear();
    for (auto& [type, chunk] : chunks)
    {
        type->DestroyResourceChunk(chunk);
    }
    chunks.clear();
    objects.clear();
    records.clear();
}
