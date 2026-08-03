#include "Rendering/GpuResourceManager.h"

#include "Log/Log.h"

#include <cassert>
#include <utility>

namespace
{
    constexpr uint32 VertexFloatCount = 11;
    constexpr uint32 VertexStride = VertexFloatCount * sizeof(float32);
    constexpr usize InvalidStorageIndex = static_cast<usize>(-1);

    //判断字符串前缀
    bool StartsWith(const std::string& text, const std::string& prefix)
    {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    //判断字符串后缀
    bool EndsWith(const std::string& text, const std::string& suffix)
    {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    //按约定从纹理 uniform 名生成是否启用的 bool uniform 名
    std::string CreateTexturePresenceUniformName(const std::string& textureUniformName)
    {
        std::string name = textureUniformName;
        if (StartsWith(name, "u_")) name.erase(0, 2);
        if (EndsWith(name, "Texture")) name.erase(name.size() - 7);
        return "u_Has" + name + "Texture";
    }

    //释放 GPU Mesh 句柄并保留管理器关联信息
    void DeleteGpuResource(RenderBackend* backend, GpuMesh& mesh)
    {
        if (backend)
        {
            backend->DeleteVertexInput(mesh.vertexInput);
            backend->DeleteVertexBuffer(mesh.vertexBuffer);
            backend->DeleteIndexBuffer(mesh.indexBuffer);
        }

        mesh.vertexInput = GpuVertexInputID();
        mesh.vertexBuffer = GpuVertexBufferID();
        mesh.indexBuffer = GpuIndexBufferID();
        mesh.indexCount = 0;
    }

    //释放 GPU 纹理句柄
    void DeleteGpuResource(RenderBackend* backend, GpuTextureID& texture)
    {
        if (backend) backend->DeleteTexture(texture);
        texture = GpuTextureID();
    }

    //释放 GPU 天空盒句柄
    void DeleteGpuResource(RenderBackend* backend, GpuCubeTextureID& skybox)
    {
        if (backend) backend->DeleteCubeTexture(skybox);
        skybox = GpuCubeTextureID();
    }

    //释放 GPU Shader 句柄并保留管理器关联信息
    void DeleteGpuResource(RenderBackend* backend, GpuShader& shader)
    {
        if (backend)
        {
            for (GpuShaderPass& pass : shader.passes)
            {
                backend->DeleteShaderProgram(pass.shaderProgram);
            }
        }

        shader.passes.clear();
    }

    //清空材质解析缓存并保留管理器关联信息
    void DeleteGpuResource(RenderBackend*, GpuMaterial& material)
    {
        material.shader = nullptr;
        material.sourceShader = nullptr;
        material.textureBindings.clear();
        material.colorBindings.clear();
        material.floatBindings.clear();
    }

    //把新包装对象加入活动容器并返回稳定地址
    template<typename T>
    T* AddGpuResource(List<std::unique_ptr<T>>& resources, std::unique_ptr<T> resource)
    {
        resource->storageIndex = resources.size();
        T* result = resource.get();
        resources.push_back(std::move(resource));
        return result;
    }

    //按包装对象记录的索引从活动容器 O(1) 摘除
    template<typename T>
    std::unique_ptr<T> TakeGpuResource(List<std::unique_ptr<T>>& resources, T* resource)
    {
        assert(resource);
        usize index = resource->storageIndex;
        assert(index < resources.size());
        assert(resources[index].get() == resource);

        usize lastIndex = resources.size() - 1;
        std::unique_ptr<T> result = std::move(resources[index]);
        if (index != lastIndex)
        {
            resources[index] = std::move(resources[lastIndex]);
            resources[index]->storageIndex = index;
        }

        resources.pop_back();
        result->storageIndex = InvalidStorageIndex;
        return result;
    }

    //释放一组管理器拥有的 GPU 包装对象
    template<typename T>
    void DeleteGpuResources(RenderBackend* backend, List<std::unique_ptr<T>>& resources)
    {
        for (std::unique_ptr<T>& resource : resources)
        {
            if (resource) DeleteGpuResource(backend, *resource);
        }
        resources.clear();
    }

    //把 CPU Mesh 上传到临时 GPU 状态
    bool UploadMesh(RenderBackend* backend, Mesh* mesh, GpuMesh& uploaded)
    {
        if (mesh->vertices.empty() || mesh->indices.empty())
        {
            Log::Error("GpuResourceManager mesh upload failed: mesh has no vertices or indices.");
            return false;
        }

        //将位置、法线、纹理坐标和切线交错打包为统一顶点布局。
        List<float32> vertexData;
        vertexData.resize(mesh->vertices.size() * VertexFloatCount);
        for (usize index = 0; index < mesh->vertices.size(); ++index)
        {
            const vector3& position = mesh->vertices[index];
            vector3 normal = index < mesh->normals.size() ? mesh->normals[index] : vector3();
            vector2 texcoord = index < mesh->texcoords.size() ? mesh->texcoords[index] : vector2();
            vector3 tangent = index < mesh->tangents.size() ? mesh->tangents[index] : vector3();

            usize offset = index * VertexFloatCount;
            vertexData[offset + 0] = position.x;
            vertexData[offset + 1] = position.y;
            vertexData[offset + 2] = position.z;
            vertexData[offset + 3] = normal.x;
            vertexData[offset + 4] = normal.y;
            vertexData[offset + 5] = normal.z;
            vertexData[offset + 6] = texcoord.x;
            vertexData[offset + 7] = texcoord.y;
            vertexData[offset + 8] = tangent.x;
            vertexData[offset + 9] = tangent.y;
            vertexData[offset + 10] = tangent.z;
        }

        GpuBufferDesc vertexBufferDesc;
        vertexBufferDesc.data = vertexData.data();
        vertexBufferDesc.size = vertexData.size() * sizeof(float32);

        GpuBufferDesc indexBufferDesc;
        indexBufferDesc.data = mesh->indices.data();
        indexBufferDesc.size = mesh->indices.size() * sizeof(uint32);

        uploaded.vertexBuffer = backend->CreateVertexBuffer(vertexBufferDesc);
        uploaded.indexBuffer = backend->CreateIndexBuffer(indexBufferDesc);
        uploaded.indexCount = static_cast<uint32>(mesh->indices.size());

        GpuVertexInputDesc vertexInputDesc;
        vertexInputDesc.vertexBuffer = uploaded.vertexBuffer;
        vertexInputDesc.indexBuffer = uploaded.indexBuffer;
        vertexInputDesc.stride = VertexStride;
        if (uploaded.vertexBuffer.IsValid() && uploaded.indexBuffer.IsValid())
        {
            uploaded.vertexInput = backend->CreateVertexInput(vertexInputDesc);
        }

        if (uploaded.IsValid()) return true;

        DeleteGpuResource(backend, uploaded);
        Log::Error("GpuResourceManager mesh upload failed: backend returned invalid mesh handles.");
        return false;
    }

    //把 CPU Shader 的所有 Pass 编译到临时 GPU 状态
    bool UploadShader(RenderBackend* backend, Shader* shader, GpuShader& uploaded)
    {
        for (const ShaderPass& sourcePass : shader->passes)
        {
            GpuShaderProgramDesc shaderProgramDesc;
            shaderProgramDesc.vertexSource = sourcePass.vertexSource.c_str();
            shaderProgramDesc.fragmentSource = sourcePass.fragmentSource.c_str();

            GpuShaderPass pass;
            pass.name = sourcePass.name;
            pass.state = sourcePass.state;
            pass.shaderProgram = backend->CreateShaderProgram(shaderProgramDesc);
            uploaded.passes.push_back(pass);
            if (!pass.shaderProgram.IsValid()) break;
        }

        if (uploaded.IsValid()) return true;

        DeleteGpuResource(backend, uploaded);
        Log::Error("GpuResourceManager shader upload failed.");
        return false;
    }
}

//注销对象事件并释放仍由管理器持有的 GPU 资源
GpuResourceManager::~GpuResourceManager()
{
    Shutdown();
}

void GpuResourceManager::Initialize(RenderBackend* renderBackend)
{
    backend = renderBackend;
    Object::AddDestroyListener(this);
}

void GpuResourceManager::InvalidateCaches()
{
    //材质先断开依赖，避免随后释放 Shader 和纹理时留下悬空引用。
    for (std::unique_ptr<GpuMaterial>& material : materials)
    {
        if (material->source) material->source->gpuMaterial = nullptr;
        material->source = nullptr;
    }
    DeleteGpuResources(backend, materials);
    DeleteGpuResources(backend, pendingMaterials);

    //清空 Mesh 反向指针后释放活动和延迟资源。
    for (std::unique_ptr<GpuMesh>& mesh : meshes)
    {
        if (mesh->source) mesh->source->gpuMesh = nullptr;
        mesh->source = nullptr;
    }
    DeleteGpuResources(backend, meshes);
    DeleteGpuResources(backend, pendingMeshes);

    //Texture2D 直接保存 GPU ID，失效时同步清除活动索引。
    for (Texture2D* texture : textures)
    {
        if (!texture) continue;
        DeleteGpuResource(backend, texture->gpuTexture);
        texture->gpuTextureStorageIndex = -1;
    }
    textures.clear();
    for (GpuTextureID& texture : pendingTextures) DeleteGpuResource(backend, texture);
    pendingTextures.clear();

    //Skybox 与普通纹理相同，直接清除 CPU 对象中的轻量 ID。
    for (Skybox* skybox : skyboxes)
    {
        if (!skybox) continue;
        DeleteGpuResource(backend, skybox->gpuSkybox);
        skybox->gpuSkyboxStorageIndex = -1;
    }
    skyboxes.clear();
    for (GpuCubeTextureID& skybox : pendingSkyboxes) DeleteGpuResource(backend, skybox);
    pendingSkyboxes.clear();

    //最后释放 Shader，确保所有引用它的材质已经清空。
    for (std::unique_ptr<GpuShader>& shader : shaders)
    {
        if (shader->source) shader->source->gpuShader = nullptr;
        shader->source = nullptr;
    }
    DeleteGpuResources(backend, shaders);
    DeleteGpuResources(backend, pendingShaders);
}

void GpuResourceManager::Shutdown()
{
    Object::RemoveDestroyListener(this);
    InvalidateCaches();
    backend = nullptr;
}

const GpuMesh* GpuResourceManager::GetMesh(Mesh* mesh)
{
    if (!backend || !mesh) return nullptr;

    //正常命中只读取 CPU Mesh 上的稳定指针和 GPU dirty 标记。
    GpuMesh* gpuMesh = mesh->gpuMesh;
    if (gpuMesh && !mesh->IsDirty(MeshDirtyFlags::Gpu)) return gpuMesh;

    //脏更新时原地清理旧句柄，包装对象地址保持不变。
    if (gpuMesh) DeleteGpuResource(backend, *gpuMesh);

    GpuMesh uploaded;
    if (!UploadMesh(backend, mesh, uploaded))
    {
        mesh->MarkDirty(MeshDirtyFlags::Gpu);
        return nullptr;
    }

    if (!gpuMesh)
    {
        std::unique_ptr<GpuMesh> resource = std::make_unique<GpuMesh>();
        resource->source = mesh;
        gpuMesh = AddGpuResource(meshes, std::move(resource));
        mesh->gpuMesh = gpuMesh;
    }

    gpuMesh->vertexInput = uploaded.vertexInput;
    gpuMesh->vertexBuffer = uploaded.vertexBuffer;
    gpuMesh->indexBuffer = uploaded.indexBuffer;
    gpuMesh->indexCount = uploaded.indexCount;
    mesh->ClearDirty(MeshDirtyFlags::Gpu);
    return gpuMesh;
}

GpuTextureID GpuResourceManager::GetTexture(Texture2D* texture)
{
    if (!backend || !texture) return GpuTextureID();

    //Texture2D 直接保存后端无关 ID，命中时只复制一个整数句柄。
    if (texture->gpuTexture.IsValid()) return texture->gpuTexture;

    GpuTextureDesc textureDesc;
    textureDesc.width = texture->width;
    textureDesc.height = texture->height;
    textureDesc.channels = texture->channels;
    textureDesc.pixels = texture->pixels.empty() ? nullptr : texture->pixels.data();

    GpuTextureID textureID = backend->CreateTexture(textureDesc);
    if (!textureID.IsValid())
    {
        Log::Error("GpuResourceManager texture upload failed.");
        return GpuTextureID();
    }

    assert(texture->gpuTextureStorageIndex < 0);
    texture->gpuTexture = textureID;
    texture->gpuTextureStorageIndex = static_cast<int32>(textures.size());
    textures.push_back(texture);
    return textureID;
}

GpuCubeTextureID GpuResourceManager::GetSkybox(Skybox* skybox)
{
    if (!backend || !skybox) return GpuCubeTextureID();

    //Skybox 直接保存立方体纹理 ID，避免每个相机重新解析资源。
    if (skybox->gpuSkybox.IsValid()) return skybox->gpuSkybox;

    Texture2D* faces[6] =
    {
        skybox->right.Get(),
        skybox->left.Get(),
        skybox->top.Get(),
        skybox->bottom.Get(),
        skybox->front.Get(),
        skybox->back.Get(),
    };

    Texture2D* firstFace = faces[0];
    if (!firstFace || firstFace->pixels.empty())
    {
        Log::Error("GpuResourceManager skybox upload failed: first face is missing.");
        return GpuCubeTextureID();
    }

    GpuCubeTextureDesc desc;
    desc.width = firstFace->width;
    desc.height = firstFace->height;
    desc.channels = firstFace->channels;
    for (uint32 face = 0; face < 6; ++face)
    {
        Texture2D* texture = faces[face];
        if (!texture || texture->width != desc.width || texture->height != desc.height || texture->channels != desc.channels || texture->pixels.empty())
        {
            Log::Error("GpuResourceManager skybox upload failed: faces must share size and format.");
            return GpuCubeTextureID();
        }

        desc.faces[face] = texture->pixels.data();
    }

    GpuCubeTextureID cubeTexture = backend->CreateCubeTexture(desc);
    if (!cubeTexture.IsValid())
    {
        Log::Error("GpuResourceManager skybox upload failed.");
        return GpuCubeTextureID();
    }

    assert(skybox->gpuSkyboxStorageIndex < 0);
    skybox->gpuSkybox = cubeTexture;
    skybox->gpuSkyboxStorageIndex = static_cast<int32>(skyboxes.size());
    skyboxes.push_back(skybox);
    return cubeTexture;
}

const GpuShader* GpuResourceManager::GetShader(Shader* shader)
{
    if (!backend || !shader) return nullptr;

    //正常命中只读取 CPU Shader 上的稳定指针和 dirty 标记。
    GpuShader* gpuShader = shader->gpuShader;
    if (gpuShader && !shader->IsDirty()) return gpuShader;

    //Shader 布局或程序变化时，依赖材质也需要在下次使用时重建绑定。
    if (gpuShader)
    {
        MarkMaterialsUsingShaderDirty(shader);
        DeleteGpuResource(backend, *gpuShader);
    }

    GpuShader uploaded;
    if (!UploadShader(backend, shader, uploaded))
    {
        shader->MarkDirty();
        return nullptr;
    }

    if (!gpuShader)
    {
        std::unique_ptr<GpuShader> resource = std::make_unique<GpuShader>();
        resource->source = shader;
        gpuShader = AddGpuResource(shaders, std::move(resource));
        shader->gpuShader = gpuShader;
    }

    gpuShader->passes = std::move(uploaded.passes);
    shader->ClearDirty();
    return gpuShader;
}

const GpuMaterial* GpuResourceManager::GetMaterial(Material* material)
{
    if (!backend || !material) return nullptr;

    //命中时只检查直接缓存指针和 dirty，不解析 Ref、ObjectID 或纹理槽。
    GpuMaterial* gpuMaterial = material->gpuMaterial;
    if (gpuMaterial
        && !material->IsDirty()
        && gpuMaterial->sourceShader
        && !gpuMaterial->sourceShader->IsDirty())
    {
        return gpuMaterial;
    }

    //失败时保持 dirty，避免把半有效缓存当作命中。
    material->MarkDirty();

    //缓存失效后原地清空绑定，包装地址保持稳定。
    if (gpuMaterial) DeleteGpuResource(backend, *gpuMaterial);

    Shader* shader = material->shader.Get();
    if (!shader)
    {
        Log::Error("GpuResourceManager material upload failed: shader is missing.");
        return nullptr;
    }

    const GpuShader* gpuShader = GetShader(shader);
    if (!gpuShader) return nullptr;

    //Shader 更新只标脏依赖材质，不摘除包装；仍重新读取以避免依赖隐式时序。
    gpuMaterial = material->gpuMaterial;

    GpuMaterial uploaded;
    uploaded.shader = gpuShader;
    uploaded.sourceShader = shader;

    //只有首次创建或 dirty 时才解析材质的纹理引用。
    for (const ShaderTextureSlot& slot : shader->textureSlots)
    {
        if (slot.dimension != ShaderTextureDimension::Texture2D) continue;

        GpuMaterialTextureBinding binding;
        binding.uniformName = slot.name;
        binding.presenceUniformName = CreateTexturePresenceUniformName(slot.name);
        Texture2D* texture = material->GetTexture(slot.name);
        binding.sourceTexture = texture;
        if (texture)
        {
            binding.texture = GetTexture(texture);
            binding.hasTexture = binding.texture.IsValid();
        }
        else if (material->HasTexture(slot.name))
        {
            Log::Error(("GpuResourceManager material texture skipped: texture is missing for " + slot.name).c_str());
        }

        uploaded.textureBindings.push_back(binding);
    }

    for (const ShaderColorSlot& slot : shader->colorSlots)
    {
        GpuMaterialColorBinding binding;
        binding.uniformName = slot.name;
        binding.value = material->GetColor(slot.name, slot.defaultValue);
        uploaded.colorBindings.push_back(binding);
    }

    for (const ShaderFloatSlot& slot : shader->floatSlots)
    {
        GpuMaterialFloatBinding binding;
        binding.uniformName = slot.name;
        binding.value = material->GetFloat(slot.name, slot.defaultValue);
        uploaded.floatBindings.push_back(binding);
    }

    if (!gpuMaterial)
    {
        std::unique_ptr<GpuMaterial> resource = std::make_unique<GpuMaterial>();
        resource->source = material;
        gpuMaterial = AddGpuResource(materials, std::move(resource));
        material->gpuMaterial = gpuMaterial;
    }

    gpuMaterial->shader = uploaded.shader;
    gpuMaterial->sourceShader = uploaded.sourceShader;
    gpuMaterial->textureBindings = std::move(uploaded.textureBindings);
    gpuMaterial->colorBindings = std::move(uploaded.colorBindings);
    gpuMaterial->floatBindings = std::move(uploaded.floatBindings);
    material->ClearDirty();
    return gpuMaterial;
}

void GpuResourceManager::MarkMaterialsUsingShaderDirty(Shader* shader)
{
    for (const std::unique_ptr<GpuMaterial>& material : materials)
    {
        if (material->sourceShader == shader && material->source)
        {
            material->source->MarkDirty();
        }
    }
}

void GpuResourceManager::InvalidateMaterialsUsingShader(Shader* shader)
{
    usize index = 0;
    while (index < materials.size())
    {
        GpuMaterial* material = materials[index].get();
        if (material->sourceShader != shader)
        {
            ++index;
            continue;
        }

        assert(material->source);
        QueueMaterialRelease(material->source);
    }
}

void GpuResourceManager::InvalidateMaterialsUsingTexture(Texture2D* texture)
{
    usize index = 0;
    while (index < materials.size())
    {
        GpuMaterial* material = materials[index].get();
        bool usesTexture = false;
        for (const GpuMaterialTextureBinding& binding : material->textureBindings)
        {
            if (binding.sourceTexture == texture)
            {
                usesTexture = true;
                break;
            }
        }

        if (!usesTexture)
        {
            ++index;
            continue;
        }

        assert(material->source);
        QueueMaterialRelease(material->source);
    }
}

void GpuResourceManager::QueueMeshRelease(Mesh* mesh)
{
    if (!mesh || !mesh->gpuMesh) return;

    GpuMesh* resource = mesh->gpuMesh;
    mesh->gpuMesh = nullptr;
    resource->source = nullptr;
    pendingMeshes.push_back(TakeGpuResource(meshes, resource));
}

void GpuResourceManager::QueueShaderRelease(Shader* shader)
{
    if (!shader || !shader->gpuShader) return;

    GpuShader* resource = shader->gpuShader;
    shader->gpuShader = nullptr;
    resource->source = nullptr;
    pendingShaders.push_back(TakeGpuResource(shaders, resource));
}

void GpuResourceManager::QueueMaterialRelease(Material* material)
{
    if (!material || !material->gpuMaterial) return;

    GpuMaterial* resource = material->gpuMaterial;
    material->gpuMaterial = nullptr;
    resource->source = nullptr;
    pendingMaterials.push_back(TakeGpuResource(materials, resource));
}

void GpuResourceManager::QueueTextureRelease(Texture2D* texture)
{
    if (!texture || !texture->gpuTexture.IsValid()) return;

    int32 index = texture->gpuTextureStorageIndex;
    assert(index >= 0 && static_cast<usize>(index) < textures.size());
    assert(textures[index] == texture);

    pendingTextures.push_back(texture->gpuTexture);
    int32 lastIndex = static_cast<int32>(textures.size() - 1);
    if (index != lastIndex)
    {
        Texture2D* moved = textures[lastIndex];
        textures[index] = moved;
        moved->gpuTextureStorageIndex = index;
    }
    textures.pop_back();

    texture->gpuTexture = GpuTextureID();
    texture->gpuTextureStorageIndex = -1;
}

void GpuResourceManager::QueueSkyboxRelease(Skybox* skybox)
{
    if (!skybox || !skybox->gpuSkybox.IsValid()) return;

    int32 index = skybox->gpuSkyboxStorageIndex;
    assert(index >= 0 && static_cast<usize>(index) < skyboxes.size());
    assert(skyboxes[index] == skybox);

    pendingSkyboxes.push_back(skybox->gpuSkybox);
    int32 lastIndex = static_cast<int32>(skyboxes.size() - 1);
    if (index != lastIndex)
    {
        Skybox* moved = skyboxes[lastIndex];
        skyboxes[index] = moved;
        moved->gpuSkyboxStorageIndex = index;
    }
    skyboxes.pop_back();

    skybox->gpuSkybox = GpuCubeTextureID();
    skybox->gpuSkyboxStorageIndex = -1;
}

//记录即将销毁的渲染资源对象
void GpuResourceManager::OnObjectDestroyed(Object* object)
{
    if (!backend || !object) return;

    if (object->Is(Material::StaticType()))
    {
        QueueMaterialRelease(static_cast<Material*>(object));
    }
    else if (object->Is(Mesh::StaticType()))
    {
        QueueMeshRelease(static_cast<Mesh*>(object));
    }
    else if (object->Is(Texture2D::StaticType()))
    {
        Texture2D* texture = static_cast<Texture2D*>(object);
        InvalidateMaterialsUsingTexture(texture);
        QueueTextureRelease(texture);
    }
    else if (object->Is(Skybox::StaticType()))
    {
        QueueSkyboxRelease(static_cast<Skybox*>(object));
    }
    else if (object->Is(Shader::StaticType()))
    {
        Shader* shader = static_cast<Shader*>(object);
        InvalidateMaterialsUsingShader(shader);
        QueueShaderRelease(shader);
    }
}

//释放对象销毁事件对应的 GPU 缓存
void GpuResourceManager::ReleaseDestroyedResources()
{
    if (!backend) return;

    //依赖项先释放，基础 GPU 资源随后释放。
    DeleteGpuResources(backend, pendingMaterials);
    DeleteGpuResources(backend, pendingMeshes);

    for (GpuTextureID& texture : pendingTextures) DeleteGpuResource(backend, texture);
    pendingTextures.clear();

    for (GpuCubeTextureID& skybox : pendingSkyboxes) DeleteGpuResource(backend, skybox);
    pendingSkyboxes.clear();

    DeleteGpuResources(backend, pendingShaders);
}
