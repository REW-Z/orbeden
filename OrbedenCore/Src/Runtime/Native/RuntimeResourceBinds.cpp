#include "Runtime/Native/RuntimeResourceBinds.h"

#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Texture2D.h"
#include "Runtime/Native/NativeCall.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/World.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
    //从 UTF-8 字节创建字符串。
    std::string ReadUtf8Text(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return std::string();
        return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(length));
    }

    //把 UTF-8 字符串写入 C# 提供的缓冲区，并返回完整字节数。
    int32 CopyText(const std::string& text, uint8* buffer, int32 bufferSize)
    {
        int32 byteCount = static_cast<int32>(text.size());
        if (buffer && bufferSize > 0 && byteCount > 0)
        {
            int32 copyCount = std::min(byteCount, bufferSize);
            std::memcpy(buffer, text.data(), static_cast<size_t>(copyCount));
        }

        return byteCount;
    }

    //读取并规范化资源 Key。
    std::string ReadResourceKey(const uint8* text, int32 length)
    {
        return ResourceManager::ToResourceKey(ReadUtf8Text(text, length));
    }

    //查找已加载资源对象。
    template<typename TObject>
    TObject* FindResource(const std::string& key)
    {
        Object* object = ResourceManager::FindLoaded(key);
        return object ? object->Cast<TObject>() : nullptr;
    }

    //查找已加载资源对象。
    template<typename TObject>
    TObject* FindResource(const uint8* key, int32 length)
    {
        return FindResource<TObject>(ReadResourceKey(key, length));
    }

    //加载资源并绑定到当前 World 生命周期。
    template<typename TObject>
    TObject* LoadResource(const uint8* key, int32 length)
    {
        std::string resourceKey = ReadResourceKey(key, length);
        if (resourceKey.empty()) return nullptr;

        World* world = World::CurrentWorld();
        if (world)
        {
            if (!world->AddExternResourceRef(TObject::StaticType(), resourceKey)) return nullptr;
            return FindResource<TObject>(resourceKey);
        }

        return ResourceManager::Load<TObject>(resourceKey);
    }

    //加载资源并返回是否成功。
    template<typename TObject>
    uint8 LoadResourceResult(const uint8* key, int32 length)
    {
        return LoadResource<TObject>(key, length) ? 1 : 0;
    }

    //判断资源是否有效。
    template<typename TObject>
    uint8 IsResourceValid(const uint8* key, int32 length)
    {
        return FindResource<TObject>(key, length) ? 1 : 0;
    }

    //把资源引用 ID 写到 C# 缓冲区。
    template<typename TObject>
    int32 CopyRefKey(const Ref<TObject>& value, uint8* buffer, int32 bufferSize)
    {
        return CopyText(value.GetInstanceId().GetPath(), buffer, bufferSize);
    }

    //加载目标资源并写入 Ref。
    template<typename TObject>
    bool SetLoadedRef(Ref<TObject>& target, const std::string& key)
    {
        if (key.empty())
        {
            std::string oldKey = target.GetInstanceId().GetPath();
            if (!oldKey.empty()) ResourceManager::ReleaseWorldRef(oldKey);
            target.SetInstanceId(StringId());
            return true;
        }

        TObject* resource = nullptr;
        World* world = World::CurrentWorld();
        if (world)
        {
            if (!world->AddExternResourceRef(TObject::StaticType(), key)) return false;
            resource = FindResource<TObject>(key);
        }
        else
        {
            resource = ResourceManager::Load<TObject>(key);
        }

        if (!resource) return false;

        std::string oldKey = target.GetInstanceId().GetPath();
        if (!oldKey.empty() && oldKey != key) ResourceManager::ReleaseWorldRef(oldKey);
        target.SetInstanceId(StringId(key));
        return true;
    }

    //读取 Mesh 名称。
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetName(const uint8* key, int32 length, uint8* buffer, int32 bufferSize)
    {
        Mesh* mesh = FindResource<Mesh>(key, length);
        return CopyText(mesh ? mesh->name : std::string(), buffer, bufferSize);
    }

    //读取 Mesh 顶点数量。
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetVertexCount(const uint8* key, int32 length)
    {
        Mesh* mesh = FindResource<Mesh>(key, length);
        return mesh ? static_cast<int32>(mesh->vertices.size()) : 0;
    }

    //读取 Mesh 索引数量。
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetIndexCount(const uint8* key, int32 length)
    {
        Mesh* mesh = FindResource<Mesh>(key, length);
        return mesh ? static_cast<int32>(mesh->indices.size()) : 0;
    }

    //读取 Mesh 子网格数量。
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetSubMeshCount(const uint8* key, int32 length)
    {
        Mesh* mesh = FindResource<Mesh>(key, length);
        return mesh ? static_cast<int32>(mesh->subMeshes.size()) : 0;
    }

    //读取 SubMesh。
    const SubMesh* GetSubMesh(const uint8* key, int32 length, int32 index)
    {
        Mesh* mesh = FindResource<Mesh>(key, length);
        if (!mesh || index < 0 || static_cast<usize>(index) >= mesh->subMeshes.size()) return nullptr;
        return &mesh->subMeshes[static_cast<usize>(index)];
    }

    //读取 SubMesh 名称。
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetSubMeshName(const uint8* key, int32 length, int32 index, uint8* buffer, int32 bufferSize)
    {
        const SubMesh* subMesh = GetSubMesh(key, length, index);
        return CopyText(subMesh ? subMesh->name : std::string(), buffer, bufferSize);
    }

    //读取 SubMesh 起始索引。
    uint32 ORBEDEN_NATIVE_CALL NativeMeshGetSubMeshIndexStart(const uint8* key, int32 length, int32 index)
    {
        const SubMesh* subMesh = GetSubMesh(key, length, index);
        return subMesh ? subMesh->indexStart : 0u;
    }

    //读取 SubMesh 索引数量。
    uint32 ORBEDEN_NATIVE_CALL NativeMeshGetSubMeshIndexCount(const uint8* key, int32 length, int32 index)
    {
        const SubMesh* subMesh = GetSubMesh(key, length, index);
        return subMesh ? subMesh->indexCount : 0u;
    }

    //读取 SubMesh 材质 Key。
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetSubMeshMaterial(const uint8* key, int32 length, int32 index, uint8* buffer, int32 bufferSize)
    {
        const SubMesh* subMesh = GetSubMesh(key, length, index);
        return subMesh ? CopyRefKey(subMesh->material, buffer, bufferSize) : 0;
    }

    //读取 Material 名称。
    int32 ORBEDEN_NATIVE_CALL NativeMaterialGetName(const uint8* key, int32 length, uint8* buffer, int32 bufferSize)
    {
        Material* material = FindResource<Material>(key, length);
        return CopyText(material ? material->name : std::string(), buffer, bufferSize);
    }

    //读取 Material Shader Key。
    int32 ORBEDEN_NATIVE_CALL NativeMaterialGetShader(const uint8* key, int32 length, uint8* buffer, int32 bufferSize)
    {
        Material* material = FindResource<Material>(key, length);
        return material ? CopyRefKey(material->shader, buffer, bufferSize) : 0;
    }

    //设置 Material Shader Key。
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialSetShader(const uint8* key, int32 length, const uint8* shaderKey, int32 shaderLength)
    {
        Material* material = FindResource<Material>(key, length);
        if (!material) return 0;

        return SetLoadedRef(material->shader, ReadResourceKey(shaderKey, shaderLength)) ? 1 : 0;
    }

    //判断材质纹理槽是否存在。
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialHasTexture(const uint8* key, int32 length, const uint8* slotName, int32 slotLength)
    {
        Material* material = FindResource<Material>(key, length);
        return material && material->HasTexture(ReadUtf8Text(slotName, slotLength)) ? 1 : 0;
    }

    //读取材质纹理 Key。
    int32 ORBEDEN_NATIVE_CALL NativeMaterialGetTexture(const uint8* key, int32 length, const uint8* slotName, int32 slotLength, uint8* buffer, int32 bufferSize)
    {
        Material* material = FindResource<Material>(key, length);
        if (!material) return 0;

        for (const MaterialTextureSlot& slot : material->textureSlots)
        {
            if (slot.name == ReadUtf8Text(slotName, slotLength))
            {
                return CopyRefKey(slot.texture, buffer, bufferSize);
            }
        }

        return 0;
    }

    //设置材质纹理 Key。
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialSetTexture(const uint8* key, int32 length, const uint8* slotName, int32 slotLength, const uint8* textureKey, int32 textureLength)
    {
        Material* material = FindResource<Material>(key, length);
        if (!material) return 0;

        std::string textureResourceKey = ReadResourceKey(textureKey, textureLength);
        if (!textureResourceKey.empty() && !LoadResource<Texture2D>(textureKey, textureLength)) return 0;

        material->SetTexture(ReadUtf8Text(slotName, slotLength), StringId(textureResourceKey));
        return 1;
    }

    //清除材质纹理槽。
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialClearTexture(const uint8* key, int32 length, const uint8* slotName, int32 slotLength)
    {
        Material* material = FindResource<Material>(key, length);
        if (!material) return 0;

        material->ClearTexture(ReadUtf8Text(slotName, slotLength));
        return 1;
    }

    //判断材质颜色槽是否存在。
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialHasColor(const uint8* key, int32 length, const uint8* slotName, int32 slotLength)
    {
        Material* material = FindResource<Material>(key, length);
        return material && material->HasColor(ReadUtf8Text(slotName, slotLength)) ? 1 : 0;
    }

    //读取材质颜色槽。
    color ORBEDEN_NATIVE_CALL NativeMaterialGetColor(const uint8* key, int32 length, const uint8* slotName, int32 slotLength, color defaultValue)
    {
        Material* material = FindResource<Material>(key, length);
        return material ? material->GetColor(ReadUtf8Text(slotName, slotLength), defaultValue) : defaultValue;
    }

    //设置材质颜色槽。
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialSetColor(const uint8* key, int32 length, const uint8* slotName, int32 slotLength, color value)
    {
        Material* material = FindResource<Material>(key, length);
        if (!material) return 0;

        material->SetColor(ReadUtf8Text(slotName, slotLength), value);
        return 1;
    }

    //清除材质颜色槽。
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialClearColor(const uint8* key, int32 length, const uint8* slotName, int32 slotLength)
    {
        Material* material = FindResource<Material>(key, length);
        if (!material) return 0;

        material->ClearColor(ReadUtf8Text(slotName, slotLength));
        return 1;
    }

    //判断材质浮点槽是否存在。
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialHasFloat(const uint8* key, int32 length, const uint8* slotName, int32 slotLength)
    {
        Material* material = FindResource<Material>(key, length);
        return material && material->HasFloat(ReadUtf8Text(slotName, slotLength)) ? 1 : 0;
    }

    //读取材质浮点槽。
    float32 ORBEDEN_NATIVE_CALL NativeMaterialGetFloat(const uint8* key, int32 length, const uint8* slotName, int32 slotLength, float32 defaultValue)
    {
        Material* material = FindResource<Material>(key, length);
        return material ? material->GetFloat(ReadUtf8Text(slotName, slotLength), defaultValue) : defaultValue;
    }

    //设置材质浮点槽。
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialSetFloat(const uint8* key, int32 length, const uint8* slotName, int32 slotLength, float32 value)
    {
        Material* material = FindResource<Material>(key, length);
        if (!material) return 0;

        material->SetFloat(ReadUtf8Text(slotName, slotLength), value);
        return 1;
    }

    //清除材质浮点槽。
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialClearFloat(const uint8* key, int32 length, const uint8* slotName, int32 slotLength)
    {
        Material* material = FindResource<Material>(key, length);
        if (!material) return 0;

        material->ClearFloat(ReadUtf8Text(slotName, slotLength));
        return 1;
    }

    //读取材质版本。
    uint64 ORBEDEN_NATIVE_CALL NativeMaterialGetRevision(const uint8* key, int32 length)
    {
        Material* material = FindResource<Material>(key, length);
        return material ? material->GetRevision() : 0u;
    }

    //读取 Shader 名称。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetName(const uint8* key, int32 length, uint8* buffer, int32 bufferSize)
    {
        Shader* shader = FindResource<Shader>(key, length);
        return CopyText(shader ? shader->name : std::string(), buffer, bufferSize);
    }

    //读取 Shader 顶点源码路径。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetVertexPath(const uint8* key, int32 length, uint8* buffer, int32 bufferSize)
    {
        Shader* shader = FindResource<Shader>(key, length);
        return CopyText(shader ? shader->vertexPath : std::string(), buffer, bufferSize);
    }

    //读取 Shader 片元源码路径。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetFragmentPath(const uint8* key, int32 length, uint8* buffer, int32 bufferSize)
    {
        Shader* shader = FindResource<Shader>(key, length);
        return CopyText(shader ? shader->fragmentPath : std::string(), buffer, bufferSize);
    }

    //读取 Shader 纹理槽数量。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetTextureSlotCount(const uint8* key, int32 length)
    {
        Shader* shader = FindResource<Shader>(key, length);
        return shader ? static_cast<int32>(shader->textureSlots.size()) : 0;
    }

    //读取 Shader 颜色槽数量。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetColorSlotCount(const uint8* key, int32 length)
    {
        Shader* shader = FindResource<Shader>(key, length);
        return shader ? static_cast<int32>(shader->colorSlots.size()) : 0;
    }

    //读取 Shader 浮点槽数量。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetFloatSlotCount(const uint8* key, int32 length)
    {
        Shader* shader = FindResource<Shader>(key, length);
        return shader ? static_cast<int32>(shader->floatSlots.size()) : 0;
    }

    //读取 Shader 纹理槽。
    const ShaderTextureSlot* GetShaderTextureSlot(const uint8* key, int32 length, int32 index)
    {
        Shader* shader = FindResource<Shader>(key, length);
        if (!shader || index < 0 || static_cast<usize>(index) >= shader->textureSlots.size()) return nullptr;
        return &shader->textureSlots[static_cast<usize>(index)];
    }

    //读取 Shader 颜色槽。
    const ShaderColorSlot* GetShaderColorSlot(const uint8* key, int32 length, int32 index)
    {
        Shader* shader = FindResource<Shader>(key, length);
        if (!shader || index < 0 || static_cast<usize>(index) >= shader->colorSlots.size()) return nullptr;
        return &shader->colorSlots[static_cast<usize>(index)];
    }

    //读取 Shader 浮点槽。
    const ShaderFloatSlot* GetShaderFloatSlot(const uint8* key, int32 length, int32 index)
    {
        Shader* shader = FindResource<Shader>(key, length);
        if (!shader || index < 0 || static_cast<usize>(index) >= shader->floatSlots.size()) return nullptr;
        return &shader->floatSlots[static_cast<usize>(index)];
    }

    //读取 Shader 纹理槽名称。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetTextureSlotName(const uint8* key, int32 length, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderTextureSlot* slot = GetShaderTextureSlot(key, length, index);
        return CopyText(slot ? slot->name : std::string(), buffer, bufferSize);
    }

    //读取 Shader 纹理槽显示名。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetTextureSlotDisplayName(const uint8* key, int32 length, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderTextureSlot* slot = GetShaderTextureSlot(key, length, index);
        return CopyText(slot ? slot->displayName : std::string(), buffer, bufferSize);
    }

    //读取 Shader 纹理槽维度。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetTextureSlotDimension(const uint8* key, int32 length, int32 index)
    {
        const ShaderTextureSlot* slot = GetShaderTextureSlot(key, length, index);
        return slot ? static_cast<int32>(slot->dimension) : 0;
    }

    //读取 Shader 颜色槽名称。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetColorSlotName(const uint8* key, int32 length, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderColorSlot* slot = GetShaderColorSlot(key, length, index);
        return CopyText(slot ? slot->name : std::string(), buffer, bufferSize);
    }

    //读取 Shader 颜色槽显示名。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetColorSlotDisplayName(const uint8* key, int32 length, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderColorSlot* slot = GetShaderColorSlot(key, length, index);
        return CopyText(slot ? slot->displayName : std::string(), buffer, bufferSize);
    }

    //读取 Shader 颜色槽默认值。
    color ORBEDEN_NATIVE_CALL NativeShaderGetColorSlotDefault(const uint8* key, int32 length, int32 index)
    {
        const ShaderColorSlot* slot = GetShaderColorSlot(key, length, index);
        return slot ? slot->defaultValue : color();
    }

    //读取 Shader 浮点槽名称。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetFloatSlotName(const uint8* key, int32 length, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderFloatSlot* slot = GetShaderFloatSlot(key, length, index);
        return CopyText(slot ? slot->name : std::string(), buffer, bufferSize);
    }

    //读取 Shader 浮点槽显示名。
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetFloatSlotDisplayName(const uint8* key, int32 length, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderFloatSlot* slot = GetShaderFloatSlot(key, length, index);
        return CopyText(slot ? slot->displayName : std::string(), buffer, bufferSize);
    }

    //读取 Shader 浮点槽默认值。
    float32 ORBEDEN_NATIVE_CALL NativeShaderGetFloatSlotDefault(const uint8* key, int32 length, int32 index)
    {
        const ShaderFloatSlot* slot = GetShaderFloatSlot(key, length, index);
        return slot ? slot->defaultValue : 0.0f;
    }
}

MeshBind MeshBind::Create()
{
    MeshBind bind;
    bind.Load = reinterpret_cast<void*>(&LoadResourceResult<Mesh>);
    bind.IsValid = reinterpret_cast<void*>(&IsResourceValid<Mesh>);
    bind.GetName = reinterpret_cast<void*>(&NativeMeshGetName);
    bind.GetVertexCount = reinterpret_cast<void*>(&NativeMeshGetVertexCount);
    bind.GetIndexCount = reinterpret_cast<void*>(&NativeMeshGetIndexCount);
    bind.GetSubMeshCount = reinterpret_cast<void*>(&NativeMeshGetSubMeshCount);
    bind.GetSubMeshName = reinterpret_cast<void*>(&NativeMeshGetSubMeshName);
    bind.GetSubMeshIndexStart = reinterpret_cast<void*>(&NativeMeshGetSubMeshIndexStart);
    bind.GetSubMeshIndexCount = reinterpret_cast<void*>(&NativeMeshGetSubMeshIndexCount);
    bind.GetSubMeshMaterial = reinterpret_cast<void*>(&NativeMeshGetSubMeshMaterial);
    return bind;
}

MaterialBind MaterialBind::Create()
{
    MaterialBind bind;
    bind.Load = reinterpret_cast<void*>(&LoadResourceResult<Material>);
    bind.IsValid = reinterpret_cast<void*>(&IsResourceValid<Material>);
    bind.GetName = reinterpret_cast<void*>(&NativeMaterialGetName);
    bind.GetShader = reinterpret_cast<void*>(&NativeMaterialGetShader);
    bind.SetShader = reinterpret_cast<void*>(&NativeMaterialSetShader);
    bind.HasTexture = reinterpret_cast<void*>(&NativeMaterialHasTexture);
    bind.GetTexture = reinterpret_cast<void*>(&NativeMaterialGetTexture);
    bind.SetTexture = reinterpret_cast<void*>(&NativeMaterialSetTexture);
    bind.ClearTexture = reinterpret_cast<void*>(&NativeMaterialClearTexture);
    bind.HasColor = reinterpret_cast<void*>(&NativeMaterialHasColor);
    bind.GetColor = reinterpret_cast<void*>(&NativeMaterialGetColor);
    bind.SetColor = reinterpret_cast<void*>(&NativeMaterialSetColor);
    bind.ClearColor = reinterpret_cast<void*>(&NativeMaterialClearColor);
    bind.HasFloat = reinterpret_cast<void*>(&NativeMaterialHasFloat);
    bind.GetFloat = reinterpret_cast<void*>(&NativeMaterialGetFloat);
    bind.SetFloat = reinterpret_cast<void*>(&NativeMaterialSetFloat);
    bind.ClearFloat = reinterpret_cast<void*>(&NativeMaterialClearFloat);
    bind.GetRevision = reinterpret_cast<void*>(&NativeMaterialGetRevision);
    return bind;
}

ShaderBind ShaderBind::Create()
{
    ShaderBind bind;
    bind.Load = reinterpret_cast<void*>(&LoadResourceResult<Shader>);
    bind.IsValid = reinterpret_cast<void*>(&IsResourceValid<Shader>);
    bind.GetName = reinterpret_cast<void*>(&NativeShaderGetName);
    bind.GetVertexPath = reinterpret_cast<void*>(&NativeShaderGetVertexPath);
    bind.GetFragmentPath = reinterpret_cast<void*>(&NativeShaderGetFragmentPath);
    bind.GetTextureSlotCount = reinterpret_cast<void*>(&NativeShaderGetTextureSlotCount);
    bind.GetTextureSlotName = reinterpret_cast<void*>(&NativeShaderGetTextureSlotName);
    bind.GetTextureSlotDisplayName = reinterpret_cast<void*>(&NativeShaderGetTextureSlotDisplayName);
    bind.GetTextureSlotDimension = reinterpret_cast<void*>(&NativeShaderGetTextureSlotDimension);
    bind.GetColorSlotCount = reinterpret_cast<void*>(&NativeShaderGetColorSlotCount);
    bind.GetColorSlotName = reinterpret_cast<void*>(&NativeShaderGetColorSlotName);
    bind.GetColorSlotDisplayName = reinterpret_cast<void*>(&NativeShaderGetColorSlotDisplayName);
    bind.GetColorSlotDefault = reinterpret_cast<void*>(&NativeShaderGetColorSlotDefault);
    bind.GetFloatSlotCount = reinterpret_cast<void*>(&NativeShaderGetFloatSlotCount);
    bind.GetFloatSlotName = reinterpret_cast<void*>(&NativeShaderGetFloatSlotName);
    bind.GetFloatSlotDisplayName = reinterpret_cast<void*>(&NativeShaderGetFloatSlotDisplayName);
    bind.GetFloatSlotDefault = reinterpret_cast<void*>(&NativeShaderGetFloatSlotDefault);
    return bind;
}
