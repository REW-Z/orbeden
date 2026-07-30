#include "Runtime/Native/RuntimeResourceBinds.h"

#include "Runtime/Native/NativeCall.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Texture2D.h"
#include "Runtime/ResourceManager.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
    //从 UTF-8 字节创建字符串
    std::string ReadUtf8Text(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return std::string();
        return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(length));
    }

    //把 UTF-8 字符串写入 C# 缓冲区
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

    //读取并转换为资源 Key
    std::string ReadResourceKey(const uint8* text, int32 length)
    {
        return ResourceManager::ToResourceKey(ReadUtf8Text(text, length));
    }

    //获取仍然存活的原生对象
    Object* GetBoundObject(void* pointer)
    {
        Object* object = static_cast<Object*>(pointer);
        if (!object) return nullptr;

        Object* current = Object::FindObjectById(object->GetObjectId());
        return current == object ? object : nullptr;
    }

    //获取指定类型的原生对象
    template<typename TObject>
    TObject* GetBoundObject(void* pointer)
    {
        Object* object = GetBoundObject(pointer);
        return object ? object->Cast<TObject>() : nullptr;
    }

    //查找已加载资源对象
    template<typename TObject>
    TObject* FindResource(const std::string& key)
    {
        std::string resourceKey = ResourceManager::ToResourceKey(key);
        Object* object = ResourceManager::FindLoaded(resourceKey);
        if (!object) object = Object::FindObject(StringId(resourceKey));
        return object ? object->Cast<TObject>() : nullptr;
    }

    //加载资源对象
    template<typename TObject>
    TObject* LoadResource(const uint8* key, int32 length)
    {
        std::string resourceKey = ReadResourceKey(key, length);
        if (resourceKey.empty()) return nullptr;

        if (Object::IsRuntimeInstancePath(resourceKey))
        {
            return FindResource<TObject>(resourceKey);
        }

        return ResourceManager::Load<TObject>(resourceKey);
    }

    //加载目标资源并写入 Ref
    template<typename TObject>
    bool SetLoadedRef(Ref<TObject>& target, const std::string& key)
    {
        if (key.empty())
        {
            target.SetInstanceId(StringId());
            return true;
        }

        TObject* resource = Object::IsRuntimeInstancePath(key)
            ? FindResource<TObject>(key)
            : ResourceManager::Load<TObject>(key);
        if (!resource) return false;

        target.Set(resource);
        return true;
    }

    //把原生数组复制到 C# 缓冲区
    template<typename TValue>
    int32 CopyArray(const List<TValue>& values, TValue* buffer, int32 bufferCount)
    {
        int32 elementCount = static_cast<int32>(values.size());
        if (buffer && bufferCount > 0 && elementCount > 0)
        {
            int32 copyCount = std::min(elementCount, bufferCount);
            std::memcpy(buffer, values.data(), static_cast<usize>(copyCount) * sizeof(TValue));
        }

        return elementCount;
    }

    //获取对象运行时 ID
    int32 ORBEDEN_NATIVE_CALL NativeObjectGetInstanceId(void* pointer)
    {
        Object* object = GetBoundObject(pointer);
        return object ? object->GetObjectId() : 0;
    }

    //读取对象持有的稳定资源 Key。
    int32 ORBEDEN_NATIVE_CALL NativeObjectGetResourceKey(void* pointer, uint8* buffer, int32 bufferSize)
    {
        Object* object = GetBoundObject(pointer);
        return object ? CopyText(object->GetInstanceId().GetPath(), buffer, bufferSize) : 0;
    }

    //判断对象是否存活
    uint8 ORBEDEN_NATIVE_CALL NativeObjectIsAlive(int32 instanceId)
    {
        return Object::IsObjectAlive(instanceId) ? 1 : 0;
    }

    //读取托管包装缓存
    void* ORBEDEN_NATIVE_CALL NativeObjectGetManagedWrapper(void* pointer)
    {
        Object* object = GetBoundObject(pointer);
        return object ? object->GetManagedWrapper() : nullptr;
    }

    //写入托管包装缓存
    void ORBEDEN_NATIVE_CALL NativeObjectSetManagedWrapper(void* pointer, void* wrapper)
    {
        Object* object = GetBoundObject(pointer);
        if (object) object->SetManagedWrapper(wrapper);
    }

    //销毁对象
    uint8 ORBEDEN_NATIVE_CALL NativeObjectDestroy(void* pointer)
    {
        Object* object = GetBoundObject(pointer);
        return Object::DestroyObjectFromBinding(object) ? 1 : 0;
    }

    //释放未使用对象
    uint32 ORBEDEN_NATIVE_CALL NativeObjectUnloadUnusedObjects(const int32* roots, int32 count)
    {
        return Object::UnloadUnusedObjects(roots, count);
    }

    //加载 Mesh 资源
    void* ORBEDEN_NATIVE_CALL NativeMeshLoad(const uint8* key, int32 length)
    {
        return LoadResource<Mesh>(key, length);
    }

    //判断 Mesh 是否有效
    uint8 ORBEDEN_NATIVE_CALL NativeMeshIsValid(void* pointer)
    {
        return GetBoundObject<Mesh>(pointer) ? 1 : 0;
    }

    //读取 Mesh 名称
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetName(void* pointer, uint8* buffer, int32 bufferSize)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return CopyText(mesh ? mesh->name : std::string(), buffer, bufferSize);
    }

    //读取 Mesh 顶点数量
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetVertexCount(void* pointer)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh ? static_cast<int32>(mesh->vertices.size()) : 0;
    }

    //读取 Mesh 索引数量
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetIndexCount(void* pointer)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh ? static_cast<int32>(mesh->indices.size()) : 0;
    }

    //读取 Mesh 子网格数量
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetSubMeshCount(void* pointer)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh ? static_cast<int32>(mesh->subMeshes.size()) : 0;
    }

    //读取 SubMesh
    const SubMesh* GetSubMesh(void* pointer, int32 index)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        if (!mesh || index < 0 || static_cast<usize>(index) >= mesh->subMeshes.size()) return nullptr;
        return &mesh->subMeshes[static_cast<usize>(index)];
    }

    //读取 SubMesh 名称
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetSubMeshName(void* pointer, int32 index, uint8* buffer, int32 bufferSize)
    {
        const SubMesh* subMesh = GetSubMesh(pointer, index);
        return CopyText(subMesh ? subMesh->name : std::string(), buffer, bufferSize);
    }

    //读取 SubMesh 起始索引
    uint32 ORBEDEN_NATIVE_CALL NativeMeshGetSubMeshIndexStart(void* pointer, int32 index)
    {
        const SubMesh* subMesh = GetSubMesh(pointer, index);
        return subMesh ? subMesh->indexStart : 0u;
    }

    //读取 SubMesh 索引数量
    uint32 ORBEDEN_NATIVE_CALL NativeMeshGetSubMeshIndexCount(void* pointer, int32 index)
    {
        const SubMesh* subMesh = GetSubMesh(pointer, index);
        return subMesh ? subMesh->indexCount : 0u;
    }

    //读取 SubMesh 材质
    void* ORBEDEN_NATIVE_CALL NativeMeshGetSubMeshMaterial(void* pointer, int32 index)
    {
        const SubMesh* subMesh = GetSubMesh(pointer, index);
        return subMesh ? subMesh->material.Get() : nullptr;
    }

    //创建运行时 Mesh
    void* ORBEDEN_NATIVE_CALL NativeMeshCreate(const uint8* name, int32 nameLength)
    {
        Mesh* mesh = Object::CreateInstance<Mesh>();
        if (!mesh) return nullptr;

        mesh->name = ReadUtf8Text(name, nameLength);
        if (mesh->name.empty()) mesh->name = "Mesh";
        return mesh;
    }

    //写入 Mesh 名称
    uint8 ORBEDEN_NATIVE_CALL NativeMeshSetName(void* pointer, const uint8* name, int32 nameLength)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        if (!mesh) return 0;

        mesh->name = ReadUtf8Text(name, nameLength);
        mesh->TouchRevision();
        return 1;
    }

    //读取 Mesh 版本
    uint64 ORBEDEN_NATIVE_CALL NativeMeshGetRevision(void* pointer)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh ? mesh->GetRevision() : 0u;
    }

    //读取顶点位置数组
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetVertexPositions(void* pointer, vector3* buffer, int32 bufferCount)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh ? CopyArray(mesh->vertices, buffer, bufferCount) : 0;
    }

    //写入顶点位置数组
    uint8 ORBEDEN_NATIVE_CALL NativeMeshSetVertexPositions(void* pointer, const vector3* data, int32 count)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh && mesh->SetVertexPositions(data, count) ? 1 : 0;
    }

    //读取顶点法线数组
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetVertexNormals(void* pointer, vector3* buffer, int32 bufferCount)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh ? CopyArray(mesh->normals, buffer, bufferCount) : 0;
    }

    //写入顶点法线数组
    uint8 ORBEDEN_NATIVE_CALL NativeMeshSetVertexNormals(void* pointer, const vector3* data, int32 count)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh && mesh->SetVertexNormals(data, count) ? 1 : 0;
    }

    //读取顶点 UV 数组
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetVertexTexcoords(void* pointer, vector2* buffer, int32 bufferCount)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh ? CopyArray(mesh->texcoords, buffer, bufferCount) : 0;
    }

    //写入顶点 UV 数组
    uint8 ORBEDEN_NATIVE_CALL NativeMeshSetVertexTexcoords(void* pointer, const vector2* data, int32 count)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh && mesh->SetVertexTexcoords(data, count) ? 1 : 0;
    }

    //读取顶点切线数组
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetVertexTangents(void* pointer, vector3* buffer, int32 bufferCount)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh ? CopyArray(mesh->tangents, buffer, bufferCount) : 0;
    }

    //写入顶点切线数组
    uint8 ORBEDEN_NATIVE_CALL NativeMeshSetVertexTangents(void* pointer, const vector3* data, int32 count)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh && mesh->SetVertexTangents(data, count) ? 1 : 0;
    }

    //读取索引数组
    int32 ORBEDEN_NATIVE_CALL NativeMeshGetIndexData(void* pointer, uint32* buffer, int32 bufferCount)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh ? CopyArray(mesh->indices, buffer, bufferCount) : 0;
    }

    //写入索引数组
    uint8 ORBEDEN_NATIVE_CALL NativeMeshSetIndexData(void* pointer, const uint32* data, int32 count)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh && mesh->SetIndexData(data, count) ? 1 : 0;
    }

    //清空 Mesh 几何数据
    uint8 ORBEDEN_NATIVE_CALL NativeMeshClearGeometry(void* pointer)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        if (!mesh) return 0;

        mesh->ClearGeometry();
        return 1;
    }

    //重新计算 Mesh 法线
    uint8 ORBEDEN_NATIVE_CALL NativeMeshRefreshNormals(void* pointer)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh && mesh->RefreshNormals() ? 1 : 0;
    }

    //调整 Mesh 子网格数量
    uint8 ORBEDEN_NATIVE_CALL NativeMeshResizeSubMeshes(void* pointer, int32 count)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        return mesh && mesh->ResizeSubMeshes(count) ? 1 : 0;
    }

    //配置 Mesh 子网格
    uint8 ORBEDEN_NATIVE_CALL NativeMeshConfigureSubMesh(
        void* pointer,
        int32 index,
        const uint8* name,
        int32 nameLength,
        uint32 indexStart,
        uint32 indexCount,
        void* materialPointer)
    {
        Mesh* mesh = GetBoundObject<Mesh>(pointer);
        Material* material = materialPointer ? GetBoundObject<Material>(materialPointer) : nullptr;
        if (!mesh || (materialPointer && !material)) return 0;
        if (index < 0 || static_cast<usize>(index) >= mesh->subMeshes.size()) return 0;

        usize start = static_cast<usize>(indexStart);
        usize count = static_cast<usize>(indexCount);
        if (start > mesh->indices.size() || count > mesh->indices.size() - start) return 0;

        return mesh->ConfigureSubMesh(index, ReadUtf8Text(name, nameLength), indexStart, indexCount, material) ? 1 : 0;
    }

    //加载 Material 资源
    void* ORBEDEN_NATIVE_CALL NativeMaterialLoad(const uint8* key, int32 length)
    {
        return LoadResource<Material>(key, length);
    }

    //判断 Material 是否有效
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialIsValid(void* pointer)
    {
        return GetBoundObject<Material>(pointer) ? 1 : 0;
    }

    //读取 Material 名称
    int32 ORBEDEN_NATIVE_CALL NativeMaterialGetName(void* pointer, uint8* buffer, int32 bufferSize)
    {
        Material* material = GetBoundObject<Material>(pointer);
        return CopyText(material ? material->name : std::string(), buffer, bufferSize);
    }

    //读取 Material Shader
    void* ORBEDEN_NATIVE_CALL NativeMaterialGetShader(void* pointer)
    {
        Material* material = GetBoundObject<Material>(pointer);
        return material ? material->shader.Get() : nullptr;
    }

    //设置 Material Shader
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialSetShader(void* pointer, void* shaderPointer)
    {
        Material* material = GetBoundObject<Material>(pointer);
        Shader* shader = shaderPointer ? GetBoundObject<Shader>(shaderPointer) : nullptr;
        if (!material || (shaderPointer && !shader)) return 0;

        material->shader.Set(shader);
        material->TouchRevision();
        return 1;
    }

    //判断材质纹理槽是否存在
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialHasTexture(void* pointer, const uint8* slotName, int32 slotLength)
    {
        Material* material = GetBoundObject<Material>(pointer);
        return material && material->HasTexture(ReadUtf8Text(slotName, slotLength)) ? 1 : 0;
    }

    //读取材质纹理 Key
    int32 ORBEDEN_NATIVE_CALL NativeMaterialGetTexture(void* pointer, const uint8* slotName, int32 slotLength, uint8* buffer, int32 bufferSize)
    {
        Material* material = GetBoundObject<Material>(pointer);
        if (!material) return 0;

        std::string slot = ReadUtf8Text(slotName, slotLength);
        for (const MaterialTextureSlot& value : material->textureSlots)
        {
            if (value.name == slot) return CopyText(value.texture.GetInstanceId().GetPath(), buffer, bufferSize);
        }

        return 0;
    }

    //设置材质纹理 Key
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialSetTexture(void* pointer, const uint8* slotName, int32 slotLength, const uint8* textureKey, int32 textureLength)
    {
        Material* material = GetBoundObject<Material>(pointer);
        if (!material) return 0;

        std::string textureResourceKey = ReadResourceKey(textureKey, textureLength);
        if (!textureResourceKey.empty() && !LoadResource<Texture2D>(textureKey, textureLength)) return 0;

        material->SetTexture(ReadUtf8Text(slotName, slotLength), StringId(textureResourceKey));
        return 1;
    }

    //清除材质纹理槽
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialClearTexture(void* pointer, const uint8* slotName, int32 slotLength)
    {
        Material* material = GetBoundObject<Material>(pointer);
        if (!material) return 0;

        material->ClearTexture(ReadUtf8Text(slotName, slotLength));
        return 1;
    }

    //判断材质颜色槽是否存在
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialHasColor(void* pointer, const uint8* slotName, int32 slotLength)
    {
        Material* material = GetBoundObject<Material>(pointer);
        return material && material->HasColor(ReadUtf8Text(slotName, slotLength)) ? 1 : 0;
    }

    //读取材质颜色槽
    color ORBEDEN_NATIVE_CALL NativeMaterialGetColor(void* pointer, const uint8* slotName, int32 slotLength, color defaultValue)
    {
        Material* material = GetBoundObject<Material>(pointer);
        return material ? material->GetColor(ReadUtf8Text(slotName, slotLength), defaultValue) : defaultValue;
    }

    //设置材质颜色槽
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialSetColor(void* pointer, const uint8* slotName, int32 slotLength, color value)
    {
        Material* material = GetBoundObject<Material>(pointer);
        if (!material) return 0;

        material->SetColor(ReadUtf8Text(slotName, slotLength), value);
        return 1;
    }

    //清除材质颜色槽
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialClearColor(void* pointer, const uint8* slotName, int32 slotLength)
    {
        Material* material = GetBoundObject<Material>(pointer);
        if (!material) return 0;

        material->ClearColor(ReadUtf8Text(slotName, slotLength));
        return 1;
    }

    //判断材质浮点槽是否存在
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialHasFloat(void* pointer, const uint8* slotName, int32 slotLength)
    {
        Material* material = GetBoundObject<Material>(pointer);
        return material && material->HasFloat(ReadUtf8Text(slotName, slotLength)) ? 1 : 0;
    }

    //读取材质浮点槽
    float32 ORBEDEN_NATIVE_CALL NativeMaterialGetFloat(void* pointer, const uint8* slotName, int32 slotLength, float32 defaultValue)
    {
        Material* material = GetBoundObject<Material>(pointer);
        return material ? material->GetFloat(ReadUtf8Text(slotName, slotLength), defaultValue) : defaultValue;
    }

    //设置材质浮点槽
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialSetFloat(void* pointer, const uint8* slotName, int32 slotLength, float32 value)
    {
        Material* material = GetBoundObject<Material>(pointer);
        if (!material) return 0;

        material->SetFloat(ReadUtf8Text(slotName, slotLength), value);
        return 1;
    }

    //清除材质浮点槽
    uint8 ORBEDEN_NATIVE_CALL NativeMaterialClearFloat(void* pointer, const uint8* slotName, int32 slotLength)
    {
        Material* material = GetBoundObject<Material>(pointer);
        if (!material) return 0;

        material->ClearFloat(ReadUtf8Text(slotName, slotLength));
        return 1;
    }

    //读取材质版本
    uint64 ORBEDEN_NATIVE_CALL NativeMaterialGetRevision(void* pointer)
    {
        Material* material = GetBoundObject<Material>(pointer);
        return material ? material->GetRevision() : 0u;
    }

    //创建运行时 Material
    void* ORBEDEN_NATIVE_CALL NativeMaterialCreate(const uint8* name, int32 nameLength, void* shaderPointer)
    {
        Shader* shader = shaderPointer ? GetBoundObject<Shader>(shaderPointer) : nullptr;
        if (shaderPointer && !shader) return nullptr;

        Material* material = Object::CreateInstance<Material>();
        if (!material) return nullptr;

        material->name = ReadUtf8Text(name, nameLength);
        if (material->name.empty()) material->name = "Material";
        material->shader.Set(shader);
        material->TouchRevision();
        return material;
    }

    //加载 Shader 资源
    void* ORBEDEN_NATIVE_CALL NativeShaderLoad(const uint8* key, int32 length)
    {
        return LoadResource<Shader>(key, length);
    }

    //判断 Shader 是否有效
    uint8 ORBEDEN_NATIVE_CALL NativeShaderIsValid(void* pointer)
    {
        return GetBoundObject<Shader>(pointer) ? 1 : 0;
    }

    //读取 Shader 名称
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetName(void* pointer, uint8* buffer, int32 bufferSize)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        return CopyText(shader ? shader->name : std::string(), buffer, bufferSize);
    }

    //读取 Shader 顶点源码路径
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetVertexPath(void* pointer, uint8* buffer, int32 bufferSize)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        return CopyText(shader ? shader->vertexPath : std::string(), buffer, bufferSize);
    }

    //读取 Shader 片元源码路径
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetFragmentPath(void* pointer, uint8* buffer, int32 bufferSize)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        return CopyText(shader ? shader->fragmentPath : std::string(), buffer, bufferSize);
    }

    //读取 Shader 纹理槽数量
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetTextureSlotCount(void* pointer)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        return shader ? static_cast<int32>(shader->textureSlots.size()) : 0;
    }

    //读取 Shader 颜色槽数量
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetColorSlotCount(void* pointer)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        return shader ? static_cast<int32>(shader->colorSlots.size()) : 0;
    }

    //读取 Shader 浮点槽数量
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetFloatSlotCount(void* pointer)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        return shader ? static_cast<int32>(shader->floatSlots.size()) : 0;
    }

    //读取 Shader 纹理槽
    const ShaderTextureSlot* GetShaderTextureSlot(void* pointer, int32 index)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        if (!shader || index < 0 || static_cast<usize>(index) >= shader->textureSlots.size()) return nullptr;
        return &shader->textureSlots[static_cast<usize>(index)];
    }

    //读取 Shader 颜色槽
    const ShaderColorSlot* GetShaderColorSlot(void* pointer, int32 index)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        if (!shader || index < 0 || static_cast<usize>(index) >= shader->colorSlots.size()) return nullptr;
        return &shader->colorSlots[static_cast<usize>(index)];
    }

    //读取 Shader 浮点槽
    const ShaderFloatSlot* GetShaderFloatSlot(void* pointer, int32 index)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        if (!shader || index < 0 || static_cast<usize>(index) >= shader->floatSlots.size()) return nullptr;
        return &shader->floatSlots[static_cast<usize>(index)];
    }

    //读取 Shader 纹理槽名称
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetTextureSlotName(void* pointer, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderTextureSlot* slot = GetShaderTextureSlot(pointer, index);
        return CopyText(slot ? slot->name : std::string(), buffer, bufferSize);
    }

    //读取 Shader 纹理槽显示名
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetTextureSlotDisplayName(void* pointer, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderTextureSlot* slot = GetShaderTextureSlot(pointer, index);
        return CopyText(slot ? slot->displayName : std::string(), buffer, bufferSize);
    }

    //读取 Shader 纹理槽维度
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetTextureSlotDimension(void* pointer, int32 index)
    {
        const ShaderTextureSlot* slot = GetShaderTextureSlot(pointer, index);
        return slot ? static_cast<int32>(slot->dimension) : 0;
    }

    //读取 Shader 颜色槽名称
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetColorSlotName(void* pointer, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderColorSlot* slot = GetShaderColorSlot(pointer, index);
        return CopyText(slot ? slot->name : std::string(), buffer, bufferSize);
    }

    //读取 Shader 颜色槽显示名
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetColorSlotDisplayName(void* pointer, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderColorSlot* slot = GetShaderColorSlot(pointer, index);
        return CopyText(slot ? slot->displayName : std::string(), buffer, bufferSize);
    }

    //读取 Shader 颜色槽默认值
    color ORBEDEN_NATIVE_CALL NativeShaderGetColorSlotDefault(void* pointer, int32 index)
    {
        const ShaderColorSlot* slot = GetShaderColorSlot(pointer, index);
        return slot ? slot->defaultValue : color();
    }

    //读取 Shader 浮点槽名称
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetFloatSlotName(void* pointer, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderFloatSlot* slot = GetShaderFloatSlot(pointer, index);
        return CopyText(slot ? slot->name : std::string(), buffer, bufferSize);
    }

    //读取 Shader 浮点槽显示名
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetFloatSlotDisplayName(void* pointer, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderFloatSlot* slot = GetShaderFloatSlot(pointer, index);
        return CopyText(slot ? slot->displayName : std::string(), buffer, bufferSize);
    }

    //读取 Shader 浮点槽默认值
    float32 ORBEDEN_NATIVE_CALL NativeShaderGetFloatSlotDefault(void* pointer, int32 index)
    {
        const ShaderFloatSlot* slot = GetShaderFloatSlot(pointer, index);
        return slot ? slot->defaultValue : 0.0f;
    }

    //读取 Shader Pass
    const ShaderPass* GetShaderPass(void* pointer, int32 index)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        return shader && index >= 0 ? shader->GetPass(static_cast<uint32>(index)) : nullptr;
    }

    //读取 Shader Pass 数量
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetPassCount(void* pointer)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        return shader ? static_cast<int32>(shader->GetPassCount()) : 0;
    }

    //读取 Shader Pass 名称
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetPassName(void* pointer, int32 index, uint8* buffer, int32 bufferSize)
    {
        const ShaderPass* pass = GetShaderPass(pointer, index);
        return CopyText(pass ? pass->name : std::string(), buffer, bufferSize);
    }

    //读取 Shader Pass 深度测试状态
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetPassDepthTest(void* pointer, int32 index)
    {
        const ShaderPass* pass = GetShaderPass(pointer, index);
        return pass ? static_cast<int32>(pass->state.depthTest) : 0;
    }

    //读取 Shader Pass 深度写入状态
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetPassDepthWrite(void* pointer, int32 index)
    {
        const ShaderPass* pass = GetShaderPass(pointer, index);
        return pass ? static_cast<int32>(pass->state.depthWrite) : 0;
    }

    //读取 Shader Pass 混合状态
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetPassBlend(void* pointer, int32 index)
    {
        const ShaderPass* pass = GetShaderPass(pointer, index);
        return pass ? static_cast<int32>(pass->state.blend) : 0;
    }

    //读取 Shader Pass 剔除状态
    int32 ORBEDEN_NATIVE_CALL NativeShaderGetPassCull(void* pointer, int32 index)
    {
        const ShaderPass* pass = GetShaderPass(pointer, index);
        return pass ? static_cast<int32>(pass->state.cull) : 0;
    }

    //创建运行时 Shader
    void* ORBEDEN_NATIVE_CALL NativeShaderCreateFromSource(
        const uint8* name,
        int32 nameLength,
        const uint8* vertexSource,
        int32 vertexLength,
        const uint8* fragmentSource,
        int32 fragmentLength)
    {
        Shader* shader = Object::CreateInstance<Shader>();
        if (!shader) return nullptr;

        shader->name = ReadUtf8Text(name, nameLength);
        if (shader->name.empty()) shader->name = "Shader";
        shader->vertexPath = "runtime://shader/" + shader->name + "/vertex";
        shader->fragmentPath = "runtime://shader/" + shader->name + "/fragment";
        shader->ReplaceSource(ReadUtf8Text(vertexSource, vertexLength), ReadUtf8Text(fragmentSource, fragmentLength));
        return shader;
    }

    //替换 Shader 源码
    uint8 ORBEDEN_NATIVE_CALL NativeShaderReplaceSource(
        void* pointer,
        const uint8* vertexSource,
        int32 vertexLength,
        const uint8* fragmentSource,
        int32 fragmentLength)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        if (!shader) return 0;

        shader->ReplaceSource(ReadUtf8Text(vertexSource, vertexLength), ReadUtf8Text(fragmentSource, fragmentLength));
        return 1;
    }

    //读取 Shader 版本
    uint64 ORBEDEN_NATIVE_CALL NativeShaderGetRevision(void* pointer)
    {
        Shader* shader = GetBoundObject<Shader>(pointer);
        return shader ? shader->GetRevision() : 0u;
    }
}

ObjectBind ObjectBind::Create()
{
    ObjectBind bind;
    bind.GetInstanceId = reinterpret_cast<void*>(&NativeObjectGetInstanceId);
    bind.IsAlive = reinterpret_cast<void*>(&NativeObjectIsAlive);
    bind.GetManagedWrapper = reinterpret_cast<void*>(&NativeObjectGetManagedWrapper);
    bind.SetManagedWrapper = reinterpret_cast<void*>(&NativeObjectSetManagedWrapper);
    bind.Destroy = reinterpret_cast<void*>(&NativeObjectDestroy);
    bind.UnloadUnusedObjects = reinterpret_cast<void*>(&NativeObjectUnloadUnusedObjects);
    return bind;
}

ObjectExtensionBind ObjectExtensionBind::Create()
{
    ObjectExtensionBind bind;
    bind.GetResourceKey = reinterpret_cast<void*>(&NativeObjectGetResourceKey);
    return bind;
}

MeshBind MeshBind::Create()
{
    MeshBind bind;
    bind.Load = reinterpret_cast<void*>(&NativeMeshLoad);
    bind.IsValid = reinterpret_cast<void*>(&NativeMeshIsValid);
    bind.GetName = reinterpret_cast<void*>(&NativeMeshGetName);
    bind.GetVertexCount = reinterpret_cast<void*>(&NativeMeshGetVertexCount);
    bind.GetIndexCount = reinterpret_cast<void*>(&NativeMeshGetIndexCount);
    bind.GetSubMeshCount = reinterpret_cast<void*>(&NativeMeshGetSubMeshCount);
    bind.GetSubMeshName = reinterpret_cast<void*>(&NativeMeshGetSubMeshName);
    bind.GetSubMeshIndexStart = reinterpret_cast<void*>(&NativeMeshGetSubMeshIndexStart);
    bind.GetSubMeshIndexCount = reinterpret_cast<void*>(&NativeMeshGetSubMeshIndexCount);
    bind.GetSubMeshMaterial = reinterpret_cast<void*>(&NativeMeshGetSubMeshMaterial);
    bind.CreateInstance = reinterpret_cast<void*>(&NativeMeshCreate);
    bind.SetName = reinterpret_cast<void*>(&NativeMeshSetName);
    bind.GetRevision = reinterpret_cast<void*>(&NativeMeshGetRevision);
    bind.GetVertexPositions = reinterpret_cast<void*>(&NativeMeshGetVertexPositions);
    bind.SetVertexPositions = reinterpret_cast<void*>(&NativeMeshSetVertexPositions);
    bind.GetVertexNormals = reinterpret_cast<void*>(&NativeMeshGetVertexNormals);
    bind.SetVertexNormals = reinterpret_cast<void*>(&NativeMeshSetVertexNormals);
    bind.GetVertexTexcoords = reinterpret_cast<void*>(&NativeMeshGetVertexTexcoords);
    bind.SetVertexTexcoords = reinterpret_cast<void*>(&NativeMeshSetVertexTexcoords);
    bind.GetVertexTangents = reinterpret_cast<void*>(&NativeMeshGetVertexTangents);
    bind.SetVertexTangents = reinterpret_cast<void*>(&NativeMeshSetVertexTangents);
    bind.GetIndexData = reinterpret_cast<void*>(&NativeMeshGetIndexData);
    bind.SetIndexData = reinterpret_cast<void*>(&NativeMeshSetIndexData);
    bind.ClearGeometry = reinterpret_cast<void*>(&NativeMeshClearGeometry);
    bind.RefreshNormals = reinterpret_cast<void*>(&NativeMeshRefreshNormals);
    bind.ResizeSubMeshes = reinterpret_cast<void*>(&NativeMeshResizeSubMeshes);
    bind.ConfigureSubMesh = reinterpret_cast<void*>(&NativeMeshConfigureSubMesh);
    return bind;
}

MaterialBind MaterialBind::Create()
{
    MaterialBind bind;
    bind.Load = reinterpret_cast<void*>(&NativeMaterialLoad);
    bind.IsValid = reinterpret_cast<void*>(&NativeMaterialIsValid);
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
    bind.CreateInstance = reinterpret_cast<void*>(&NativeMaterialCreate);
    return bind;
}

ShaderBind ShaderBind::Create()
{
    ShaderBind bind;
    bind.Load = reinterpret_cast<void*>(&NativeShaderLoad);
    bind.IsValid = reinterpret_cast<void*>(&NativeShaderIsValid);
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
    bind.GetPassCount = reinterpret_cast<void*>(&NativeShaderGetPassCount);
    bind.GetPassName = reinterpret_cast<void*>(&NativeShaderGetPassName);
    bind.GetPassDepthTest = reinterpret_cast<void*>(&NativeShaderGetPassDepthTest);
    bind.GetPassDepthWrite = reinterpret_cast<void*>(&NativeShaderGetPassDepthWrite);
    bind.GetPassBlend = reinterpret_cast<void*>(&NativeShaderGetPassBlend);
    bind.GetPassCull = reinterpret_cast<void*>(&NativeShaderGetPassCull);
    bind.CreateFromSource = reinterpret_cast<void*>(&NativeShaderCreateFromSource);
    bind.ReplaceSource = reinterpret_cast<void*>(&NativeShaderReplaceSource);
    bind.GetRevision = reinterpret_cast<void*>(&NativeShaderGetRevision);
    return bind;
}
