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

    //释放 GPU Mesh 句柄
    void DeleteGpuMesh(RenderBackend* backend, GpuMesh& mesh)
    {
        if (!backend) return;

        backend->DeleteVertexInput(mesh.vertexInput);
        backend->DeleteVertexBuffer(mesh.vertexBuffer);
        backend->DeleteIndexBuffer(mesh.indexBuffer);
        mesh = GpuMesh();
    }

    //释放 GPU Shader 句柄
    void DeleteGpuShader(RenderBackend* backend, GpuShader& shader)
    {
        if (!backend) return;

        backend->DeleteShaderProgram(shader.shaderProgram);
        shader = GpuShader();
    }
}

void GpuResourceManager::Initialize(RenderBackend* renderBackend)
{
    backend = renderBackend;
}

void GpuResourceManager::Shutdown()
{
    if (backend)
    {
        for (auto& pair : meshes)
        {
            DeleteGpuMesh(backend, pair.second);
        }

        for (auto& pair : textures)
        {
            backend->DeleteTexture(pair.second);
        }

        for (auto& pair : skyboxes)
        {
            backend->DeleteCubeTexture(pair.second);
        }

        for (auto& pair : shaders)
        {
            DeleteGpuShader(backend, pair.second);
        }
    }

    meshes.clear();
    textures.clear();
    skyboxes.clear();
    shaders.clear();
    materials.clear();
    backend = nullptr;
}

GpuMesh GpuResourceManager::GetMesh(Mesh* mesh)
{
    if (!backend || !mesh) return GpuMesh();

    auto it = meshes.find(mesh);
    uint64 meshRevision = mesh->GetRevision();
    if (it != meshes.end())
    {
        if (it->second.meshRevision == meshRevision) return it->second;

        DeleteGpuMesh(backend, it->second);
        meshes.erase(it);
    }

    if (mesh->vertices.empty() || mesh->indices.empty())
    {
        Log::Error("GpuResourceManager mesh upload failed: mesh has no vertices or indices.");
        return GpuMesh();
    }

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

    if (!gpuMesh.IsValid())
    {
        Log::Error("GpuResourceManager mesh upload failed: backend returned invalid mesh handles.");
        backend->DeleteVertexInput(gpuMesh.vertexInput);
        backend->DeleteVertexBuffer(gpuMesh.vertexBuffer);
        backend->DeleteIndexBuffer(gpuMesh.indexBuffer);
        return GpuMesh();
    }

    meshes[mesh] = gpuMesh;
    return gpuMesh;
}

GpuTextureID GpuResourceManager::GetTexture(Texture2D* texture)
{
    if (!backend || !texture) return GpuTextureID();

    auto it = textures.find(texture);
    if (it != textures.end()) return it->second;

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

    textures[texture] = textureID;
    return textureID;
}

GpuCubeTextureID GpuResourceManager::GetSkybox(Skybox* skybox)
{
    if (!backend || !skybox) return GpuCubeTextureID();

    auto it = skyboxes.find(skybox);
    if (it != skyboxes.end()) return it->second;

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

    skyboxes[skybox] = cubeTexture;
    return cubeTexture;
}

GpuShader GpuResourceManager::GetShader(Shader* shader)
{
    if (!backend || !shader) return GpuShader();

    auto it = shaders.find(shader);
    uint64 shaderRevision = shader->GetRevision();
    if (it != shaders.end())
    {
        if (it->second.shaderRevision == shaderRevision) return it->second;

        DeleteGpuShader(backend, it->second);
        shaders.erase(it);
    }

    GpuShaderProgramDesc shaderProgramDesc;
    shaderProgramDesc.vertexSource = shader->vertexSource.c_str();
    shaderProgramDesc.fragmentSource = shader->fragmentSource.c_str();

    GpuShader gpuShader;
    gpuShader.shaderProgram = backend->CreateShaderProgram(shaderProgramDesc);
    gpuShader.shaderRevision = shaderRevision;
    gpuShader.sourceKey = shader->GetInstanceId().GetPath();
    if (!gpuShader.IsValid())
    {
        Log::Error("GpuResourceManager shader upload failed.");
        return GpuShader();
    }

    shaders[shader] = gpuShader;
    return gpuShader;
}

GpuMaterial GpuResourceManager::GetMaterial(Material* material)
{
    if (!backend || !material) return GpuMaterial();

    Shader* shader = material->shader.Get();
    if (!shader)
    {
        Log::Error("GpuResourceManager material upload failed: shader is missing.");
        return GpuMaterial();
    }

    auto it = materials.find(material);
    uint64 shaderRevision = shader->GetRevision();
    if (it != materials.end()
        && it->second.sourceShader == shader
        && it->second.materialRevision == material->GetRevision()
        && it->second.shaderRevision == shaderRevision)
    {
        return it->second;
    }

    GpuMaterial gpuMaterial;
    gpuMaterial.sourceShader = shader;
    gpuMaterial.sourceKey = material->GetInstanceId().GetPath();
    gpuMaterial.materialRevision = material->GetRevision();
    gpuMaterial.shaderRevision = shaderRevision;
    gpuMaterial.shader = GetShader(shader);
    if (!gpuMaterial.shader.IsValid()) return GpuMaterial();

    for (const ShaderTextureSlot& slot : shader->textureSlots)
    {
        if (slot.dimension != ShaderTextureDimension::Texture2D) continue;

        GpuMaterialTextureBinding binding;
        binding.uniformName = slot.name;
        binding.presenceUniformName = CreateTexturePresenceUniformName(slot.name);
        Texture2D* texture = material->GetTexture(slot.name);
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

    for (const ShaderColorSlot& slot : shader->colorSlots)
    {
        GpuMaterialColorBinding binding;
        binding.uniformName = slot.name;
        binding.value = material->GetColor(slot.name, slot.defaultValue);
        gpuMaterial.colorBindings.push_back(binding);
    }

    for (const ShaderFloatSlot& slot : shader->floatSlots)
    {
        GpuMaterialFloatBinding binding;
        binding.uniformName = slot.name;
        binding.value = material->GetFloat(slot.name, slot.defaultValue);
        gpuMaterial.floatBindings.push_back(binding);
    }

    materials[material] = gpuMaterial;
    return gpuMaterial;
}

void GpuResourceManager::CollectUnused()
{
    if (!backend) return;

    for (auto it = meshes.begin(); it != meshes.end();)
    {
        if (!Object::FindObject(StringId(it->second.sourceKey)))
        {
            DeleteGpuMesh(backend, it->second);
            it = meshes.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = shaders.begin(); it != shaders.end();)
    {
        if (!Object::FindObject(StringId(it->second.sourceKey)))
        {
            DeleteGpuShader(backend, it->second);
            it = shaders.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = materials.begin(); it != materials.end();)
    {
        bool materialAlive = Object::FindObject(StringId(it->second.sourceKey)) != nullptr;
        bool shaderAlive = it->second.shader.sourceKey.empty() || Object::FindObject(StringId(it->second.shader.sourceKey)) != nullptr;
        if (!materialAlive || !shaderAlive)
        {
            it = materials.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
