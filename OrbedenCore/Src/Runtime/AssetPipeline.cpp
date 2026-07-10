#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <unordered_map>

#include "FileSystem/FileSystem.h"
#include "FileSystem/PathDefines.h"
#include "Log/Log.h"
#include "Runtime/AssetPipeline.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Texture2D.h"

#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"

class AssetPipelineObjectFactory
{
public:
    static Object* Create(Type* type, const std::string& instancePath)
    {
        return Object::CreateResourceInstance(type, instancePath);
    }
};

namespace
{
    constexpr const char* MaterialDiffuseTextureSlot = "u_DiffuseTexture";
    constexpr const char* MaterialNormalTextureSlot = "u_NormalTexture";
    constexpr const char* MaterialDiffuseColorSlot = "u_DiffuseColor";
    constexpr const char* MaterialSpecularColorSlot = "u_SpecularColor";
    constexpr const char* MaterialEmissionColorSlot = "u_EmissionColor";
    constexpr const char* MaterialShininessSlot = "u_Shininess";

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

    //读取文本文件，失败时记录错误
    std::string LoadTextOrError(const std::string& path, AssetCollection& collection);

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

    //整理文件路径。
    std::string ToCleanPath(const std::string& path)
    {
        return ResourceManager::ToResourceKey(std::filesystem::path(path).lexically_normal().string());
    }

    //解析实际磁盘路径，保持资源 Key 不变但由当前项目决定来源。
    std::string GetAssetFilePath(const std::string& path)
    {
        std::string cleanPath = ToCleanPath(path);
        if (FileSystem::Exist(cleanPath)) return cleanPath;

        if (PathDefines::HasProjectRoot())
        {
            std::string resourceRoot = PathDefines::GetResourceRoot();
            if (cleanPath == resourceRoot || StartsWith(cleanPath, resourceRoot + "/"))
            {
                std::string resourceCandidate = PathDefines::GetResourceFilePath(cleanPath);
                if (FileSystem::Exist(resourceCandidate)) return resourceCandidate;
            }
        }

        return cleanPath;
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

    //裁剪字符串两端空白
    std::string Trim(const std::string& text)
    {
        usize begin = 0;
        while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
        {
            begin++;
        }

        usize end = text.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        {
            end--;
        }

        return text.substr(begin, end - begin);
    }

    //判断是否为OrbShader分段头
    bool ParseOrbShaderStageHeader(const std::string& line, std::string& stage)
    {
        stage.clear();
        usize dashCount = 0;
        while (dashCount < line.size() && line[dashCount] == '-')
        {
            dashCount++;
        }

        if (dashCount < 6) return false;

        std::string tail = Trim(line.substr(dashCount));
        std::stringstream stream(tail);
        stream >> stage;
        stage = ToLower(stage);
        return true;
    }

    //判断是否为当前后端支持的OrbShader分段
    bool IsSupportedOrbShaderStage(const std::string& stage)
    {
        return stage == "vert" || stage == "frag";
    }

    //判断是否为未来预留的OrbShader分段
    bool IsReservedOrbShaderStage(const std::string& stage)
    {
        return stage == "tesc"
            || stage == "tese"
            || stage == "geom"
            || stage == "comp"
            || stage == "raygen"
            || stage == "miss"
            || stage == "closesthit"
            || stage == "anyhit"
            || stage == "intersection"
            || stage == "callable";
    }

    //判断是否为合法的 include 指令
    bool ParseOrbShaderIncludeLine(const std::string& line, std::string& includePath)
    {
        includePath.clear();

        std::string trimmed = Trim(line);
        if (!StartsWith(trimmed, "#include")) return false;

        std::string rest = Trim(trimmed.substr(8));
        if (rest.size() < 2 || rest.front() != '"' || rest.back() != '"')
        {
            return true;
        }

        includePath = rest.substr(1, rest.size() - 2);
        return true;
    }

    //按 OrbShader 规则解析 include 资源路径
    std::string GetOrbShaderIncludeKey(const std::string& sourceKey, const std::string& path)
    {
        std::string includeKey = ResourceManager::ToResourceKey(path);
        if (StartsWith(includeKey, "Resource/")) return includeKey;

        std::filesystem::path parent = std::filesystem::path(sourceKey).parent_path();
        return ToCleanPath((parent / includeKey).string());
    }

    //从 include 文本中提取当前分段可见的源码
    bool ExtractOrbShaderIncludeSource(const std::string& sourceKey, const std::string& source, const std::string& stage, AssetCollection& collection, std::string& stageSource)
    {
        stageSource.clear();

        bool hasStageHeader = false;
        std::string sharedSource;
        std::string selectedSource;
        std::string* currentSource = &sharedSource;
        std::stringstream input(source);
        std::string line;
        uint32 lineNumber = 0;
        while (std::getline(input, line))
        {
            lineNumber++;

            std::string lineStage;
            if (ParseOrbShaderStageHeader(line, lineStage))
            {
                hasStageHeader = true;
                currentSource = nullptr;
                if (lineStage.empty())
                {
                    collection.AddError("OrbShader include stage header is missing a stage name: " + sourceKey + ":" + std::to_string(lineNumber));
                    return false;
                }

                if (IsSupportedOrbShaderStage(lineStage))
                {
                    currentSource = lineStage == stage ? &selectedSource : nullptr;
                    continue;
                }

                if (IsReservedOrbShaderStage(lineStage))
                {
                    collection.AddError("OrbShader include stage is reserved but not supported by the current backend: " + lineStage + " in " + sourceKey + ":" + std::to_string(lineNumber));
                }
                else
                {
                    collection.AddError("OrbShader include stage is unknown: " + lineStage + " in " + sourceKey + ":" + std::to_string(lineNumber));
                }

                return false;
            }

            if (currentSource)
            {
                *currentSource += line;
                *currentSource += '\n';
            }
        }

        if (!hasStageHeader)
        {
            stageSource = source;
            return true;
        }

        stageSource = sharedSource + selectedSource;
        return true;
    }

    //展开 OrbShader 的 include 片段
    bool ExpandOrbShaderIncludes(const std::string& sourceKey, const std::string& stage, const std::string& source, AssetCollection& collection, List<std::string>& includeStack, std::string& expandedSource)
    {
        expandedSource.clear();
        includeStack.push_back(sourceKey);

        std::stringstream input(source);
        std::string line;
        uint32 lineNumber = 0;
        while (std::getline(input, line))
        {
            lineNumber++;

            std::string includePath;
            if (!ParseOrbShaderIncludeLine(line, includePath))
            {
                expandedSource += line;
                expandedSource += '\n';
                continue;
            }

            if (includePath.empty() || includePath.front() == '"' || includePath.back() == '"')
            {
                collection.AddError("OrbShader include syntax is invalid: " + sourceKey + ":" + std::to_string(lineNumber));
                includeStack.pop_back();
                return false;
            }

            //解析 include 目标
            std::string includeKey = GetOrbShaderIncludeKey(sourceKey, includePath);
            if (std::find(includeStack.begin(), includeStack.end(), includeKey) != includeStack.end())
            {
                collection.AddError("OrbShader include cycle detected: " + sourceKey + " -> " + includeKey);
                includeStack.pop_back();
                return false;
            }

            //读取并筛选当前分段可见源码
            std::string includeText = LoadTextOrError(includeKey, collection);
            if (!collection.Succeeded())
            {
                includeStack.pop_back();
                return false;
            }

            std::string includeSource;
            if (!ExtractOrbShaderIncludeSource(includeKey, includeText, stage, collection, includeSource))
            {
                includeStack.pop_back();
                return false;
            }

            //递归展开子 include
            std::string expandedInclude;
            if (!ExpandOrbShaderIncludes(includeKey, stage, includeSource, collection, includeStack, expandedInclude))
            {
                includeStack.pop_back();
                return false;
            }

            expandedSource += expandedInclude;
            if (!expandedInclude.empty() && expandedInclude.back() != '\n')
            {
                expandedSource += '\n';
            }
        }

        includeStack.pop_back();
        return true;
    }

    //解析OrbShader单文件，拆出当前后端支持的GLSL源码
    bool ParseOrbShaderSource(const std::string& sourceKey, const std::string& source, AssetCollection& collection, std::string& vertexSource, std::string& fragmentSource)
    {
        vertexSource.clear();
        fragmentSource.clear();

        std::string* currentSource = nullptr;
        bool ignoringInvalidStage = false;
        bool hasVertex = false;
        bool hasFragment = false;
        std::stringstream input(source);
        std::string line;
        uint32 lineNumber = 0;
        while (std::getline(input, line))
        {
            lineNumber++;

            std::string stage;
            if (ParseOrbShaderStageHeader(line, stage))
            {
                currentSource = nullptr;
                ignoringInvalidStage = false;
                if (stage.empty())
                {
                    collection.AddError("OrbShader stage header is missing a stage name: " + sourceKey + ":" + std::to_string(lineNumber));
                    ignoringInvalidStage = true;
                    continue;
                }

                if (IsSupportedOrbShaderStage(stage) && stage == "vert")
                {
                    if (hasVertex)
                    {
                        collection.AddError("OrbShader duplicate vert stage: " + sourceKey + ":" + std::to_string(lineNumber));
                        ignoringInvalidStage = true;
                        continue;
                    }

                    hasVertex = true;
                    currentSource = &vertexSource;
                    continue;
                }

                if (IsSupportedOrbShaderStage(stage) && stage == "frag")
                {
                    if (hasFragment)
                    {
                        collection.AddError("OrbShader duplicate frag stage: " + sourceKey + ":" + std::to_string(lineNumber));
                        ignoringInvalidStage = true;
                        continue;
                    }

                    hasFragment = true;
                    currentSource = &fragmentSource;
                    continue;
                }

                if (IsReservedOrbShaderStage(stage))
                {
                    collection.AddError("OrbShader stage is reserved but not supported by the current backend: " + stage + " in " + sourceKey + ":" + std::to_string(lineNumber));
                }
                else
                {
                    collection.AddError("OrbShader unknown stage: " + stage + " in " + sourceKey + ":" + std::to_string(lineNumber));
                }

                ignoringInvalidStage = true;
                continue;
            }

            if (currentSource)
            {
                *currentSource += line;
                *currentSource += '\n';
            }
            else if (!ignoringInvalidStage && !Trim(line).empty())
            {
                collection.AddError("OrbShader content appears before a stage header: " + sourceKey + ":" + std::to_string(lineNumber));
            }
        }

        if (!hasVertex)
        {
            collection.AddError("OrbShader is missing vert stage: " + sourceKey);
        }

        if (!hasFragment)
        {
            collection.AddError("OrbShader is missing frag stage: " + sourceKey);
        }

        return collection.Succeeded();
    }

    //解析OBJ索引，支持负索引
    int32 ConvertObjIndex(int32 rawIndex, usize count)
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
        std::string resourceKey = ResourceManager::ToResourceKey(key);
        Object* loaded = ResourceManager::FindLoaded(resourceKey);
        if (loaded) return loaded->Cast<T>();

        Object* found = Object::FindObject(StringId(resourceKey));
        T* existing = found ? found->Cast<T>() : nullptr;
        if (existing)
        {
            return ResourceManager::RegisterObject(resourceKey, existing) ? existing : nullptr;
        }

        Object* created = AssetPipelineObjectFactory::Create(T::StaticType(), resourceKey);
        T* object = created ? created->Cast<T>() : nullptr;
        if (!object) return nullptr;

        if (!ResourceManager::RegisterObject(resourceKey, object))
        {
            Object::DeleteInstance(object);
            return nullptr;
        }

        return object;
    }

    //导入图片到指定ObjectKey
    Texture2D* ImportImageAsKey(const std::string& filePath, const std::string& objectKey, AssetCollection& collection)
    {
        std::string imageFilePath = GetAssetFilePath(filePath);
        std::string textureKey = ResourceManager::ToResourceKey(objectKey);
        collection.AddSourceFile(imageFilePath);

        Texture2D* texture = CreateImportedObject<Texture2D>(textureKey);
        if (!texture)
        {
            collection.AddError("Failed to create Texture2D: " + textureKey);
            return nullptr;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(imageFilePath.c_str(), &width, &height, &channels, 4);
        if (!pixels)
        {
            collection.AddError("Image decode failed: " + imageFilePath);
            return nullptr;
        }

        texture->name = Path::GetNameWithOutExtension(imageFilePath);
        texture->width = width;
        texture->height = height;
        texture->channels = 4;
        texture->format = 4;
        texture->pixels.assign(pixels, pixels + static_cast<usize>(width) * static_cast<usize>(height) * 4);
        stbi_image_free(pixels);

        collection.AddObject(textureKey, texture, true);
        return texture;
    }

    //读取文本文件，失败时记录错误
    std::string LoadTextOrError(const std::string& path, AssetCollection& collection)
    {
        std::string filePath = GetAssetFilePath(path);
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
                files.push_back(ToCleanPath(path.string()));
            }
        }

        std::string defaultMtl = ToCleanPath(Path::GetFullNameWithOutExtension(objPath) + ".mtl");
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

                if (command == "Kd" || command == "Ks" || command == "Ke")
                {
                    color value = { 0.0f, 0.0f, 0.0f, 1.0f };
                    stream >> value.r >> value.g >> value.b;
                    if (command == "Kd") currentMaterial->SetColor(MaterialDiffuseColorSlot, value);
                    if (command == "Ks") currentMaterial->SetColor(MaterialSpecularColorSlot, value);
                    if (command == "Ke") currentMaterial->SetColor(MaterialEmissionColorSlot, value);
                }
                else if (command == "Ns")
                {
                    float32 shininess = 0.0f;
                    stream >> shininess;
                    currentMaterial->SetFloat(MaterialShininessSlot, shininess);
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
                        currentMaterial->SetTexture(MaterialDiffuseTextureSlot, texture);
                    }
                    else
                    {
                        currentMaterial->SetTexture(MaterialNormalTextureSlot, texture);
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

        corner.key.position = ConvertObjIndex(rawPosition, positions.size());
        corner.key.texcoord = ConvertObjIndex(rawTexcoord, texcoords.size());
        corner.key.normal = ConvertObjIndex(rawNormal, normals.size());

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
    std::string resourceKey = ResourceManager::ToResourceKey(path);
    if (std::find(sourceFiles.begin(), sourceFiles.end(), resourceKey) != sourceFiles.end()) return;

    sourceFiles.push_back(resourceKey);
}

//记录导入对象
void AssetCollection::AddObject(const std::string& key, Object* object, bool isMain)
{
    std::string objectKey = ResourceManager::ToResourceKey(key);
    if (std::find(objectKeys.begin(), objectKeys.end(), objectKey) == objectKeys.end())
    {
        objectKeys.push_back(objectKey);
        objects.push_back(object);
    }

    if (isMain && std::find(mainKeys.begin(), mainKeys.end(), objectKey) == mainKeys.end())
    {
        mainKeys.push_back(objectKey);
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

    if (extension == ".orbshader")
    {
        return Import_ORBSHADER(sourceKey);
    }

    if (FileSystem::Exist(GetAssetFilePath(sourceKey + ".vert.glsl")) && FileSystem::Exist(GetAssetFilePath(sourceKey + ".frag.glsl")))
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
    std::string sourceKey = ResourceManager::ToResourceKey(path);
    collection.sourceKey = sourceKey;

    std::string vertexPath = sourceKey + ".vert.glsl";
    std::string fragmentPath = sourceKey + ".frag.glsl";
    std::string vertexSource = LoadTextOrError(vertexPath, collection);
    std::string fragmentSource = LoadTextOrError(fragmentPath, collection);
    if (!collection.Succeeded()) return collection;

    Shader* shader = CreateImportedObject<Shader>(sourceKey);
    if (!shader)
    {
        collection.AddError("Failed to create Shader: " + sourceKey);
        return collection;
    }

    shader->name = Path::GetName(sourceKey);
    shader->vertexPath = vertexPath;
    shader->fragmentPath = fragmentPath;
    shader->vertexSource = vertexSource;
    shader->fragmentSource = fragmentSource;
    shader->ReflectSlotsFromSource();
    collection.AddObject(sourceKey, shader, true);
    return collection;
}

//导入单文件OrbShader
AssetCollection AssetPipeline::Import_ORBSHADER(std::string path)
{
    AssetCollection collection;
    std::string sourceKey = ResourceManager::ToResourceKey(path);
    collection.sourceKey = sourceKey;

    std::string source = LoadTextOrError(sourceKey, collection);
    if (!collection.Succeeded()) return collection;

    std::string vertexSource;
    std::string fragmentSource;
    if (!ParseOrbShaderSource(sourceKey, source, collection, vertexSource, fragmentSource))
    {
        return collection;
    }

    Shader* shader = CreateImportedObject<Shader>(sourceKey);
    if (!shader)
    {
        collection.AddError("Failed to create Shader: " + sourceKey);
        return collection;
    }

    shader->name = Path::GetNameWithOutExtension(sourceKey);
    shader->vertexPath = sourceKey;
    shader->fragmentPath = sourceKey;
    List<std::string> includeStack;
    if (!ExpandOrbShaderIncludes(sourceKey, "vert", vertexSource, collection, includeStack, shader->vertexSource))
    {
        return collection;
    }

    includeStack.clear();
    if (!ExpandOrbShaderIncludes(sourceKey, "frag", fragmentSource, collection, includeStack, shader->fragmentSource))
    {
        return collection;
    }

    shader->ReflectSlotsFromSource();
    collection.AddObject(sourceKey, shader, true);
    return collection;
}

//导入图片为CPU纹理
AssetCollection AssetPipeline::Import_IMG(std::string path)
{
    AssetCollection collection;
    std::string sourceKey = ResourceManager::ToResourceKey(path);
    collection.sourceKey = sourceKey;
    ImportImageAsKey(sourceKey, sourceKey, collection);
    return collection;
}

//导入OBJ为复合资源
AssetCollection AssetPipeline::Import_OBJ(std::string path)
{
    AssetCollection collection;
    std::string sourceKey = ResourceManager::ToResourceKey(path);
    collection.sourceKey = sourceKey;
    std::string objPath = GetAssetFilePath(sourceKey);
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

    mesh->TouchRevision();
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
        mesh->TouchRevision();
        break;
    }

    return collection;
}
