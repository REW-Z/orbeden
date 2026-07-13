#include "Rendering/GpuResourceManager.h"

#include "Log/Log.h"

namespace
{
    constexpr uint32 VertexFloatCount = 11;
    constexpr uint32 VertexStride = VertexFloatCount * sizeof(float32);

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

    //释放普通缓存数据
    template<typename T>
    void DeleteGpuResource(RenderBackend*, T& resource)
    {
        resource = T();
    }

    //释放 GPU Mesh 句柄
    void DeleteGpuResource(RenderBackend* backend, GpuMesh& mesh)
    {
        if (!backend) return;

        backend->DeleteVertexInput(mesh.vertexInput);
        backend->DeleteVertexBuffer(mesh.vertexBuffer);
        backend->DeleteIndexBuffer(mesh.indexBuffer);
        mesh = GpuMesh();
    }

    //释放 GPU 纹理句柄
    void DeleteGpuResource(RenderBackend* backend, GpuTextureID& texture)
    {
        if (!backend) return;

        backend->DeleteTexture(texture);
        texture = GpuTextureID();
    }

    //释放 GPU 天空盒句柄
    void DeleteGpuResource(RenderBackend* backend, GpuCubeTextureID& skybox)
    {
        if (!backend) return;

        backend->DeleteCubeTexture(skybox);
        skybox = GpuCubeTextureID();
    }

    //释放 GPU Shader 句柄
    void DeleteGpuResource(RenderBackend* backend, GpuShader& shader)
    {
        if (!backend) return;

        backend->DeleteShaderProgram(shader.shaderProgram);
        shader = GpuShader();
    }

    //释放整个 GPU 缓存
    template<typename TCache>
    void DeleteGpuCache(RenderBackend* backend, TCache& cache)
    {
        //逐条释放后端资源，再清空缓存容器。
        for (auto& pair : cache)
        {
            DeleteGpuResource(backend, pair.second.resource);
        }

        cache.clear();
    }

    //清理 CPU 对象已经销毁的 GPU 缓存项
    template<typename TCache>
    void CollectUnusedCache(RenderBackend* backend, TCache& cache)
    {
        //遍历时直接擦除已失效条目，保持迭代器有效。
        for (auto it = cache.begin(); it != cache.end();)
        {
            if (!Object::FindObject(StringId(it->second.sourceKey)))
            {
                DeleteGpuResource(backend, it->second.resource);
                it = cache.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

void GpuResourceManager::Initialize(RenderBackend* renderBackend)
{
    //记录后端引用，后续所有缓存资源都通过该后端创建和释放。
    backend = renderBackend;
}

void GpuResourceManager::Shutdown()
{
    //先释放依赖其他资源的材质，再释放基础网格、纹理和 shader。
    DeleteGpuCache(backend, materials);
    DeleteGpuCache(backend, meshes);
    DeleteGpuCache(backend, textures);
    DeleteGpuCache(backend, skyboxes);
    DeleteGpuCache(backend, shaders);
    //清空后端引用，防止关闭后继续访问 GPU 资源。
    backend = nullptr;
}

const GpuMesh* GpuResourceManager::GetMesh(Mesh* mesh)
{
    //没有后端或 CPU 网格时无法进行上传。
    if (!backend || !mesh) return nullptr;

    //命中同版本缓存时直接复用；版本变化则先释放旧资源。
    auto it = meshes.find(mesh);
    uint64 meshRevision = mesh->GetRevision();
    if (it != meshes.end())
    {
        if (it->second.resource.meshRevision == meshRevision) return &it->second.resource;

        DeleteGpuResource(backend, it->second.resource);
        meshes.erase(it);
    }

    //空网格不具备可上传的顶点和索引数据。
    if (mesh->vertices.empty() || mesh->indices.empty())
    {
        Log::Error("GpuResourceManager mesh upload failed: mesh has no vertices or indices.");
        return nullptr;
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

    //准备顶点和索引缓冲描述，数据仍由 CPU 网格临时持有。
    GpuBufferDesc vertexBufferDesc;
    vertexBufferDesc.data = vertexData.data();
    vertexBufferDesc.size = vertexData.size() * sizeof(float32);

    GpuBufferDesc indexBufferDesc;
    indexBufferDesc.data = mesh->indices.data();
    indexBufferDesc.size = mesh->indices.size() * sizeof(uint32);

    //创建 GPU 缓冲和顶点输入对象，并记录版本及来源路径。
    GpuMesh gpuMesh;
    gpuMesh.vertexBuffer = backend->CreateVertexBuffer(vertexBufferDesc);
    gpuMesh.indexBuffer = backend->CreateIndexBuffer(indexBufferDesc);
    gpuMesh.indexCount = static_cast<uint32>(mesh->indices.size());
    gpuMesh.meshRevision = meshRevision;
    gpuMesh.sourceKey = mesh->GetInstanceId().GetPath();

    GpuVertexInputDesc vertexInputDesc;
    vertexInputDesc.vertexBuffer = gpuMesh.vertexBuffer;
    vertexInputDesc.indexBuffer = gpuMesh.indexBuffer;
    vertexInputDesc.stride = VertexStride;
    gpuMesh.vertexInput = backend->CreateVertexInput(vertexInputDesc);

    //任一句柄创建失败时释放已创建的部分资源。
    if (!gpuMesh.IsValid())
    {
        Log::Error("GpuResourceManager mesh upload failed: backend returned invalid mesh handles.");
        backend->DeleteVertexInput(gpuMesh.vertexInput);
        backend->DeleteVertexBuffer(gpuMesh.vertexBuffer);
        backend->DeleteIndexBuffer(gpuMesh.indexBuffer);
        return nullptr;
    }

    //上传成功后写入缓存，后续绘制直接返回缓存条目。
    meshes[mesh] = { gpuMesh, gpuMesh.sourceKey };
    return &meshes[mesh].resource;
}

GpuTextureID GpuResourceManager::GetTexture(Texture2D* texture)
{
    //没有后端或 CPU 纹理时无法创建 GPU 纹理。
    if (!backend || !texture) return GpuTextureID();

    //普通纹理当前按 CPU 对象缓存，命中后直接复用。
    auto it = textures.find(texture);
    if (it != textures.end()) return it->second.resource;

    //使用纹理自身尺寸、通道和像素数据构造上传描述。
    GpuTextureDesc textureDesc;
    textureDesc.width = texture->width;
    textureDesc.height = texture->height;
    textureDesc.channels = texture->channels;
    textureDesc.pixels = texture->pixels.empty() ? nullptr : texture->pixels.data();

    //创建 GPU 纹理，失败时不写入无效缓存项。
    GpuTextureID textureID = backend->CreateTexture(textureDesc);
    if (!textureID.IsValid())
    {
        Log::Error("GpuResourceManager texture upload failed.");
        return GpuTextureID();
    }

    //记录资源来源路径，用于后续清理无主缓存。
    textures[texture] = { textureID, texture->GetInstanceId().GetPath() };
    return textureID;
}

GpuCubeTextureID GpuResourceManager::GetSkybox(Skybox* skybox)
{
    //没有后端或天空盒资源时无法创建立方体纹理。
    if (!backend || !skybox) return GpuCubeTextureID();

    //天空盒按 CPU 对象缓存，避免每个相机重复上传六个面。
    auto it = skyboxes.find(skybox);
    if (it != skyboxes.end()) return it->second.resource;

    //按后端约定顺序收集立方体六个面。
    Texture2D* faces[6] =
    {
        skybox->right.Get(),
        skybox->left.Get(),
        skybox->top.Get(),
        skybox->bottom.Get(),
        skybox->front.Get(),
        skybox->back.Get(),
    };

    //先用第一个面确定立方体的尺寸和格式基准。
    Texture2D* firstFace = faces[0];
    if (!firstFace || firstFace->pixels.empty())
    {
        Log::Error("GpuResourceManager skybox upload failed: first face is missing.");
        return GpuCubeTextureID();
    }

    //验证六个面尺寸、通道和像素数据一致，再填充上传描述。
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

    //创建 GPU 立方体纹理，失败时不保留无效条目。
    GpuCubeTextureID cubeTexture = backend->CreateCubeTexture(desc);
    if (!cubeTexture.IsValid())
    {
        Log::Error("GpuResourceManager skybox upload failed.");
        return GpuCubeTextureID();
    }

    //保存天空盒资源路径，供缓存回收流程检测对象是否仍然存在。
    skyboxes[skybox] = { cubeTexture, skybox->GetInstanceId().GetPath() };
    return cubeTexture;
}

const GpuShader* GpuResourceManager::GetShader(Shader* shader)
{
    //没有后端或 CPU shader 时无法创建 GPU program。
    if (!backend || !shader) return nullptr;

    //命中同版本缓存时直接复用；shader 版本变化则释放旧 program。
    auto it = shaders.find(shader);
    uint64 shaderRevision = shader->GetRevision();
    if (it != shaders.end())
    {
        if (it->second.resource.shaderRevision == shaderRevision) return &it->second.resource;

        DeleteGpuResource(backend, it->second.resource);
        shaders.erase(it);
    }

    //使用 CPU shader 的顶点和片元源码创建 GPU program。
    GpuShaderProgramDesc shaderProgramDesc;
    shaderProgramDesc.vertexSource = shader->vertexSource.c_str();
    shaderProgramDesc.fragmentSource = shader->fragmentSource.c_str();

    //创建 program 并记录版本号和资源来源。
    GpuShader gpuShader;
    gpuShader.shaderProgram = backend->CreateShaderProgram(shaderProgramDesc);
    gpuShader.shaderRevision = shaderRevision;
    gpuShader.sourceKey = shader->GetInstanceId().GetPath();
    //编译失败时不写入无效缓存项。
    if (!gpuShader.IsValid())
    {
        Log::Error("GpuResourceManager shader upload failed.");
        return nullptr;
    }

    //上传成功后保存 program 缓存。
    shaders[shader] = { gpuShader, gpuShader.sourceKey };
    return &shaders[shader].resource;
}

const GpuMaterial* GpuResourceManager::GetMaterial(Material* material)
{
    //没有后端或 CPU 材质时无法建立材质绑定。
    if (!backend || !material) return nullptr;

    //材质必须先解析出 shader，后续才能生成 uniform 绑定。
    Shader* shader = material->shader.Get();
    if (!shader)
    {
        Log::Error("GpuResourceManager material upload failed: shader is missing.");
        return nullptr;
    }

    //检查已缓存材质的 shader、版本和纹理引用是否仍然一致。
    auto it = materials.find(material);
    uint64 shaderRevision = shader->GetRevision();
    bool textureBindingsCurrent = true;
    if (it != materials.end())
    {
        for (const GpuMaterialTextureBinding& binding : it->second.resource.textureBindings)
        {
            if (material->GetTexture(binding.uniformName) != binding.sourceTexture)
            {
                textureBindingsCurrent = false;
                break;
            }
        }
    }
    //所有依赖仍然有效时直接复用完整材质缓存。
    if (it != materials.end()
        && it->second.resource.sourceShader == shader
        && it->second.resource.materialRevision == material->GetRevision()
        && it->second.resource.shaderRevision == shaderRevision
        && textureBindingsCurrent)
    {
        return &it->second.resource;
    }

    //创建新的材质记录，并同步 CPU 材质和 shader 的版本信息。
    GpuMaterial gpuMaterial;
    gpuMaterial.sourceShader = shader;
    gpuMaterial.sourceKey = material->GetInstanceId().GetPath();
    gpuMaterial.materialRevision = material->GetRevision();
    gpuMaterial.shaderRevision = shaderRevision;
    //材质复用或创建 shader 的 GPU program。
    const GpuShader* gpuShader = GetShader(shader);
    if (!gpuShader || !gpuShader->IsValid()) return nullptr;
    gpuMaterial.shader = *gpuShader;

    //按 shader 声明生成 2D 纹理槽和对应的存在标记。
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

        gpuMaterial.textureBindings.push_back(binding);
    }

    //读取材质颜色值；未设置时使用 shader 默认值。
    for (const ShaderColorSlot& slot : shader->colorSlots)
    {
        GpuMaterialColorBinding binding;
        binding.uniformName = slot.name;
        binding.value = material->GetColor(slot.name, slot.defaultValue);
        gpuMaterial.colorBindings.push_back(binding);
    }

    //读取材质浮点值；未设置时使用 shader 默认值。
    for (const ShaderFloatSlot& slot : shader->floatSlots)
    {
        GpuMaterialFloatBinding binding;
        binding.uniformName = slot.name;
        binding.value = material->GetFloat(slot.name, slot.defaultValue);
        gpuMaterial.floatBindings.push_back(binding);
    }

    //完整绑定创建成功后写入材质缓存。
    materials[material] = { gpuMaterial, gpuMaterial.sourceKey };
    return &materials[material].resource;
}

void GpuResourceManager::CollectUnused()
{
    //后端关闭后不再触碰资源句柄。
    if (!backend) return;

    //按资源类型清理 CPU 对象已经不存在的缓存项。
    CollectUnusedCache(backend, materials);
    CollectUnusedCache(backend, meshes);
    CollectUnusedCache(backend, textures);
    CollectUnusedCache(backend, skyboxes);
    CollectUnusedCache(backend, shaders);
}
