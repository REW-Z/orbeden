#include "Editor/PlayerContentCooker.h"
#include "FileSystem/PathDefines.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/AssetPipeline.h"
#include "Runtime/CookedAssetSerializer.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Texture2D.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

/// <summary>检查真实引擎运行结果，失败时返回非零退出码。</summary>
void Require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}

//把原始字节折进描述哈希，避免为大体重数组构造超长比较文本
void FoldBytes(uint64& hash, const void* data, usize size)
{
    const uint8* bytes = static_cast<const uint8*>(data);
    for (usize index = 0; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
}

//折入一个浮点数，按位比较不受格式化影响
void FoldFloat(uint64& hash, float32 value)
{
    FoldBytes(hash, &value, sizeof(value));
}

//折入一个定长记录数组
template<typename T>
void FoldArray(uint64& hash, const List<T>& values)
{
    if (values.empty()) return;
    FoldBytes(hash, values.data(), values.size() * sizeof(T));
}

//描述纹理的全部持久化字段
std::string DescribeTexture(Texture2D* texture)
{
    std::ostringstream stream;
    stream << "Texture2D|" << texture->name << '|' << texture->width << '|' << texture->height
        << '|' << texture->channels << '|' << texture->format
        << '|' << static_cast<uint32>(texture->colorSpace) << '|' << texture->pixels.size();

    uint64 pixelsHash = 14695981039346656037ull;
    FoldArray(pixelsHash, texture->pixels);
    stream << '|' << pixelsHash;
    return stream.str();
}

//描述网格的全部持久化字段
std::string DescribeMesh(Mesh* mesh)
{
    std::ostringstream stream;
    stream << "Mesh|" << mesh->name << '|' << mesh->vertices.size() << '|' << mesh->texcoords.size()
        << '|' << mesh->normals.size() << '|' << mesh->tangents.size() << '|' << mesh->indices.size()
        << '|' << mesh->subMeshes.size();

    uint64 geometryHash = 14695981039346656037ull;
    FoldArray(geometryHash, mesh->vertices);
    FoldArray(geometryHash, mesh->texcoords);
    FoldArray(geometryHash, mesh->normals);
    FoldArray(geometryHash, mesh->tangents);
    FoldArray(geometryHash, mesh->indices);
    stream << '|' << geometryHash;

    for (const SubMesh& subMesh : mesh->subMeshes)
    {
        stream << '|' << subMesh.name << '|' << subMesh.indexStart << '|' << subMesh.indexCount;
    }

    return stream.str();
}

//描述材质的全部持久化字段
std::string DescribeMaterial(Material* material)
{
    std::ostringstream stream;
    stream << "Material|" << material->name << '|' << material->shader.GetInstanceId().GetPath();

    for (const MaterialTextureSlot& slot : material->textureSlots)
    {
        stream << '|' << slot.name << '=' << slot.texture.GetInstanceId().GetPath();
    }

    uint64 slotHash = 14695981039346656037ull;
    for (const MaterialColorSlot& slot : material->colorSlots)
    {
        stream << '|' << slot.name;
        FoldFloat(slotHash, slot.value.r);
        FoldFloat(slotHash, slot.value.g);
        FoldFloat(slotHash, slot.value.b);
        FoldFloat(slotHash, slot.value.a);
    }

    for (const MaterialFloatSlot& slot : material->floatSlots)
    {
        stream << '|' << slot.name;
        FoldFloat(slotHash, slot.value);
    }

    stream << '|' << slotHash;
    return stream.str();
}

//描述着色器的全部持久化字段
std::string DescribeShader(Shader* shader)
{
    std::ostringstream stream;
    stream << "Shader|" << shader->name << '|' << shader->vertexPath << '|' << shader->fragmentPath
        << '|' << shader->passes.size();

    for (const ShaderPass& pass : shader->passes)
    {
        stream << '|' << pass.name
            << '|' << static_cast<uint32>(pass.state.depthTest) << static_cast<uint32>(pass.state.depthWrite)
            << static_cast<uint32>(pass.state.blend) << static_cast<uint32>(pass.state.cull)
            << '|' << pass.vertexSource.size() << '|' << pass.fragmentSource.size();
    }

    //槽位由源码反射重建，一并比对以确认重建结果一致。
    for (const ShaderTextureSlot& slot : shader->textureSlots) stream << '|' << slot.name;
    for (const ShaderColorSlot& slot : shader->colorSlots) stream << '|' << slot.name;
    for (const ShaderFloatSlot& slot : shader->floatSlots) stream << '|' << slot.name;

    return stream.str();
}

//按运行时类型描述一个资源对象
std::string Describe(Object* object)
{
    if (Texture2D* texture = object->Cast<Texture2D>()) return DescribeTexture(texture);
    if (Mesh* mesh = object->Cast<Mesh>()) return DescribeMesh(mesh);
    if (Material* material = object->Cast<Material>()) return DescribeMaterial(material);
    if (Shader* shader = object->Cast<Shader>()) return DescribeShader(shader);

    throw std::runtime_error("Unexpected resource type: " + std::string(object->GetType()->GetName()));
}

/// <summary>导入真实资源、写成产物、清空后再从产物读回，逐字段比对。</summary>
int main() try
{
    const std::string sourceContentRoot = std::filesystem::absolute("OrbedenEditor/Templates").generic_string();
    const std::filesystem::path cookedRoot = std::filesystem::absolute("Log/CookedAssetRoundTrip/cooked");
    std::filesystem::remove_all(cookedRoot);
    std::filesystem::create_directories(cookedRoot);

    //真实导入覆盖纹理、OBJ 复合资源与 OrbShader 三条导入路径。
    const List<std::string> sourceKeys =
    {
        "Examples/FlightTraining/Textures/sky_blue.png",
        "Examples/FlightTraining/Meshes/ground.obj",
        "Builtin/Shaders/blinn_phong.orbshader",
    };

    PathDefines::SetContentRoot(sourceContentRoot);

    List<std::string> objectKeys;
    List<std::string> importedDescriptions;
    std::string meshKey;
    std::string materialKey;
    for (const std::string& sourceKey : sourceKeys)
    {
        AssetCollection collection = AssetPipeline::ImportSource(sourceKey);
        Require(collection.Succeeded(), "Cannot import source asset: " + sourceKey);
        Require(!collection.objectKeys.empty(), "Import produced no objects: " + sourceKey);

        for (usize index = 0; index < collection.objectKeys.size(); ++index)
        {
            const std::string& objectKey = collection.objectKeys[index];
            Object* object = collection.objects[index];
            const ResourceManager::ResourceRecord* record = ResourceManager::FindRecord(objectKey);
            List<std::string> dependencies = record ? record->dependencies : List<std::string>();

            std::filesystem::path blobPath = cookedRoot / CookedAssetSerializer::GetBlobFileName(objectKey);
            std::string error;
            Require(CookedAssetSerializer::Write(std::filesystem::absolute(blobPath).generic_string(), object, sourceKey, dependencies, error),
                "Cooked write failed for " + objectKey + ": " + error);

            objectKeys.push_back(objectKey);
            importedDescriptions.push_back(Describe(object));

            if (object->Is(Mesh::StaticType())) meshKey = objectKey;
            if (object->Is(Material::StaticType())) materialKey = objectKey;
        }
    }

    Require(objectKeys.size() > 3, "Not enough objects were imported to exercise the format");

    std::string indexError;
    std::filesystem::path indexPath = cookedRoot / CookedAssetSerializer::IndexFileName;
    Require(CookedAssetSerializer::WriteIndex(std::filesystem::absolute(indexPath).generic_string(), objectKeys, indexError),
        "Cooked index write failed: " + indexError);

    //清空全部资源，改成只读产物目录，模拟发布包内的解析路径。
    ResourceManager::Shutdown();
    PathDefines::SetContentRoot(std::filesystem::absolute(cookedRoot).generic_string());

    List<std::string> indexedKeys;
    Require(CookedAssetSerializer::ReadIndex(indexedKeys), "Cooked index could not be read back");
    Require(indexedKeys.size() == objectKeys.size(), "Cooked index lost entries");

    for (usize index = 0; index < objectKeys.size(); ++index)
    {
        const std::string& objectKey = objectKeys[index];
        Object* loaded = ResourceManager::Load(nullptr, objectKey);
        Require(loaded != nullptr, "Cooked asset could not be loaded: " + objectKey);
        Require(loaded->GetInstanceId().GetPath() == objectKey, "Cooked asset identity changed: " + objectKey);
        Require(Describe(loaded) == importedDescriptions[index], "Cooked asset differs after round trip: " + objectKey);
    }

    std::cout << "PASS: " << objectKeys.size() << " real assets survived the cooked round trip\n";

    //只读产物目录里单独取一个对象：跨文件引用必须由产物加载路径递归补齐，
    //否则材质拿不到它的 Shader。
    Require(!meshKey.empty() && !materialKey.empty(), "Test assets did not produce a mesh and a material");
    ResourceManager::Shutdown();

    Mesh* mesh = ResourceManager::Load<Mesh>(meshKey);
    Require(mesh && !mesh->subMeshes.empty(), "Cooked mesh could not be loaded alone: " + meshKey);

    Material* material = ResourceManager::Load<Material>(materialKey);
    Require(material != nullptr, "Cooked material could not be loaded alone: " + materialKey);
    Require(material->shader.Get() != nullptr, "Cooked material did not pull in its shader");

    std::cout << "PASS: cross file references are restored from cooked assets\n";

    //打包器：真实走一遍内容根扫描、分类、导入、写产物与复制场景。
    ResourceManager::Shutdown();

    const std::filesystem::path packageRoot = std::filesystem::absolute("Log/CookedAssetRoundTrip");
    const std::filesystem::path packageCacheRoot = packageRoot / "ResourceCache" / "Player";
    const std::filesystem::path guardedRoot = packageRoot / "NotACache";
    std::filesystem::remove_all(packageCacheRoot);
    std::filesystem::remove_all(guardedRoot);

    //输出目录会被清空重建，名字不是 ResourceCache 时必须拒绝。
    std::string guardError;
    Require(!PlayerContentCooker::Cook(sourceContentRoot, guardedRoot.generic_string(), guardError),
        "Cooker accepted an output directory that is not the project ResourceCache");
    Require(!std::filesystem::exists(guardedRoot), "Refused cook still touched the output directory");

    std::string cookError;
    Require(PlayerContentCooker::Cook(sourceContentRoot, packageCacheRoot.generic_string(), cookError),
        "Cook failed: " + cookError);
    Require(std::filesystem::exists(packageCacheRoot / CookedAssetSerializer::IndexFileName), "Cook produced no index");
    Require(std::filesystem::exists(packageCacheRoot / "Examples/FlightTraining/Scenes/main.world"),
        "Cook did not copy the world file");

    //产物目录里只应有产物与场景，源文件一个都不该进去。
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(packageCacheRoot))
    {
        if (!entry.is_regular_file()) continue;

        const std::string extension = entry.path().extension().string();
        Require(extension == ".orbo" || extension == ".world" || extension == ".index",
            "Cook copied a source file into the package: " + entry.path().string());
    }

    //产物目录里没有任何源文件，能加载出资源就说明确实走的是产物路径。
    PathDefines::SetContentRoot(packageCacheRoot.generic_string());

    //内置 Shader 靠文件名在内容根内查找，哈希命名抹掉了文件名，打包后只能靠清单匹配。
    const std::string builtinShaderFileName = "shadow_depth.orbshader";
    List<std::string> cookedIndexKeys;
    Require(CookedAssetSerializer::ReadIndex(cookedIndexKeys), "Cooked index could not be read back");
    bool foundBuiltinShader = false;
    for (const std::string& key : cookedIndexKeys)
    {
        if (key.size() < builtinShaderFileName.size()) continue;
        if (key.compare(key.size() - builtinShaderFileName.size(), builtinShaderFileName.size(), builtinShaderFileName) == 0)
        {
            foundBuiltinShader = true;
        }
    }

    Require(foundBuiltinShader, "Builtin shader is not reachable by file name through the cooked index");

    Require(ResourceManager::Load<Mesh>("Examples/FlightTraining/Meshes/ground.obj//Mesh/Main") != nullptr,
        "Cooked package did not resolve a mesh without any source files");
    Require(ResourceManager::Load<Shader>("Builtin/Shaders/blinn_phong.orbshader") != nullptr,
        "Cooked package did not resolve a shader without any source files");
    Require(ResourceManager::Load<Texture2D>("Examples/FlightTraining/Textures/sky_blue.png") != nullptr,
        "Cooked package did not resolve a texture without any source files");

    std::cout << "PASS: cooker produced a self contained package that resolves without sources\n";

    //只读目录里没有源文件，缺失产物必须失败而不是静默回退。
    Require(ResourceManager::Load(nullptr, "Missing/Nothing.obj//Mesh/Main") == nullptr, "Missing cooked asset did not fail");
    std::cout << "PASS: missing cooked asset fails instead of silently succeeding\n";

    ResourceManager::Shutdown();
    PathDefines::Clear();
    std::filesystem::remove_all(packageCacheRoot);
    std::filesystem::remove_all(cookedRoot);
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
