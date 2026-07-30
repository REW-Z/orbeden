#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>
#include <unordered_map>

#include "FileSystem/FileSystem.h"
#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"
#include "Runtime/AssetPipeline.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Texture2D.h"

#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

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

    struct GltfTextureImportInfo
    {
    public:
        Texture2D* texture = nullptr;
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
        return ResourceManager::ToResourceKey(Utf8Path::ToUtf8(Utf8Path::FromUtf8(path).lexically_normal()));
    }

    //解析实际磁盘路径，保持资源 Key 不变但由当前内容根目录决定来源。
    std::string GetAssetFilePath(const std::string& path)
    {
        std::string cleanPath = ToCleanPath(path);
        if (FileSystem::Exist(cleanPath)) return cleanPath;

        if (PathDefines::HasContentRoot())
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
        return ToLower(Utf8Path::ToUtf8(Utf8Path::FromUtf8(path).extension()));
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

    //解析 OrbShader 分段头及其剩余参数
    bool ParseOrbShaderHeader(const std::string& line, std::string& directive, std::string& argument)
    {
        directive.clear();
        argument.clear();
        usize dashCount = 0;
        while (dashCount < line.size() && line[dashCount] == '-')
        {
            dashCount++;
        }

        if (dashCount < 6) return false;

        std::string tail = Trim(line.substr(dashCount));
        std::stringstream stream(tail);
        stream >> directive;
        directive = ToLower(directive);
        std::getline(stream, argument);
        argument = Trim(argument);
        return true;
    }

    //判断是否为 OrbShader 分段头
    bool ParseOrbShaderStageHeader(const std::string& line, std::string& stage)
    {
        std::string argument;
        return ParseOrbShaderHeader(line, stage, argument);
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

        std::filesystem::path parent = Utf8Path::FromUtf8(sourceKey).parent_path();
        return Utf8Path::ToUtf8((parent / Utf8Path::FromUtf8(includeKey)).lexically_normal());
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

    //解析 Shader Pass 三态开关
    bool ParseShaderPassToggle(const std::string& text, ShaderPassToggle& value)
    {
        std::string normalized = ToLower(text);
        if (normalized == "auto") value = ShaderPassToggle::Auto;
        else if (normalized == "on") value = ShaderPassToggle::On;
        else if (normalized == "off") value = ShaderPassToggle::Off;
        else return false;
        return true;
    }

    //解析 Shader Pass 剔除模式
    bool ParseShaderCullMode(const std::string& text, CullMode& value)
    {
        std::string normalized = ToLower(text);
        if (normalized == "auto") value = CullMode::Auto;
        else if (normalized == "none") value = CullMode::None;
        else if (normalized == "front") value = CullMode::Front;
        else if (normalized == "back") value = CullMode::Back;
        else return false;
        return true;
    }

    //判断文本是否以 Pass 状态关键字开头
    bool IsShaderPassStateLine(const std::string& line)
    {
        std::stringstream stream(Trim(line));
        std::string key;
        std::string value;
        std::string extra;
        stream >> key >> value >> extra;
        if (value.empty() || !extra.empty()) return false;

        key = ToLower(key);
        return key == "depthtest" || key == "depthwrite" || key == "blend" || key == "cull";
    }

    //解析 OrbShader 单文件为有序 Pass
    bool ParseOrbShaderSource(const std::string& sourceKey, const std::string& source, AssetCollection& collection, List<ShaderPass>& passes)
    {
        passes.clear();

        ShaderPass* currentPass = nullptr;
        std::string* currentSource = nullptr;
        List<std::string> passNames;
        bool explicitPasses = false;
        bool legacyPass = false;
        bool stageStarted = false;
        bool hasVertex = false;
        bool hasFragment = false;

        auto validateCurrentPass = [&]()
        {
            if (!currentPass) return;
            if (!hasVertex) collection.AddError("OrbShader pass is missing vert stage: " + currentPass->name + " in " + sourceKey);
            if (!hasFragment) collection.AddError("OrbShader pass is missing frag stage: " + currentPass->name + " in " + sourceKey);
        };

        std::stringstream input(source);
        std::string line;
        uint32 lineNumber = 0;
        while (std::getline(input, line))
        {
            lineNumber++;

            std::string directive;
            std::string argument;
            if (ParseOrbShaderHeader(line, directive, argument))
            {
                currentSource = nullptr;
                if (directive.empty())
                {
                    collection.AddError("OrbShader stage header is missing a stage name: " + sourceKey + ":" + std::to_string(lineNumber));
                    continue;
                }

                if (directive == "pass")
                {
                    if (legacyPass)
                    {
                        collection.AddError("OrbShader cannot mix implicit and explicit passes: " + sourceKey + ":" + std::to_string(lineNumber));
                        continue;
                    }
                    if (argument.empty())
                    {
                        collection.AddError("OrbShader pass header is missing a name: " + sourceKey + ":" + std::to_string(lineNumber));
                        continue;
                    }
                    if (std::find(passNames.begin(), passNames.end(), argument) != passNames.end())
                    {
                        collection.AddError("OrbShader duplicate pass name: " + argument + " in " + sourceKey + ":" + std::to_string(lineNumber));
                        continue;
                    }

                    validateCurrentPass();
                    explicitPasses = true;
                    passNames.push_back(argument);
                    passes.push_back(ShaderPass());
                    currentPass = &passes.back();
                    currentPass->name = argument;
                    stageStarted = false;
                    hasVertex = false;
                    hasFragment = false;
                    continue;
                }

                if (IsSupportedOrbShaderStage(directive))
                {
                    if (!argument.empty())
                    {
                        collection.AddError("OrbShader stage header has unexpected content: " + sourceKey + ":" + std::to_string(lineNumber));
                        continue;
                    }
                    if (!currentPass)
                    {
                        if (explicitPasses)
                        {
                            collection.AddError("OrbShader stage appears before a pass header: " + sourceKey + ":" + std::to_string(lineNumber));
                            continue;
                        }

                        legacyPass = true;
                        passes.push_back(ShaderPass());
                        currentPass = &passes.back();
                    }

                    stageStarted = true;
                }

                if (directive == "vert")
                {
                    if (hasVertex)
                    {
                        collection.AddError("OrbShader duplicate vert stage: " + sourceKey + ":" + std::to_string(lineNumber));
                        continue;
                    }

                    hasVertex = true;
                    currentSource = &currentPass->vertexSource;
                    continue;
                }

                if (directive == "frag")
                {
                    if (hasFragment)
                    {
                        collection.AddError("OrbShader duplicate frag stage: " + sourceKey + ":" + std::to_string(lineNumber));
                        continue;
                    }

                    hasFragment = true;
                    currentSource = &currentPass->fragmentSource;
                    continue;
                }

                if (IsReservedOrbShaderStage(directive))
                {
                    collection.AddError("OrbShader stage is reserved but not supported by the current backend: " + directive + " in " + sourceKey + ":" + std::to_string(lineNumber));
                }
                else
                {
                    collection.AddError("OrbShader unknown stage: " + directive + " in " + sourceKey + ":" + std::to_string(lineNumber));
                }
                continue;
            }

            if (currentSource)
            {
                if (explicitPasses && IsShaderPassStateLine(line))
                {
                    collection.AddError("OrbShader pass state must appear before the first stage: " + sourceKey + ":" + std::to_string(lineNumber));
                    continue;
                }

                *currentSource += line;
                *currentSource += '\n';
            }
            else if (currentPass && explicitPasses && !stageStarted && !Trim(line).empty())
            {
                std::stringstream stateStream(Trim(line));
                std::string key;
                std::string value;
                std::string extra;
                stateStream >> key >> value >> extra;
                key = ToLower(key);
                bool valid = extra.empty() && !value.empty();
                if (key == "depthtest") valid &= ParseShaderPassToggle(value, currentPass->state.depthTest);
                else if (key == "depthwrite") valid &= ParseShaderPassToggle(value, currentPass->state.depthWrite);
                else if (key == "blend") valid &= ParseShaderPassToggle(value, currentPass->state.blend);
                else if (key == "cull") valid &= ParseShaderCullMode(value, currentPass->state.cull);
                else valid = false;

                if (!valid)
                {
                    collection.AddError("OrbShader pass state is invalid: " + sourceKey + ":" + std::to_string(lineNumber));
                }
            }
            else if (!Trim(line).empty())
            {
                collection.AddError("OrbShader content appears before a stage header: " + sourceKey + ":" + std::to_string(lineNumber));
            }
        }

        validateCurrentPass();
        if (passes.empty())
        {
            collection.AddError("OrbShader contains no passes: " + sourceKey);
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

    //转换cgltf错误码为日志文本
    const char* GetCgltfResultText(cgltf_result result)
    {
        switch (result)
        {
        case cgltf_result_success: return "success";
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid JSON";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_invalid_options: return "invalid options";
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "I/O error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "legacy glTF 1.x";
        default: return "unknown error";
        }
    }

    //解析glTF相对URI为磁盘路径
    std::string ResolveGltfFileUri(const std::string& sourcePath, const char* uri)
    {
        if (!uri || !uri[0] || StartsWith(uri, "data:") || std::string(uri).find("://") != std::string::npos)
        {
            return std::string();
        }

        std::string decodedUri = uri;
        decodedUri.resize(cgltf_decode_uri(decodedUri.data()));
        std::filesystem::path uriPath = Utf8Path::FromUtf8(decodedUri);
        if (uriPath.is_absolute()) return Utf8Path::ToUtf8(uriPath.lexically_normal());

        return Utf8Path::ToUtf8((Utf8Path::FromUtf8(sourcePath).parent_path() / uriPath).lexically_normal());
    }

    //解码图片data URI中的Base64内容
    bool DecodeBase64DataUri(const char* uri, List<uint8>& bytes)
    {
        bytes.clear();
        if (!uri || !StartsWith(uri, "data:")) return false;

        const char* comma = std::strchr(uri, ',');
        if (!comma) return false;

        std::string header(uri, comma);
        if (header.size() < 7 || header.compare(header.size() - 7, 7, ";base64") != 0) return false;

        uint32 accumulator = 0;
        int32 bitCount = 0;
        for (const char* cursor = comma + 1; *cursor; ++cursor)
        {
            unsigned char ch = static_cast<unsigned char>(*cursor);
            if (ch == '=') break;
            if (std::isspace(ch)) continue;

            int32 value = -1;
            if (ch >= 'A' && ch <= 'Z') value = ch - 'A';
            else if (ch >= 'a' && ch <= 'z') value = ch - 'a' + 26;
            else if (ch >= '0' && ch <= '9') value = ch - '0' + 52;
            else if (ch == '+') value = 62;
            else if (ch == '/') value = 63;
            if (value < 0) return false;

            accumulator = (accumulator << 6) | static_cast<uint32>(value);
            bitCount += 6;
            if (bitCount < 8) continue;

            bitCount -= 8;
            bytes.push_back(static_cast<uint8>((accumulator >> bitCount) & 0xffu));
            accumulator = bitCount == 0 ? 0u : accumulator & ((1u << bitCount) - 1u);
        }

        return !bytes.empty();
    }

    //从内存图片创建Texture2D资源
    Texture2D* ImportImageMemoryAsKey(const uint8* data, usize size, const std::string& imageName, const std::string& objectKey, AssetCollection& collection)
    {
        std::string textureKey = ResourceManager::ToResourceKey(objectKey);
        if (!data || size == 0 || size > static_cast<usize>(std::numeric_limits<int>::max()))
        {
            collection.AddError("Embedded glTF image data is invalid: " + textureKey);
            return nullptr;
        }

        Texture2D* texture = CreateImportedObject<Texture2D>(textureKey);
        if (!texture)
        {
            collection.AddError("Failed to create Texture2D: " + textureKey);
            return nullptr;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
        if (!pixels)
        {
            collection.AddError("Embedded glTF image decode failed: " + textureKey);
            return nullptr;
        }

        texture->name = imageName;
        texture->width = width;
        texture->height = height;
        texture->channels = 4;
        texture->format = 4;
        texture->pixels.assign(pixels, pixels + static_cast<usize>(width) * static_cast<usize>(height) * 4);
        stbi_image_free(pixels);

        collection.AddObject(textureKey, texture);
        return texture;
    }

    //按需导入glTF纹理，复用相同的image资源
    GltfTextureImportInfo GetOrImportGltfTexture(
        const cgltf_data* data,
        const cgltf_texture_view& view,
        const std::string& sourceKey,
        const std::string& sourcePath,
        AssetCollection& collection,
        std::unordered_map<const cgltf_image*, GltfTextureImportInfo>& importedTextures)
    {
        if (!view.texture) return {};

        const cgltf_image* image = view.texture->image;
        if (!image)
        {
            if (view.texture->has_basisu)
            {
                collection.AddWarning("glTF Basis Universal texture is not supported: " + sourceKey);
            }
            else if (view.texture->has_webp)
            {
                collection.AddWarning("glTF WebP texture is not supported by stb_image: " + sourceKey);
            }
            return {};
        }

        auto found = importedTextures.find(image);
        if (found != importedTextures.end()) return found->second;

        usize imageIndex = static_cast<usize>(cgltf_image_index(data, image));
        std::string fallbackName = "Texture_" + std::to_string(imageIndex);
        std::string imageName = image->name && image->name[0] ? image->name : fallbackName;
        std::string textureKey = sourceKey + "//Texture/" + std::to_string(imageIndex) + "_" + SanitizeKeyName(imageName, fallbackName);
        Texture2D* texture = nullptr;

        if (image->buffer_view)
        {
            const uint8* imageData = cgltf_buffer_view_data(image->buffer_view);
            texture = ImportImageMemoryAsKey(imageData, static_cast<usize>(image->buffer_view->size), imageName, textureKey, collection);
        }
        else if (image->uri && StartsWith(image->uri, "data:"))
        {
            List<uint8> imageBytes;
            if (!DecodeBase64DataUri(image->uri, imageBytes))
            {
                collection.AddError("Unsupported glTF image data URI: " + textureKey);
            }
            else
            {
                texture = ImportImageMemoryAsKey(imageBytes.data(), imageBytes.size(), imageName, textureKey, collection);
            }
        }
        else
        {
            std::string imagePath = ResolveGltfFileUri(sourcePath, image->uri);
            if (imagePath.empty())
            {
                collection.AddError("Unsupported glTF image URI: " + textureKey);
            }
            else
            {
                texture = ImportImageAsKey(imagePath, textureKey, collection);
                if (texture) texture->name = imageName;
            }
        }

        GltfTextureImportInfo result = { texture, textureKey };
        importedTextures[image] = result;
        return result;
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
        std::ifstream input(Utf8Path::FromUtf8(objPath));
        std::string directory = Path::GetDirectory(objPath);
        std::string line;
        while (std::getline(input, line))
        {
            if (!StartsWith(line, "mtllib ")) continue;

            std::string rest = line.substr(7);
            for (const std::string& fileName : SplitWhitespace(rest))
            {
                std::filesystem::path path = Utf8Path::FromUtf8(directory) / Utf8Path::FromUtf8(fileName);
                files.push_back(Utf8Path::ToUtf8(path.lexically_normal()));
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

            std::ifstream input(Utf8Path::FromUtf8(mtlPath));
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

                    std::filesystem::path texturePath = Utf8Path::FromUtf8(directory) / Utf8Path::FromUtf8(textureFile);
                    std::string textureSuffix = command == "map_Kd" ? "_Diffuse" : "_Bump";
                    std::string textureKey = sourceKey + "//Texture/" + SanitizeKeyName(currentMaterialName + textureSuffix, "Texture");
                    Texture2D* texture = ImportImageAsKey(Utf8Path::ToUtf8(texturePath), textureKey, collection);
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

    //导入一个glTF材质并映射到当前材质槽
    MaterialImportInfo ImportGltfMaterial(
        const cgltf_data* data,
        const cgltf_material* sourceMaterial,
        usize materialIndex,
        const std::string& sourceKey,
        const std::string& sourcePath,
        AssetCollection& collection,
        std::unordered_map<const cgltf_image*, GltfTextureImportInfo>& importedTextures)
    {
        std::string fallbackName = "Material_" + std::to_string(materialIndex);
        std::string materialName = sourceMaterial->name && sourceMaterial->name[0] ? sourceMaterial->name : fallbackName;
        std::string materialKey = sourceKey + "//Material/" + std::to_string(materialIndex) + "_" + SanitizeKeyName(materialName, fallbackName);
        Material* material = CreateImportedObject<Material>(materialKey);
        if (!material)
        {
            collection.AddError("Failed to create Material: " + materialKey);
            return {};
        }

        material->name = materialName;
        material->textureSlots.clear();
        material->colorSlots.clear();
        material->floatSlots.clear();

        color diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        color specularColor = { 0.04f, 0.04f, 0.04f, 1.0f };
        float32 shininess = 32.0f;
        const cgltf_texture_view* diffuseTexture = nullptr;

        if (sourceMaterial->has_pbr_specular_glossiness)
        {
            const cgltf_pbr_specular_glossiness& pbr = sourceMaterial->pbr_specular_glossiness;
            diffuseColor = { pbr.diffuse_factor[0], pbr.diffuse_factor[1], pbr.diffuse_factor[2], pbr.diffuse_factor[3] };
            specularColor = { pbr.specular_factor[0], pbr.specular_factor[1], pbr.specular_factor[2], 1.0f };
            shininess = std::max(1.0f, pbr.glossiness_factor * 128.0f);
            diffuseTexture = &pbr.diffuse_texture;
        }
        else if (sourceMaterial->has_pbr_metallic_roughness)
        {
            const cgltf_pbr_metallic_roughness& pbr = sourceMaterial->pbr_metallic_roughness;
            diffuseColor = { pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3] };

            float32 metallic = std::clamp(static_cast<float32>(pbr.metallic_factor), 0.0f, 1.0f);
            specularColor.r = 0.04f * (1.0f - metallic) + diffuseColor.r * metallic;
            specularColor.g = 0.04f * (1.0f - metallic) + diffuseColor.g * metallic;
            specularColor.b = 0.04f * (1.0f - metallic) + diffuseColor.b * metallic;
            float32 roughness = std::clamp(static_cast<float32>(pbr.roughness_factor), 0.0f, 1.0f);
            shininess = std::max(1.0f, (1.0f - roughness) * 128.0f);
            diffuseTexture = &pbr.base_color_texture;
        }

        float32 emissionStrength = sourceMaterial->has_emissive_strength
            ? static_cast<float32>(sourceMaterial->emissive_strength.emissive_strength)
            : 1.0f;
        color emissionColor =
        {
            sourceMaterial->emissive_factor[0] * emissionStrength,
            sourceMaterial->emissive_factor[1] * emissionStrength,
            sourceMaterial->emissive_factor[2] * emissionStrength,
            1.0f,
        };

        material->SetColor(MaterialDiffuseColorSlot, diffuseColor);
        material->SetColor(MaterialSpecularColorSlot, specularColor);
        material->SetColor(MaterialEmissionColorSlot, emissionColor);
        material->SetFloat(MaterialShininessSlot, shininess);
        collection.AddObject(materialKey, material);

        if (diffuseTexture)
        {
            GltfTextureImportInfo texture = GetOrImportGltfTexture(data, *diffuseTexture, sourceKey, sourcePath, collection, importedTextures);
            if (texture.texture)
            {
                material->SetTexture(MaterialDiffuseTextureSlot, texture.texture);
                ResourceManager::RegisterDependency(materialKey, texture.key);
            }
        }

        GltfTextureImportInfo normalTexture = GetOrImportGltfTexture(data, sourceMaterial->normal_texture, sourceKey, sourcePath, collection, importedTextures);
        if (normalTexture.texture)
        {
            material->SetTexture(MaterialNormalTextureSlot, normalTexture.texture);
            ResourceManager::RegisterDependency(materialKey, normalTexture.key);
        }

        return { material, materialKey };
    }

    //创建无显式材质primitive使用的默认材质
    MaterialImportInfo CreateDefaultGltfMaterial(const std::string& sourceKey, AssetCollection& collection)
    {
        std::string materialKey = sourceKey + "//Material/Default";
        Material* material = CreateImportedObject<Material>(materialKey);
        if (!material)
        {
            collection.AddError("Failed to create Material: " + materialKey);
            return {};
        }

        material->name = "Default";
        material->textureSlots.clear();
        material->colorSlots.clear();
        material->floatSlots.clear();
        material->SetColor(MaterialDiffuseColorSlot, { 1.0f, 1.0f, 1.0f, 1.0f });
        material->SetColor(MaterialSpecularColorSlot, { 0.04f, 0.04f, 0.04f, 1.0f });
        material->SetColor(MaterialEmissionColorSlot, { 0.0f, 0.0f, 0.0f, 1.0f });
        material->SetFloat(MaterialShininessSlot, 32.0f);
        collection.AddObject(materialKey, material);
        return { material, materialKey };
    }

    //把glTF primitive追加到引擎Mesh
    bool AppendGltfPrimitive(
        const cgltf_primitive& primitive,
        Mesh& mesh,
        const std::string& meshKey,
        const std::unordered_map<const cgltf_material*, MaterialImportInfo>& materials,
        const MaterialImportInfo& defaultMaterial,
        AssetCollection& collection,
        bool& needsNormals,
        bool& needsTangents)
    {
        if (primitive.type != cgltf_primitive_type_triangles
            && primitive.type != cgltf_primitive_type_triangle_strip
            && primitive.type != cgltf_primitive_type_triangle_fan)
        {
            collection.AddWarning("glTF primitive topology is not supported and was skipped: " + meshKey);
            return true;
        }

        const cgltf_accessor* positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
        if (!positions || positions->type != cgltf_type_vec3)
        {
            collection.AddWarning("glTF primitive has no valid POSITION attribute and was skipped: " + meshKey);
            return true;
        }

        if (positions->count > static_cast<cgltf_size>(std::numeric_limits<uint32>::max())
            || mesh.vertices.size() > static_cast<usize>(std::numeric_limits<uint32>::max()) - static_cast<usize>(positions->count))
        {
            collection.AddError("glTF mesh exceeds 32-bit vertex limits: " + meshKey);
            return false;
        }

        const cgltf_accessor* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
        if (normals && (normals->type != cgltf_type_vec3 || normals->count != positions->count))
        {
            collection.AddWarning("glTF NORMAL attribute is invalid and will be rebuilt: " + meshKey);
            normals = nullptr;
        }

        const cgltf_accessor* texcoords = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
        if (texcoords && (texcoords->type != cgltf_type_vec2 || texcoords->count != positions->count))
        {
            collection.AddWarning("glTF TEXCOORD_0 attribute is invalid and was ignored: " + meshKey);
            texcoords = nullptr;
        }

        const cgltf_accessor* tangents = cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0);
        if (tangents && (tangents->type != cgltf_type_vec4 || tangents->count != positions->count))
        {
            collection.AddWarning("glTF TANGENT attribute is invalid and will be rebuilt: " + meshKey);
            tangents = nullptr;
        }

        List<vector3> primitivePositions(static_cast<usize>(positions->count));
        List<vector3> primitiveNormals(static_cast<usize>(positions->count));
        List<vector2> primitiveTexcoords(static_cast<usize>(positions->count));
        List<vector3> primitiveTangents(static_cast<usize>(positions->count));
        for (cgltf_size index = 0; index < positions->count; ++index)
        {
            cgltf_float values[4] = {};
            if (!cgltf_accessor_read_float(positions, index, values, 3))
            {
                collection.AddError("Failed to read glTF POSITION accessor: " + meshKey);
                return false;
            }
            primitivePositions[static_cast<usize>(index)] = { values[0], values[1], values[2] };

            if (normals)
            {
                if (!cgltf_accessor_read_float(normals, index, values, 3))
                {
                    collection.AddError("Failed to read glTF NORMAL accessor: " + meshKey);
                    return false;
                }
                primitiveNormals[static_cast<usize>(index)] = { values[0], values[1], values[2] };
            }

            if (texcoords)
            {
                if (!cgltf_accessor_read_float(texcoords, index, values, 2))
                {
                    collection.AddError("Failed to read glTF TEXCOORD_0 accessor: " + meshKey);
                    return false;
                }
                primitiveTexcoords[static_cast<usize>(index)] = { values[0], values[1] };
            }

            if (tangents)
            {
                if (!cgltf_accessor_read_float(tangents, index, values, 4))
                {
                    collection.AddError("Failed to read glTF TANGENT accessor: " + meshKey);
                    return false;
                }
                primitiveTangents[static_cast<usize>(index)] = { values[0], values[1], values[2] };
            }
        }

        List<uint32> sourceIndices;
        if (primitive.indices)
        {
            if (primitive.indices->type != cgltf_type_scalar
                || (primitive.indices->component_type != cgltf_component_type_r_8u
                    && primitive.indices->component_type != cgltf_component_type_r_16u
                    && primitive.indices->component_type != cgltf_component_type_r_32u))
            {
                collection.AddError("glTF primitive has an invalid index accessor: " + meshKey);
                return false;
            }

            sourceIndices.reserve(static_cast<usize>(primitive.indices->count));
            for (cgltf_size index = 0; index < primitive.indices->count; ++index)
            {
                cgltf_size vertexIndex = cgltf_accessor_read_index(primitive.indices, index);
                if (vertexIndex >= positions->count)
                {
                    collection.AddError("glTF primitive index is out of range: " + meshKey);
                    return false;
                }
                sourceIndices.push_back(static_cast<uint32>(vertexIndex));
            }
        }
        else
        {
            sourceIndices.reserve(static_cast<usize>(positions->count));
            for (uint32 index = 0; index < static_cast<uint32>(positions->count); ++index)
            {
                sourceIndices.push_back(index);
            }
        }

        List<uint32> triangleIndices;
        auto addTriangle = [&](uint32 a, uint32 b, uint32 c)
            {
                if (a == b || b == c || a == c) return;
                triangleIndices.push_back(a);
                triangleIndices.push_back(b);
                triangleIndices.push_back(c);
            };

        if (primitive.type == cgltf_primitive_type_triangles)
        {
            if (sourceIndices.size() % 3 != 0)
            {
                collection.AddWarning("glTF triangle primitive has trailing indices: " + meshKey);
            }
            for (usize index = 0; index + 2 < sourceIndices.size(); index += 3)
            {
                addTriangle(sourceIndices[index], sourceIndices[index + 1], sourceIndices[index + 2]);
            }
        }
        else if (primitive.type == cgltf_primitive_type_triangle_strip)
        {
            for (usize index = 2; index < sourceIndices.size(); ++index)
            {
                uint32 a = sourceIndices[index - 2];
                uint32 b = sourceIndices[index - 1];
                uint32 c = sourceIndices[index];
                if ((index & 1u) != 0) std::swap(a, b);
                addTriangle(a, b, c);
            }
        }
        else
        {
            for (usize index = 2; index < sourceIndices.size(); ++index)
            {
                addTriangle(sourceIndices[0], sourceIndices[index - 1], sourceIndices[index]);
            }
        }

        if (triangleIndices.empty())
        {
            collection.AddWarning("glTF primitive contains no triangles: " + meshKey);
            return true;
        }

        uint32 vertexOffset = static_cast<uint32>(mesh.vertices.size());
        uint32 indexStart = static_cast<uint32>(mesh.indices.size());
        mesh.vertices.insert(mesh.vertices.end(), primitivePositions.begin(), primitivePositions.end());
        mesh.normals.insert(mesh.normals.end(), primitiveNormals.begin(), primitiveNormals.end());
        mesh.texcoords.insert(mesh.texcoords.end(), primitiveTexcoords.begin(), primitiveTexcoords.end());
        mesh.tangents.insert(mesh.tangents.end(), primitiveTangents.begin(), primitiveTangents.end());
        for (uint32 index : triangleIndices)
        {
            mesh.indices.push_back(vertexOffset + index);
        }

        SubMesh subMesh;
        subMesh.name = primitive.material && primitive.material->name && primitive.material->name[0]
            ? primitive.material->name
            : "Primitive_" + std::to_string(mesh.subMeshes.size());
        subMesh.indexStart = indexStart;
        subMesh.indexCount = static_cast<uint32>(mesh.indices.size()) - indexStart;

        MaterialImportInfo materialInfo = defaultMaterial;
        auto materialIt = materials.find(primitive.material);
        if (materialIt != materials.end()) materialInfo = materialIt->second;
        if (materialInfo.material)
        {
            subMesh.material.SetInstanceId(StringId(materialInfo.key));
            ResourceManager::RegisterDependency(meshKey, materialInfo.key);
        }
        mesh.subMeshes.push_back(subMesh);

        needsNormals = needsNormals || !normals;
        needsTangents = needsTangents || !tangents;
        return true;
    }

    //按最终三角形索引重建Mesh切线
    void RebuildMeshTangents(Mesh& mesh)
    {
        mesh.tangents.assign(mesh.vertices.size(), {});
        for (usize index = 0; index + 2 < mesh.indices.size(); index += 3)
        {
            uint32 ia = mesh.indices[index];
            uint32 ib = mesh.indices[index + 1];
            uint32 ic = mesh.indices[index + 2];
            vector3 tangent = ComputeTangent(
                mesh.vertices[ia], mesh.vertices[ib], mesh.vertices[ic],
                mesh.texcoords[ia], mesh.texcoords[ib], mesh.texcoords[ic]);
            AddTo(mesh.tangents[ia], tangent);
            AddTo(mesh.tangents[ib], tangent);
            AddTo(mesh.tangents[ic], tangent);
        }

        for (vector3& tangent : mesh.tangents)
        {
            tangent = Normalize(tangent);
        }
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

    if (extension == ".gltf" || extension == ".glb")
    {
        return Import_GLTF(sourceKey);
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
    shader->ReplaceSource(vertexSource, fragmentSource);
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

    List<ShaderPass> passes;
    if (!ParseOrbShaderSource(sourceKey, source, collection, passes))
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
    for (ShaderPass& pass : passes)
    {
        List<std::string> includeStack;
        std::string expandedVertex;
        if (!ExpandOrbShaderIncludes(sourceKey, "vert", pass.vertexSource, collection, includeStack, expandedVertex))
        {
            return collection;
        }

        includeStack.clear();
        std::string expandedFragment;
        if (!ExpandOrbShaderIncludes(sourceKey, "frag", pass.fragmentSource, collection, includeStack, expandedFragment))
        {
            return collection;
        }

        pass.vertexSource = std::move(expandedVertex);
        pass.fragmentSource = std::move(expandedFragment);
    }

    if (!shader->ReplacePasses(passes))
    {
        collection.AddError("OrbShader uniform uses conflicting material slot types: " + sourceKey);
        return collection;
    }
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

//导入glTF或GLB为复合资源
AssetCollection AssetPipeline::Import_GLTF(std::string path)
{
    AssetCollection collection;
    std::string sourceKey = ResourceManager::ToResourceKey(path);
    collection.sourceKey = sourceKey;
    std::string sourcePath = GetAssetFilePath(sourceKey);
    collection.AddSourceFile(sourcePath);

    if (!FileSystem::Exist(sourcePath))
    {
        collection.AddError("glTF file does not exist: " + sourceKey);
        return collection;
    }

    cgltf_options options = {};
    cgltf_data* rawData = nullptr;
    cgltf_result result = cgltf_parse_file(&options, sourcePath.c_str(), &rawData);
    if (result != cgltf_result_success || !rawData)
    {
        collection.AddError("glTF parse failed (" + std::string(GetCgltfResultText(result)) + "): " + sourceKey);
        return collection;
    }

    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(rawData, &cgltf_free);
    result = cgltf_load_buffers(&options, data.get(), sourcePath.c_str());
    if (result != cgltf_result_success)
    {
        collection.AddError("glTF buffer load failed (" + std::string(GetCgltfResultText(result)) + "): " + sourceKey);
        return collection;
    }

    result = cgltf_validate(data.get());
    if (result != cgltf_result_success)
    {
        collection.AddError("glTF validation failed (" + std::string(GetCgltfResultText(result)) + "): " + sourceKey);
        return collection;
    }

    for (cgltf_size index = 0; index < data->buffers_count; ++index)
    {
        std::string bufferPath = ResolveGltfFileUri(sourcePath, data->buffers[index].uri);
        if (!bufferPath.empty()) collection.AddSourceFile(bufferPath);
    }

    if (data->skins_count > 0)
    {
        collection.AddWarning("glTF skinning data is not supported by Mesh and was ignored: " + sourceKey);
    }
    if (data->animations_count > 0)
    {
        collection.AddWarning("glTF animation data is not supported by AssetPipeline and was ignored: " + sourceKey);
    }

    std::unordered_map<const cgltf_image*, GltfTextureImportInfo> importedTextures;
    std::unordered_map<const cgltf_material*, MaterialImportInfo> materials;
    for (cgltf_size index = 0; index < data->materials_count; ++index)
    {
        const cgltf_material* sourceMaterial = &data->materials[index];
        MaterialImportInfo material = ImportGltfMaterial(
            data.get(), sourceMaterial, static_cast<usize>(index), sourceKey, sourcePath, collection, importedTextures);
        if (material.material) materials[sourceMaterial] = material;
    }

    bool needsDefaultMaterial = false;
    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count && !needsDefaultMaterial; ++meshIndex)
    {
        const cgltf_mesh& sourceMesh = data->meshes[meshIndex];
        for (cgltf_size primitiveIndex = 0; primitiveIndex < sourceMesh.primitives_count; ++primitiveIndex)
        {
            if (!sourceMesh.primitives[primitiveIndex].material)
            {
                needsDefaultMaterial = true;
                break;
            }
        }
    }
    MaterialImportInfo defaultMaterial = needsDefaultMaterial ? CreateDefaultGltfMaterial(sourceKey, collection) : MaterialImportInfo();

    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex)
    {
        const cgltf_mesh& sourceMesh = data->meshes[meshIndex];
        std::string fallbackName = "Mesh_" + std::to_string(meshIndex);
        std::string meshName = sourceMesh.name && sourceMesh.name[0] ? sourceMesh.name : fallbackName;
        std::string meshId = data->meshes_count == 1
            ? "Main"
            : std::to_string(meshIndex) + "_" + SanitizeKeyName(meshName, fallbackName);
        std::string meshKey = sourceKey + "//Mesh/" + meshId;
        Mesh* mesh = CreateImportedObject<Mesh>(meshKey);
        if (!mesh)
        {
            collection.AddError("Failed to create Mesh: " + meshKey);
            continue;
        }

        mesh->name = meshName;
        mesh->ClearGeometry();
        bool needsNormals = false;
        bool needsTangents = false;
        for (cgltf_size primitiveIndex = 0; primitiveIndex < sourceMesh.primitives_count; ++primitiveIndex)
        {
            const cgltf_primitive& primitive = sourceMesh.primitives[primitiveIndex];
            if (primitive.has_draco_mesh_compression)
            {
                collection.AddWarning("glTF Draco-compressed primitive is not supported and was skipped: " + meshKey);
                continue;
            }
            if (primitive.targets_count > 0)
            {
                collection.AddWarning("glTF morph targets are not supported and were ignored: " + meshKey);
            }

            if (!AppendGltfPrimitive(primitive, *mesh, meshKey, materials, defaultMaterial, collection, needsNormals, needsTangents))
            {
                break;
            }
        }

        if (needsNormals && !mesh->indices.empty() && !mesh->RefreshNormals())
        {
            collection.AddError("Failed to rebuild glTF mesh normals: " + meshKey);
        }
        if (needsTangents && !mesh->indices.empty())
        {
            RebuildMeshTangents(*mesh);
        }
        if (mesh->indices.empty())
        {
            collection.AddWarning("glTF mesh imported without triangles: " + meshKey);
        }

        mesh->TouchRevision();
        collection.AddObject(meshKey, mesh, true);
    }

    if (data->meshes_count == 0)
    {
        collection.AddWarning("glTF contains no meshes: " + sourceKey);
    }

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

    std::ifstream input(Utf8Path::FromUtf8(objPath));
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
