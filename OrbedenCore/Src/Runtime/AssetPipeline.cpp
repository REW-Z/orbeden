#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <unordered_map>

#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Runtime/AssetPipeline.h"
#include "Runtime/ProjectContext.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/Resources/Material.h"
#include "Runtime/Resources/MaterialShader.h"
#include "Runtime/Resources/Mesh.h"
#include "Runtime/Resources/Texture2D.h"

#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"

namespace
{
    struct VertexKey
    {
    public:
        int32 position = -1;
        int32 texcoord = -1;
        int32 normal = -1;

        bool operator==(const VertexKey& other) const
        {
            return position == other.position && texcoord == other.texcoord && normal == other.normal;
        }
    };

    struct VertexKeyHash
    {
    public:
        usize operator()(const VertexKey& key) const
        {
            uint64 hash = 14695981039346656037ull;
            hash = (hash ^ static_cast<uint32>(key.position + 1)) * 1099511628211ull;
            hash = (hash ^ static_cast<uint32>(key.texcoord + 1)) * 1099511628211ull;
            hash = (hash ^ static_cast<uint32>(key.normal + 1)) * 1099511628211ull;
            return static_cast<usize>(hash);
        }
    };

    struct ObjCorner
    {
    public:
        VertexKey key;
        vector3 position;
        vector2 texcoord;
        vector3 normal;
        uint32 index = 0;
    };

    struct MaterialImportInfo
    {
    public:
        Material* material = nullptr;
        std::string key;
    };

    //判断字符串前缀
    bool StartsWith(const std::string& text, const std::string& prefix)
    {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    //转为小写
    std::string ToLower(std::string text)
    {
        for (char& ch : text)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }

        return text;
    }

    //规范化文件路径
    std::string NormalizePath(const std::string& path)
    {
        return ResourceManager::NormalizeKey(std::filesystem::path(path).lexically_normal().string());
    }

    //解析实际磁盘路径，保持资源 Key 不变但兼容不同工作目录
    std::string ResolveAssetPath(const std::string& path)
    {
        std::string normalizedPath = NormalizePath(path);
        if (FileSystem::Exist(normalizedPath)) return normalizedPath;

        if (StartsWith(normalizedPath, "Resources/"))
        {
            std::string projectCandidate = ProjectContext::ResolveResourcePath(normalizedPath);
            if (FileSystem::Exist(projectCandidate)) return projectCandidate;

            const char* prefixes[] =
            {
                "../",
                "../../",
                "../../../",
            };

            for (const char* prefix : prefixes)
            {
                std::string candidate = NormalizePath(std::string(prefix) + normalizedPath);
                if (FileSystem::Exist(candidate)) return candidate;
            }
        }

        return normalizedPath;
    }

    //获取扩展名小写文本
    std::string GetLowerExtension(const std::string& path)
    {
        return ToLower(std::filesystem::path(path).extension().string());
    }

    //生成可读且稳定的Key片段
    std::string SanitizeKeyName(const std::string& text, const std::string& fallback)
    {
        std::string result;
        result.reserve(text.size());
        for (char ch : text)
        {
            unsigned char value = static_cast<unsigned char>(ch);
            if (std::isalnum(value) || ch == '_' || ch == '-' || ch == '.')
            {
                result += ch;
            }
            else if (std::isspace(value) || ch == '/' || ch == '\\')
            {
                result += '_';
            }
        }

        return result.empty() ? fallback : result;
    }

    //按空白拆分字符串
    List<std::string> SplitWhitespace(const std::string& text)
    {
        List<std::string> result;
        std::stringstream stream(text);
        std::string token;
        while (stream >> token)
        {
            result.push_back(token);
        }

        return result;
    }

    //解析OBJ索引，支持负索引
    int32 ResolveObjIndex(int32 rawIndex, usize count)
    {
        if (rawIndex > 0) return rawIndex - 1;
        if (rawIndex < 0) return static_cast<int32>(count) + rawIndex;
        return -1;
    }

    //向量减法
    vector3 Subtract(const vector3& a, const vector3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    //向量加法
    void AddTo(vector3& target, const vector3& value)
    {
        target.x += value.x;
        target.y += value.y;
        target.z += value.z;
    }

    //向量归一化
    vector3 Normalize(const vector3& value)
    {
        float32 length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        if (length <= 0.000001f) return { 1.0f, 0.0f, 0.0f };

        return { value.x / length, value.y / length, value.z / length };
    }

    //计算三角形切线
    vector3 ComputeTangent(const vector3& p0, const vector3& p1, const vector3& p2, const vector2& uv0, const vector2& uv1, const vector2& uv2)
    {
        vector3 edge1 = Subtract(p1, p0);
        vector3 edge2 = Subtract(p2, p0);
        float32 du1 = uv1.x - uv0.x;
        float32 dv1 = uv1.y - uv0.y;
        float32 du2 = uv2.x - uv0.x;
        float32 dv2 = uv2.y - uv0.y;
        float32 denominator = du1 * dv2 - du2 * dv1;
        if (std::fabs(denominator) <= 0.000001f) return { 1.0f, 0.0f, 0.0f };

        float32 scale = 1.0f / denominator;
        vector3 tangent;
        tangent.x = scale * (dv2 * edge1.x - dv1 * edge2.x);
        tangent.y = scale * (dv2 * edge1.y - dv1 * edge2.y);
        tangent.z = scale * (dv2 * edge1.z - dv1 * edge2.z);
        return Normalize(tangent);
    }

    //查找或创建导入对象
    template<typename T>
    T* CreateImportedObject(const std::string& key)
    {
        std::string normalizedKey = ResourceManager::NormalizeKey(key);
        Object* loaded = ResourceManager::FindLoaded(normalizedKey);
        if (loaded) return loaded->Cast<T>();

        Object* found = Object::FindObject(StringId(normalizedKey));
        T* existing = found ? found->Cast<T>() : nullptr;
        if (existing)
        {
            ResourceManager::RegisterObject(normalizedKey, existing);
            return existing;
        }

        T* object = Object::CreateInstance<T>(normalizedKey);
        if (!object) return nullptr;

        ResourceManager::RegisterObject(normalizedKey, object);
        return object;
    }

    //导入图片到指定ObjectKey
    Texture2D* ImportImageAsKey(const std::string& filePath, const std::string& objectKey, AssetCollection& collection)
    {
        std::string normalizedFilePath = ResolveAssetPath(filePath);
        std::string normalizedObjectKey = ResourceManager::NormalizeKey(objectKey);
        collection.AddSourceFile(normalizedFilePath);

        Texture2D* texture = CreateImportedObject<Texture2D>(normalizedObjectKey);
        if (!texture)
        {
            collection.AddError("Failed to create Texture2D: " + normalizedObjectKey);
            return nullptr;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(normalizedFilePath.c_str(), &width, &height, &channels, 4);
        if (!pixels)
        {
            collection.AddError("Image decode failed: " + normalizedFilePath);
            return nullptr;
        }

        texture->name = Path::GetNameWithOutExtension(normalizedFilePath);
        texture->width = width;
        texture->height = height;
        texture->channels = 4;
        texture->format = 4;
        texture->pixels.assign(pixels, pixels + static_cast<usize>(width) * static_cast<usize>(height) * 4);
        stbi_image_free(pixels);

        collection.AddObject(normalizedObjectKey, texture, true);
        return texture;
    }

    //读取文本文件，失败时记录错误
    std::string LoadTextOrError(const std::string& path, AssetCollection& collection)
    {
        std::string filePath = ResolveAssetPath(path);
        if (!FileSystem::Exist(filePath))
        {
            collection.AddError("File does not exist: " + path);
            return std::string();
        }

        collection.AddSourceFile(filePath);
        return FileSystem::LoadText(filePath);
    }

    //查找OBJ引用的MTL文件
    List<std::string> FindMtlFiles(const std::string& objPath)
    {
        List<std::string> files;
        std::ifstream input(objPath);
        std::string directory = Path::GetDirectory(objPath);
        std::string line;
        while (std::getline(input, line))
        {
            if (!StartsWith(line, "mtllib ")) continue;

            std::string rest = line.substr(7);
            for (const std::string& fileName : SplitWhitespace(rest))
            {
                std::filesystem::path path = std::filesystem::path(directory) / fileName;
                files.push_back(NormalizePath(path.string()));
            }
        }

        std::string defaultMtl = NormalizePath(Path::GetFullNameWithOutExtension(objPath) + ".mtl");
        if (files.empty() && FileSystem::Exist(defaultMtl))
        {
            files.push_back(defaultMtl);
        }

        return files;
    }

    //解析MTL材质库
    std::unordered_map<std::string, MaterialImportInfo> ParseMtlFiles(const std::string& sourceKey, const List<std::string>& mtlFiles, AssetCollection& collection)
    {
        std::unordered_map<std::string, MaterialImportInfo> materials;

        for (const std::string& mtlPath : mtlFiles)
        {
            collection.AddSourceFile(mtlPath);

            std::ifstream input(mtlPath);
            if (!input)
            {
                collection.AddWarning("MTL file failed to open: " + mtlPath);
                continue;
            }

            std::string directory = Path::GetDirectory(mtlPath);
            Material* currentMaterial = nullptr;
            std::string currentMaterialKey;
            std::string currentMaterialName;
            std::string line;
            while (std::getline(input, line))
            {
                if (line.empty() || line[0] == '#') continue;

                if (StartsWith(line, "newmtl "))
                {
                    currentMaterialName = line.substr(7);
                    std::string keyName = SanitizeKeyName(currentMaterialName, "Material");
                    currentMaterialKey = sourceKey + "//Material/" + keyName;
                    currentMaterial = CreateImportedObject<Material>(currentMaterialKey);
                    if (!currentMaterial)
                    {
                        collection.AddError("Failed to create Material: " + currentMaterialKey);
                        continue;
                    }

                    currentMaterial->name = currentMaterialName;
                    materials[currentMaterialName] = { currentMaterial, currentMaterialKey };
                    collection.AddObject(currentMaterialKey, currentMaterial);
                    continue;
                }

                if (!currentMaterial) continue;

                std::stringstream stream(line);
                std::string command;
                stream >> command;

                if (command == "Ka" || command == "Kd" || command == "Ks" || command == "Ke")
                {
                    vector3 value;
                    stream >> value.x >> value.y >> value.z;
                    if (command == "Ka") currentMaterial->ambient = value;
                    if (command == "Kd") currentMaterial->diffuse = value;
                    if (command == "Ks") currentMaterial->specular = value;
                    if (command == "Ke") currentMaterial->emission = value;
                }
                else if (command == "Ns")
                {
                    stream >> currentMaterial->shininess;
                }
                else if (command == "shader")
                {
                    std::string shaderKey;
                    stream >> shaderKey;
                    if (shaderKey.empty()) continue;

                    AssetPipeline::ImportSource(shaderKey);
                    currentMaterial->shader.SetInstanceId(StringId(shaderKey));
                    ResourceManager::RegisterDependency(currentMaterialKey, shaderKey);
                }
                else if (command == "map_Kd" || command == "map_Bump" || command == "bump")
                {
                    std::string textureFile;
                    stream >> textureFile;
                    if (textureFile.empty()) continue;

                    std::filesystem::path texturePath = std::filesystem::path(directory) / textureFile;
                    std::string textureSuffix = command == "map_Kd" ? "_Diffuse" : "_Bump";
                    std::string textureKey = sourceKey + "//Texture/" + SanitizeKeyName(currentMaterialName + textureSuffix, "Texture");
                    Texture2D* texture = ImportImageAsKey(texturePath.string(), textureKey, collection);
                    if (!texture) continue;

                    if (command == "map_Kd")
                    {
                        currentMaterial->textureDiffuse.SetInstanceId(StringId(textureKey));
                        currentMaterial->hasDiffuseTexture = true;
                    }
                    else
                    {
                        currentMaterial->textureBump.SetInstanceId(StringId(textureKey));
                        currentMaterial->hasBumpTexture = true;
                    }

                    ResourceManager::RegisterDependency(currentMaterialKey, textureKey);
                }
            }
        }

        return materials;
    }

    //解析OBJ角点
    ObjCorner ParseCorner(const std::string& text, const List<vector3>& positions, const List<vector2>& texcoords, const List<vector3>& normals)
    {
        ObjCorner corner;
        std::stringstream stream(text);
        std::string part;
        List<std::string> parts;
        while (std::getline(stream, part, '/'))
        {
            parts.push_back(part);
        }

        int32 rawPosition = parts.size() > 0 && !parts[0].empty() ? std::stoi(parts[0]) : 0;
        int32 rawTexcoord = parts.size() > 1 && !parts[1].empty() ? std::stoi(parts[1]) : 0;
        int32 rawNormal = parts.size() > 2 && !parts[2].empty() ? std::stoi(parts[2]) : 0;

        corner.key.position = ResolveObjIndex(rawPosition, positions.size());
        corner.key.texcoord = ResolveObjIndex(rawTexcoord, texcoords.size());
        corner.key.normal = ResolveObjIndex(rawNormal, normals.size());

        if (corner.key.position >= 0 && static_cast<usize>(corner.key.position) < positions.size())
        {
            corner.position = positions[corner.key.position];
        }

        if (corner.key.texcoord >= 0 && static_cast<usize>(corner.key.texcoord) < texcoords.size())
        {
            corner.texcoord = texcoords[corner.key.texcoord];
        }

        if (corner.key.normal >= 0 && static_cast<usize>(corner.key.normal) < normals.size())
        {
            corner.normal = normals[corner.key.normal];
        }
        else
        {
            corner.normal = { 0.0f, 1.0f, 0.0f };
        }

        return corner;
    }
}

//判断导入是否成功
bool AssetCollection::Succeeded() const
{
    return errors.empty();
}

//记录导入源文件
void AssetCollection::AddSourceFile(const std::string& path)
{
    std::string normalizedPath = ResourceManager::NormalizeKey(path);
    if (std::find(sourceFiles.begin(), sourceFiles.end(), normalizedPath) != sourceFiles.end()) return;

    sourceFiles.push_back(normalizedPath);
}

//记录导入对象
void AssetCollection::AddObject(const std::string& key, Object* object, bool isMain)
{
    std::string normalizedKey = ResourceManager::NormalizeKey(key);
    if (std::find(objectKeys.begin(), objectKeys.end(), normalizedKey) == objectKeys.end())
    {
        objectKeys.push_back(normalizedKey);
        objects.push_back(object);
    }

    if (isMain && std::find(mainKeys.begin(), mainKeys.end(), normalizedKey) == mainKeys.end())
    {
        mainKeys.push_back(normalizedKey);
    }
}

//记录警告
void AssetCollection::AddWarning(const std::string& warning)
{
    warnings.push_back(warning);
    Log::Warning(warning.c_str());
}

//记录错误
void AssetCollection::AddError(const std::string& error)
{
    errors.push_back(error);
    Log::Error(error.c_str());
}

//按主文件路径选择导入器
AssetCollection AssetPipeline::ImportSource(std::string path)
{
    std::string sourceKey = ResourceManager::GetSourceKey(path);
    std::string extension = GetLowerExtension(sourceKey);

    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp")
    {
        return Import_IMG(sourceKey);
    }

    if (extension == ".obj")
    {
        return Import_OBJ(sourceKey);
    }

    if (FileSystem::Exist(ResolveAssetPath(sourceKey + ".vert.glsl")) && FileSystem::Exist(ResolveAssetPath(sourceKey + ".frag.glsl")))
    {
        return Import_GLSL(sourceKey);
    }

    AssetCollection collection;
    collection.sourceKey = sourceKey;
    collection.AddError("Unsupported asset source: " + sourceKey);
    return collection;
}

//导入GLSL着色器源码对
AssetCollection AssetPipeline::Import_GLSL(std::string path)
{
    AssetCollection collection;
    std::string sourceKey = ResourceManager::NormalizeKey(path);
    collection.sourceKey = sourceKey;

    std::string vertexPath = sourceKey + ".vert.glsl";
    std::string fragmentPath = sourceKey + ".frag.glsl";
    std::string vertexSource = LoadTextOrError(vertexPath, collection);
    std::string fragmentSource = LoadTextOrError(fragmentPath, collection);
    if (!collection.Succeeded()) return collection;

    MaterialShader* shader = CreateImportedObject<MaterialShader>(sourceKey);
    if (!shader)
    {
        collection.AddError("Failed to create MaterialShader: " + sourceKey);
        return collection;
    }

    shader->name = Path::GetName(sourceKey);
    shader->vertexPath = vertexPath;
    shader->fragmentPath = fragmentPath;
    shader->vertexSource = vertexSource;
    shader->fragmentSource = fragmentSource;
    collection.AddObject(sourceKey, shader, true);
    return collection;
}

//导入图片为CPU纹理
AssetCollection AssetPipeline::Import_IMG(std::string path)
{
    AssetCollection collection;
    std::string sourceKey = ResourceManager::NormalizeKey(path);
    collection.sourceKey = sourceKey;
    ImportImageAsKey(sourceKey, sourceKey, collection);
    return collection;
}

//导入OBJ为复合资源
AssetCollection AssetPipeline::Import_OBJ(std::string path)
{
    AssetCollection collection;
    std::string sourceKey = ResourceManager::NormalizeKey(path);
    collection.sourceKey = sourceKey;
    std::string objPath = ResolveAssetPath(sourceKey);
    collection.AddSourceFile(objPath);

    if (!FileSystem::Exist(objPath))
    {
        collection.AddError("OBJ file does not exist: " + sourceKey);
        return collection;
    }

    List<std::string> mtlFiles = FindMtlFiles(objPath);
    std::unordered_map<std::string, MaterialImportInfo> materials = ParseMtlFiles(sourceKey, mtlFiles, collection);

    std::string meshKey = sourceKey + "//Mesh/Main";
    Mesh* mesh = CreateImportedObject<Mesh>(meshKey);
    if (!mesh)
    {
        collection.AddError("Failed to create Mesh: " + meshKey);
        return collection;
    }

    mesh->name = Path::GetNameWithOutExtension(sourceKey);
    mesh->vertices.clear();
    mesh->texcoords.clear();
    mesh->normals.clear();
    mesh->tangents.clear();
    mesh->indices.clear();
    mesh->subMeshes.clear();

    List<vector3> positions;
    List<vector2> texcoords;
    List<vector3> normals;
    std::unordered_map<VertexKey, uint32, VertexKeyHash> vertexMap;
    SubMesh* currentSubMesh = nullptr;
    std::string currentMaterialName;

    auto ensureSubMesh = [&]() -> SubMesh*
        {
            if (currentSubMesh) return currentSubMesh;

            SubMesh subMesh;
            subMesh.name = currentMaterialName.empty() ? "Default" : currentMaterialName;
            subMesh.indexStart = static_cast<uint32>(mesh->indices.size());

            auto materialIt = materials.find(currentMaterialName);
            if (materialIt != materials.end())
            {
                subMesh.material.SetInstanceId(StringId(materialIt->second.key));
                ResourceManager::RegisterDependency(meshKey, materialIt->second.key);
            }

            mesh->subMeshes.push_back(subMesh);
            currentSubMesh = &mesh->subMeshes.back();
            return currentSubMesh;
        };

    auto beginSubMesh = [&](const std::string& materialName)
        {
            if (currentSubMesh && currentSubMesh->indexCount == 0)
            {
                mesh->subMeshes.pop_back();
            }

            currentSubMesh = nullptr;
            currentMaterialName = materialName;
            ensureSubMesh();
        };

    auto addVertex = [&](ObjCorner& corner) -> uint32
        {
            auto it = vertexMap.find(corner.key);
            if (it != vertexMap.end())
            {
                corner.index = it->second;
                return it->second;
            }

            uint32 index = static_cast<uint32>(mesh->vertices.size());
            vertexMap[corner.key] = index;
            mesh->vertices.push_back(corner.position);
            mesh->texcoords.push_back(corner.texcoord);
            mesh->normals.push_back(corner.normal);
            mesh->tangents.push_back({});
            corner.index = index;
            return index;
        };

    auto addTriangle = [&](ObjCorner a, ObjCorner b, ObjCorner c)
        {
            SubMesh* subMesh = ensureSubMesh();
            uint32 ia = addVertex(a);
            uint32 ib = addVertex(b);
            uint32 ic = addVertex(c);

            mesh->indices.push_back(ia);
            mesh->indices.push_back(ib);
            mesh->indices.push_back(ic);
            subMesh->indexCount += 3;

            vector3 tangent = ComputeTangent(a.position, b.position, c.position, a.texcoord, b.texcoord, c.texcoord);
            AddTo(mesh->tangents[ia], tangent);
            AddTo(mesh->tangents[ib], tangent);
            AddTo(mesh->tangents[ic], tangent);
        };

    std::ifstream input(objPath);
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '#') continue;

        if (StartsWith(line, "v "))
        {
            std::stringstream stream(line.substr(2));
            vector3 value;
            stream >> value.x >> value.y >> value.z;
            positions.push_back(value);
        }
        else if (StartsWith(line, "vt "))
        {
            std::stringstream stream(line.substr(3));
            vector2 value;
            stream >> value.x >> value.y;
            texcoords.push_back(value);
        }
        else if (StartsWith(line, "vn "))
        {
            std::stringstream stream(line.substr(3));
            vector3 value;
            stream >> value.x >> value.y >> value.z;
            normals.push_back(value);
        }
        else if (StartsWith(line, "usemtl "))
        {
            beginSubMesh(line.substr(7));
        }
        else if (StartsWith(line, "f "))
        {
            List<std::string> faceParts = SplitWhitespace(line.substr(2));
            if (faceParts.size() < 3)
            {
                collection.AddWarning("OBJ face has fewer than 3 vertices: " + sourceKey);
                continue;
            }

            List<ObjCorner> corners;
            for (const std::string& part : faceParts)
            {
                ObjCorner corner = ParseCorner(part, positions, texcoords, normals);
                if (corner.key.position < 0)
                {
                    collection.AddWarning("OBJ face references an invalid position: " + sourceKey);
                    corners.clear();
                    break;
                }

                corners.push_back(corner);
            }

            for (usize index = 1; corners.size() >= 3 && index + 1 < corners.size(); ++index)
            {
                addTriangle(corners[0], corners[index], corners[index + 1]);
            }
        }
    }

    for (vector3& tangent : mesh->tangents)
    {
        tangent = Normalize(tangent);
    }

    if (mesh->indices.empty())
    {
        collection.AddWarning("OBJ imported without triangles: " + sourceKey);
    }

    collection.AddObject(meshKey, mesh, true);
    return collection;
}

//导入OBJ为非索引Mesh
AssetCollection AssetPipeline::Import_AsIndexlessMesh_OBJ(std::string path)
{
    AssetCollection collection = Import_OBJ(path);
    if (!collection.Succeeded()) return collection;

    for (Object* object : collection.objects)
    {
        Mesh* mesh = object ? object->Cast<Mesh>() : nullptr;
        if (!mesh) continue;

        List<vector3> vertices;
        List<vector2> texcoords;
        List<vector3> normals;
        List<vector3> tangents;
        vertices.reserve(mesh->indices.size());
        texcoords.reserve(mesh->indices.size());
        normals.reserve(mesh->indices.size());
        tangents.reserve(mesh->indices.size());

        for (uint32 index : mesh->indices)
        {
            vertices.push_back(mesh->vertices[index]);
            texcoords.push_back(mesh->texcoords[index]);
            normals.push_back(mesh->normals[index]);
            tangents.push_back(mesh->tangents[index]);
        }

        mesh->vertices = std::move(vertices);
        mesh->texcoords = std::move(texcoords);
        mesh->normals = std::move(normals);
        mesh->tangents = std::move(tangents);
        mesh->indices.clear();
        mesh->subMeshes.clear();

        SubMesh subMesh;
        subMesh.name = "Main";
        subMesh.indexStart = 0;
        subMesh.indexCount = static_cast<uint32>(mesh->vertices.size());
        mesh->subMeshes.push_back(subMesh);
        break;
    }

    return collection;
}
