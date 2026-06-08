#include "Rendering/GpuResourceManager.h"

#include "Log/Log.h"

namespace
{
    constexpr uint32 VertexFloatCount = 11;
    constexpr uint32 VertexStride = VertexFloatCount * sizeof(float32);
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
            backend->DeleteVertexArray(pair.second.vertexArray);
            backend->DeleteBuffer(pair.second.vertexBuffer);
            backend->DeleteBuffer(pair.second.indexBuffer);
        }

        for (auto& pair : textures)
        {
            backend->DeleteTexture(pair.second);
        }

        for (auto& pair : shaders)
        {
            backend->DeleteProgram(pair.second.program);
        }
    }

    meshes.clear();
    textures.clear();
    shaders.clear();
    materials.clear();
    backend = nullptr;
}

GpuMeshHandle GpuResourceManager::GetMesh(Mesh* mesh)
{
    if (!backend || !mesh) return GpuMeshHandle();

    auto it = meshes.find(mesh);
    if (it != meshes.end()) return it->second;

    if (mesh->vertices.empty() || mesh->indices.empty())
    {
        Log::Error("GpuResourceManager mesh upload failed: mesh has no vertices or indices.");
        return GpuMeshHandle();
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

    BufferInfo vertexBufferInfo;
    vertexBufferInfo.kind = BufferKind::Vertex;
    vertexBufferInfo.data = vertexData.data();
    vertexBufferInfo.size = vertexData.size() * sizeof(float32);

    BufferInfo indexBufferInfo;
    indexBufferInfo.kind = BufferKind::Index;
    indexBufferInfo.data = mesh->indices.data();
    indexBufferInfo.size = mesh->indices.size() * sizeof(uint32);

    GpuMeshHandle handle;
    handle.vertexBuffer = backend->CreateBuffer(vertexBufferInfo);
    handle.indexBuffer = backend->CreateBuffer(indexBufferInfo);
    handle.indexCount = static_cast<uint32>(mesh->indices.size());

    VertexLayoutInfo layoutInfo;
    layoutInfo.vertexBuffer = handle.vertexBuffer;
    layoutInfo.indexBuffer = handle.indexBuffer;
    layoutInfo.stride = VertexStride;
    handle.vertexArray = backend->CreateVertexArray(layoutInfo);

    if (!handle.IsValid())
    {
        Log::Error("GpuResourceManager mesh upload failed: backend returned invalid mesh handles.");
        backend->DeleteVertexArray(handle.vertexArray);
        backend->DeleteBuffer(handle.vertexBuffer);
        backend->DeleteBuffer(handle.indexBuffer);
        return GpuMeshHandle();
    }

    meshes[mesh] = handle;
    return handle;
}

GpuTextureHandle GpuResourceManager::GetTexture(Texture2D* texture)
{
    if (!backend || !texture) return GpuTextureHandle();

    auto it = textures.find(texture);
    if (it != textures.end()) return it->second;

    TextureInfo info;
    info.width = texture->width;
    info.height = texture->height;
    info.channels = texture->channels;
    info.pixels = texture->pixels.empty() ? nullptr : texture->pixels.data();

    GpuTextureHandle handle = backend->CreateTexture(info);
    if (!handle.IsValid())
    {
        Log::Error("GpuResourceManager texture upload failed.");
        return GpuTextureHandle();
    }

    textures[texture] = handle;
    return handle;
}

GpuShaderHandle GpuResourceManager::GetShader(MaterialShader* shader)
{
    if (!backend || !shader) return GpuShaderHandle();

    auto it = shaders.find(shader);
    if (it != shaders.end()) return it->second;

    ProgramInfo info;
    info.vertexSource = shader->vertexSource.c_str();
    info.fragmentSource = shader->fragmentSource.c_str();

    GpuShaderHandle handle;
    handle.program = backend->CreateProgram(info);
    if (!handle.IsValid())
    {
        Log::Error("GpuResourceManager shader upload failed.");
        return GpuShaderHandle();
    }

    shaders[shader] = handle;
    return handle;
}

GpuMaterialHandle GpuResourceManager::GetMaterial(Material* material)
{
    if (!backend || !material) return GpuMaterialHandle();

    auto it = materials.find(material);
    if (it != materials.end()) return it->second;

    MaterialShader* shader = material->shader.Get();
    if (!shader)
    {
        Log::Error("GpuResourceManager material upload failed: shader is missing.");
        return GpuMaterialHandle();
    }

    GpuMaterialHandle handle;
    handle.shader = GetShader(shader);
    handle.diffuse = material->diffuse;
    if (!handle.shader.IsValid()) return GpuMaterialHandle();

    if (material->hasDiffuseTexture)
    {
        Texture2D* diffuseTexture = material->textureDiffuse.Get();
        if (diffuseTexture)
        {
            handle.diffuseTexture = GetTexture(diffuseTexture);
            handle.hasDiffuseTexture = handle.diffuseTexture.IsValid();
        }
        else
        {
            Log::Error("GpuResourceManager material texture skipped: diffuse texture is missing.");
        }
    }

    materials[material] = handle;
    return handle;
}

