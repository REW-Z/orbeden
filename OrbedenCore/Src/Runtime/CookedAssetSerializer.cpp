#include "Runtime/CookedAssetSerializer.h"

#include "Defines/Version.h"
#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Skybox.h"
#include "Runtime/Object/Texture2D.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <type_traits>

namespace
{
    constexpr char BlobMagic[4] = { 'O', 'R', 'B', 'O' };
    constexpr uint32 BlobFormatTag = 1;

    //解析源文件磁盘路径，与 AssetPipeline 的 Key 解析保持一致
    std::string GetSourceFilePath(const std::string& sourceKey)
    {
        if (PathDefines::HasContentRoot()) return PathDefines::GetContentFilePath(sourceKey);

        return sourceKey;
    }

    //位置式二进制写入器
    class BlobWriter
    {
    public:
        explicit BlobWriter(List<uint8>& target)
            : buffer(target)
        {
        }

        void WriteRaw(const void* data, usize size)
        {
            const uint8* begin = static_cast<const uint8*>(data);
            buffer.insert(buffer.end(), begin, begin + size);
        }

        template<typename T>
        void WriteValue(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            WriteRaw(&value, sizeof(T));
        }

        void WriteText(const std::string& text)
        {
            WriteValue(static_cast<uint32>(text.size()));
            if (!text.empty()) WriteRaw(text.data(), text.size());
        }

        //整块写入变长定长记录数组
        template<typename T>
        void WriteArray(const List<T>& values)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            WriteValue(static_cast<uint32>(values.size()));
            if (!values.empty()) WriteRaw(values.data(), values.size() * sizeof(T));
        }

        void WriteRefKey(const StringId& id)
        {
            WriteText(id.IsValid() ? id.GetPath() : std::string());
        }

    private:
        List<uint8>& buffer;
    };

    //位置式二进制读取器，越界即失败
    class BlobReader
    {
    public:
        BlobReader(const uint8* data, usize size)
            : cursor(data), end(data + size)
        {
        }

        bool ReadRaw(void* data, usize size)
        {
            if (size > static_cast<usize>(end - cursor)) return false;

            std::memcpy(data, cursor, size);
            cursor += size;
            return true;
        }

        template<typename T>
        bool ReadValue(T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            return ReadRaw(&value, sizeof(T));
        }

        bool ReadText(std::string& text)
        {
            uint32 size = 0;
            if (!ReadValue(size)) return false;
            if (size > static_cast<usize>(end - cursor)) return false;

            text.assign(reinterpret_cast<const char*>(cursor), size);
            cursor += size;
            return true;
        }

        //整块读取变长定长记录数组，先按剩余字节数拦截异常长度
        template<typename T>
        bool ReadArray(List<T>& values)
        {
            static_assert(std::is_trivially_copyable_v<T>);

            uint32 count = 0;
            if (!ReadValue(count)) return false;
            if (count > static_cast<usize>(end - cursor) / sizeof(T)) return false;

            values.resize(count);
            if (count == 0) return true;
            return ReadRaw(values.data(), static_cast<usize>(count) * sizeof(T));
        }

        //读取引用 Key 并记录到跨文件引用表
        bool ReadRefKey(List<std::string>& externalRefs, StringId& id)
        {
            std::string key;
            if (!ReadText(key)) return false;

            id = StringId(key);
            if (id.IsValid()) externalRefs.push_back(key);
            return true;
        }

        //判断是否正好读完
        bool AtEnd() const
        {
            return cursor == end;
        }

    private:
        const uint8* cursor = nullptr;
        const uint8* end = nullptr;
    };

    //写入纹理载荷
    bool WriteTexture2D(BlobWriter& writer, Texture2D* texture)
    {
        writer.WriteText(texture->name);
        writer.WriteValue(static_cast<int32>(texture->width));
        writer.WriteValue(static_cast<int32>(texture->height));
        writer.WriteValue(static_cast<int32>(texture->channels));
        writer.WriteValue(static_cast<int32>(texture->format));
        writer.WriteArray(texture->pixels);
        return true;
    }

    //读取纹理载荷
    bool ReadTexture2D(BlobReader& reader, Texture2D* texture)
    {
        return reader.ReadText(texture->name)
            && reader.ReadValue(texture->width)
            && reader.ReadValue(texture->height)
            && reader.ReadValue(texture->channels)
            && reader.ReadValue(texture->format)
            && reader.ReadArray(texture->pixels);
    }

    //写入网格载荷
    bool WriteMesh(BlobWriter& writer, Mesh* mesh)
    {
        writer.WriteText(mesh->name);
        writer.WriteArray(mesh->vertices);
        writer.WriteArray(mesh->texcoords);
        writer.WriteArray(mesh->normals);
        writer.WriteArray(mesh->tangents);
        writer.WriteArray(mesh->indices);
        writer.WriteValue(static_cast<uint32>(mesh->subMeshes.size()));
        for (const SubMesh& subMesh : mesh->subMeshes)
        {
            writer.WriteText(subMesh.name);
            writer.WriteValue(static_cast<uint32>(subMesh.indexStart));
            writer.WriteValue(static_cast<uint32>(subMesh.indexCount));
        }

        return true;
    }

    //读取网格载荷，必须先顶点后通道与索引，子网格最后
    bool ReadMesh(BlobReader& reader, Mesh* mesh, List<std::string>& externalRefs)
    {
        List<vector3> vertices;
        List<vector2> texcoords;
        List<vector3> normals;
        List<vector3> tangents;
        List<uint32> indices;
        if (!reader.ReadText(mesh->name)) return false;
        if (!reader.ReadArray(vertices)) return false;
        if (!reader.ReadArray(texcoords)) return false;
        if (!reader.ReadArray(normals)) return false;
        if (!reader.ReadArray(tangents)) return false;
        if (!reader.ReadArray(indices)) return false;

        //通道与索引的数量校验依赖已经写好的顶点
        if (!mesh->SetVertexPositions(vertices.empty() ? nullptr : vertices.data(), static_cast<int32>(vertices.size()))) return false;
        if (!mesh->SetVertexTexcoords(texcoords.empty() ? nullptr : texcoords.data(), static_cast<int32>(texcoords.size()))) return false;
        if (!mesh->SetVertexNormals(normals.empty() ? nullptr : normals.data(), static_cast<int32>(normals.size()))) return false;
        if (!mesh->SetVertexTangents(tangents.empty() ? nullptr : tangents.data(), static_cast<int32>(tangents.size()))) return false;
        if (!mesh->SetIndexData(indices.empty() ? nullptr : indices.data(), static_cast<int32>(indices.size()))) return false;

        uint32 subMeshCount = 0;
        if (!reader.ReadValue(subMeshCount)) return false;

        List<SubMesh> subMeshes;
        subMeshes.resize(subMeshCount);
        for (SubMesh& subMesh : subMeshes)
        {
            if (!reader.ReadText(subMesh.name)) return false;
            if (!reader.ReadValue(subMesh.indexStart)) return false;
            if (!reader.ReadValue(subMesh.indexCount)) return false;
        }

        //子网格的索引范围校验依赖已经写好的索引
        return mesh->SetSubMeshes(subMeshes);
    }

    //写入材质载荷
    bool WriteMaterial(BlobWriter& writer, Material* material)
    {
        writer.WriteText(material->name);
        writer.WriteValue(static_cast<uint32>(material->textureSlots.size()));
        for (const MaterialTextureSlot& slot : material->textureSlots)
        {
            writer.WriteText(slot.name);
            writer.WriteRefKey(slot.texture.GetInstanceId());
        }

        writer.WriteValue(static_cast<uint32>(material->colorSlots.size()));
        for (const MaterialColorSlot& slot : material->colorSlots)
        {
            writer.WriteText(slot.name);
            writer.WriteValue(slot.value);
        }

        writer.WriteValue(static_cast<uint32>(material->floatSlots.size()));
        for (const MaterialFloatSlot& slot : material->floatSlots)
        {
            writer.WriteText(slot.name);
            writer.WriteValue(static_cast<float32>(slot.value));
        }

        writer.WriteRefKey(material->shader.GetInstanceId());
        return true;
    }

    //读取材质载荷
    bool ReadMaterial(BlobReader& reader, Material* material, List<std::string>& externalRefs)
    {
        if (!reader.ReadText(material->name)) return false;

        uint32 textureSlotCount = 0;
        if (!reader.ReadValue(textureSlotCount)) return false;
        for (uint32 index = 0; index < textureSlotCount; ++index)
        {
            std::string slotName;
            StringId textureId;
            if (!reader.ReadText(slotName)) return false;
            if (!reader.ReadRefKey(externalRefs, textureId)) return false;

            material->SetTexture(slotName, textureId);
        }

        uint32 colorSlotCount = 0;
        if (!reader.ReadValue(colorSlotCount)) return false;
        for (uint32 index = 0; index < colorSlotCount; ++index)
        {
            std::string slotName;
            color value;
            if (!reader.ReadText(slotName)) return false;
            if (!reader.ReadValue(value)) return false;

            material->SetColor(slotName, value);
        }

        uint32 floatSlotCount = 0;
        if (!reader.ReadValue(floatSlotCount)) return false;
        for (uint32 index = 0; index < floatSlotCount; ++index)
        {
            std::string slotName;
            float32 value = 0.0f;
            if (!reader.ReadText(slotName)) return false;
            if (!reader.ReadValue(value)) return false;

            material->SetFloat(slotName, value);
        }

        StringId shaderId;
        if (!reader.ReadRefKey(externalRefs, shaderId)) return false;

        material->SetShader(shaderId);
        return true;
    }

    //写入着色器载荷，槽位由 Passes 反射重建，不单独存盘
    bool WriteShader(BlobWriter& writer, Shader* shader)
    {
        writer.WriteText(shader->name);
        writer.WriteText(shader->vertexPath);
        writer.WriteText(shader->fragmentPath);
        writer.WriteValue(static_cast<uint32>(shader->passes.size()));
        for (const ShaderPass& pass : shader->passes)
        {
            writer.WriteText(pass.name);
            writer.WriteValue(static_cast<uint32>(pass.state.depthTest));
            writer.WriteValue(static_cast<uint32>(pass.state.depthWrite));
            writer.WriteValue(static_cast<uint32>(pass.state.blend));
            writer.WriteValue(static_cast<uint32>(pass.state.cull));
            writer.WriteText(pass.vertexSource);
            writer.WriteText(pass.fragmentSource);
        }

        return true;
    }

    //读取着色器载荷
    bool ReadShader(BlobReader& reader, Shader* shader)
    {
        if (!reader.ReadText(shader->name)) return false;
        if (!reader.ReadText(shader->vertexPath)) return false;
        if (!reader.ReadText(shader->fragmentPath)) return false;

        uint32 passCount = 0;
        if (!reader.ReadValue(passCount)) return false;
        if (passCount == 0) return false;

        List<ShaderPass> passes;
        passes.resize(passCount);
        for (ShaderPass& pass : passes)
        {
            uint32 depthTest = 0;
            uint32 depthWrite = 0;
            uint32 blend = 0;
            uint32 cull = 0;
            if (!reader.ReadText(pass.name)) return false;
            if (!reader.ReadValue(depthTest)) return false;
            if (!reader.ReadValue(depthWrite)) return false;
            if (!reader.ReadValue(blend)) return false;
            if (!reader.ReadValue(cull)) return false;
            if (!reader.ReadText(pass.vertexSource)) return false;
            if (!reader.ReadText(pass.fragmentSource)) return false;

            pass.state.depthTest = static_cast<ShaderPassToggle>(depthTest);
            pass.state.depthWrite = static_cast<ShaderPassToggle>(depthWrite);
            pass.state.blend = static_cast<ShaderPassToggle>(blend);
            pass.state.cull = static_cast<CullMode>(cull);
        }

        //替换 Passes 会从源码重新反射材质槽位并刷新兼容源码
        return shader->ReplacePasses(passes);
    }

    //写入天空盒载荷
    bool WriteSkybox(BlobWriter& writer, Skybox* skybox)
    {
        writer.WriteRefKey(skybox->right.GetInstanceId());
        writer.WriteRefKey(skybox->left.GetInstanceId());
        writer.WriteRefKey(skybox->top.GetInstanceId());
        writer.WriteRefKey(skybox->bottom.GetInstanceId());
        writer.WriteRefKey(skybox->front.GetInstanceId());
        writer.WriteRefKey(skybox->back.GetInstanceId());
        return true;
    }

    //读取天空盒载荷
    bool ReadSkybox(BlobReader& reader, Skybox* skybox, List<std::string>& externalRefs)
    {
        Ref<Texture2D>* faces[] = { &skybox->right, &skybox->left, &skybox->top, &skybox->bottom, &skybox->front, &skybox->back };
        for (Ref<Texture2D>* face : faces)
        {
            StringId faceId;
            if (!reader.ReadRefKey(externalRefs, faceId)) return false;

            face->SetInstanceId(faceId);
        }

        return true;
    }

    //按资源类型写入载荷
    bool WritePayload(BlobWriter& writer, Object* object, std::string& error)
    {
        if (Texture2D* texture = object->Cast<Texture2D>()) return WriteTexture2D(writer, texture);
        if (Mesh* mesh = object->Cast<Mesh>()) return WriteMesh(writer, mesh);
        if (Material* material = object->Cast<Material>()) return WriteMaterial(writer, material);
        if (Shader* shader = object->Cast<Shader>()) return WriteShader(writer, shader);
        if (Skybox* skybox = object->Cast<Skybox>()) return WriteSkybox(writer, skybox);

        error = "Resource type cannot be packaged: " + std::string(object->GetType()->GetName());
        return false;
    }

    //按资源类型读取载荷
    bool ReadPayload(BlobReader& reader, Object* object, List<std::string>& externalRefs, std::string& error)
    {
        if (Texture2D* texture = object->Cast<Texture2D>()) return ReadTexture2D(reader, texture);
        if (Mesh* mesh = object->Cast<Mesh>()) return ReadMesh(reader, mesh, externalRefs);
        if (Material* material = object->Cast<Material>()) return ReadMaterial(reader, material, externalRefs);
        if (Shader* shader = object->Cast<Shader>()) return ReadShader(reader, shader);
        if (Skybox* skybox = object->Cast<Skybox>()) return ReadSkybox(reader, skybox, externalRefs);

        error = "Resource type cannot be unpacked: " + std::string(object->GetType()->GetName());
        return false;
    }

    //读取源文件时间戳与大小，供将来做产物失效
    void ReadSourceStamp(const std::string& sourceKey, uint64& modifiedTime, uint64& fileSize)
    {
        modifiedTime = 0;
        fileSize = 0;

        std::error_code code;
        std::filesystem::path sourcePath = Utf8Path::FromUtf8(GetSourceFilePath(sourceKey));
        std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(sourcePath, code);
        if (code) return;

        modifiedTime = static_cast<uint64>(writeTime.time_since_epoch().count());
        uintmax_t size = std::filesystem::file_size(sourcePath, code);
        if (!code) fileSize = static_cast<uint64>(size);
    }
}

//按资源 Key 计算打包文件名
std::string CookedAssetSerializer::GetBlobFileName(const std::string& resourceKey)
{
    static const char* Digits = "0123456789abcdef";
    uint64 hash = StringId::CalculateHash(ResourceManager::ToResourceKey(resourceKey));

    std::string name(sizeof(uint64) * 2, '0');
    for (int32 index = static_cast<int32>(name.size()) - 1; index >= 0; --index)
    {
        name[index] = Digits[hash & 0xF];
        hash >>= 4;
    }

    return name + ".orbo";
}

//按资源 Key 计算打包文件路径
std::string CookedAssetSerializer::GetBlobPath(const std::string& resourceKey)
{
    std::string fileName = GetBlobFileName(resourceKey);
    if (PathDefines::HasContentRoot()) return PathDefines::GetContentFilePath(fileName);

    return fileName;
}

//写出打包清单
bool CookedAssetSerializer::WriteIndex(const std::string& indexPath, const List<std::string>& resourceKeys, std::string& error)
{
    error.clear();

    List<std::string> sortedKeys = resourceKeys;
    std::sort(sortedKeys.begin(), sortedKeys.end());
    sortedKeys.erase(std::unique(sortedKeys.begin(), sortedKeys.end()), sortedKeys.end());

    std::ofstream output(Utf8Path::FromUtf8(indexPath), std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        error = "Cooked asset index could not be written: " + indexPath;
        return false;
    }

    //文件名在前、资源 Key 在后，便于人工对照哈希文件。
    for (const std::string& key : sortedKeys)
    {
        output << GetBlobFileName(key) << '\t' << key << '\n';
    }

    if (!output.good())
    {
        error = "Cooked asset index write failed: " + indexPath;
        return false;
    }

    output.close();
    return true;
}

//读取内容根内的打包清单
bool CookedAssetSerializer::ReadIndex(List<std::string>& resourceKeys)
{
    resourceKeys.clear();
    if (!PathDefines::HasContentRoot()) return false;

    std::ifstream input(Utf8Path::FromUtf8(PathDefines::GetContentFilePath(IndexFileName)), std::ios::binary);
    if (!input.is_open()) return false;

    std::string line;
    while (std::getline(input, line))
    {
        usize separator = line.find('\t');
        if (separator == std::string::npos) continue;

        resourceKeys.push_back(line.substr(separator + 1));
    }

    return true;
}

//写出一个资源对象及其依赖边
bool CookedAssetSerializer::Write(const std::string& blobPath, Object* object, const std::string& sourceKey, const List<std::string>& dependencies, std::string& error)
{
    error.clear();
    if (!object)
    {
        error = "Cooked asset write requires a resource object.";
        return false;
    }

    uint64 sourceMtime = 0;
    uint64 sourceSize = 0;
    ReadSourceStamp(sourceKey, sourceMtime, sourceSize);

    List<uint8> bytes;
    BlobWriter writer(bytes);
    writer.WriteRaw(BlobMagic, sizeof(BlobMagic));
    writer.WriteValue(BlobFormatTag);
    writer.WriteText(object->GetInstanceId().GetPath());
    writer.WriteText(object->GetType()->GetName());
    writer.WriteValue(sourceMtime);
    writer.WriteValue(sourceSize);
    writer.WriteValue(static_cast<uint32>(OrbedenProjectVersion));
    writer.WriteValue(static_cast<uint32>(dependencies.size()));
    for (const std::string& dependency : dependencies) writer.WriteText(dependency);

    if (!WritePayload(writer, object, error)) return false;

    std::filesystem::path path = Utf8Path::FromUtf8(blobPath);
    std::error_code code;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), code);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        error = "Cooked asset file could not be written: " + blobPath;
        return false;
    }

    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output.good())
    {
        error = "Cooked asset file write failed: " + blobPath;
        return false;
    }

    output.close();
    return true;
}

//读出资源对象并注册
bool CookedAssetSerializer::Read(const std::string& blobPath, List<std::string>& externalRefs, std::string& error)
{
    error.clear();

    std::ifstream input(Utf8Path::FromUtf8(blobPath), std::ios::binary);
    if (!input.is_open())
    {
        error = "Cooked asset file was not found: " + blobPath;
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (content.size() < sizeof(BlobMagic))
    {
        error = "Cooked asset file is truncated: " + blobPath;
        return false;
    }

    BlobReader reader(reinterpret_cast<const uint8*>(content.data()), content.size());
    char magic[sizeof(BlobMagic)] = {};
    if (!reader.ReadRaw(magic, sizeof(magic)) || std::memcmp(magic, BlobMagic, sizeof(magic)) != 0)
    {
        error = "Cooked asset file has an invalid header: " + blobPath;
        return false;
    }

    uint32 formatTag = 0;
    if (!reader.ReadValue(formatTag) || formatTag != BlobFormatTag)
    {
        error = "Cooked asset file was produced by a different engine build: " + blobPath;
        return false;
    }

    std::string resourceKey;
    std::string typeName;
    uint64 sourceMtime = 0;
    uint64 sourceSize = 0;
    uint32 importerVersion = 0;
    uint32 dependencyCount = 0;
    if (!reader.ReadText(resourceKey) || !reader.ReadText(typeName))
    {
        error = "Cooked asset file has an invalid header: " + blobPath;
        return false;
    }

    //时间戳、大小与导入器版本属于预留的失效信息，当前只写不读。
    if (!reader.ReadValue(sourceMtime) || !reader.ReadValue(sourceSize) || !reader.ReadValue(importerVersion))
    {
        error = "Cooked asset file has an invalid header: " + blobPath;
        return false;
    }

    List<std::string> dependencies;
    if (!reader.ReadValue(dependencyCount))
    {
        error = "Cooked asset file has an invalid header: " + blobPath;
        return false;
    }

    dependencies.resize(dependencyCount);
    for (std::string& dependency : dependencies)
    {
        if (!reader.ReadText(dependency))
        {
            error = "Cooked asset file has an invalid header: " + blobPath;
            return false;
        }
    }

    //Key 必须与文件名哈希一致，否则说明文件被改名或发生碰撞。
    if (GetBlobFileName(resourceKey) != Utf8Path::ToUtf8(Utf8Path::FromUtf8(blobPath).filename()))
    {
        error = "Cooked asset key does not match its file name: " + blobPath;
        return false;
    }

    Type* type = Object::FindType(typeName);
    if (!type)
    {
        error = "Cooked asset type is not registered: " + typeName;
        return false;
    }

    Object* object = Object::CreateResourceInstance(type, resourceKey);
    if (!object)
    {
        error = "Cooked asset object could not be created: " + resourceKey;
        return false;
    }

    if (!ReadPayload(reader, object, externalRefs, error) || !reader.AtEnd())
    {
        if (error.empty()) error = "Cooked asset payload is malformed: " + blobPath;

        Object::DeleteInstance(object);
        return false;
    }

    if (!ResourceManager::RegisterObject(resourceKey, object))
    {
        error = "Cooked asset object could not be registered: " + resourceKey;
        Object::DeleteInstance(object);
        return false;
    }

    for (const std::string& dependency : dependencies)
    {
        ResourceManager::RegisterDependency(resourceKey, dependency);
    }

    return true;
}
