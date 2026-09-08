#include "Physics/HeightFieldComponent.h"

#include "Runtime/Ens.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/Object/Texture2D.h"
#include "Runtime/Object/TransformComponent.h"

#include <algorithm>
#include <cmath>

OBJECT_TYPE_IMPLEMENT(HeightFieldComponent, Component)

namespace
{
    //确定性整数哈希，用于派生格点随机值。
    uint32 HashInt(uint32 value)
    {
        value ^= value >> 16;
        value *= 0x7FEB352Du;
        value ^= value >> 15;
        value *= 0x846CA68Bu;
        value ^= value >> 16;
        return value;
    }

    //在 [0,1] 平滑插值。
    float32 Smoothstep(float32 value)
    {
        return value * value * (3.0f - 2.0f * value);
    }

    //在 [0,1] 线性插值。
    float32 Lerp(float32 a, float32 b, float32 t)
    {
        return a + (b - a) * t;
    }
}

//挂载时生成初始地形。
void HeightFieldComponent::OnAttach()
{
    Regenerate();
}

/// <summary>卸载地形块时释放专属资源，避免无限飞行累积 CPU/GPU 内存。</summary>
void HeightFieldComponent::OnDetach()
{
    StaticMeshRenderer* renderer = GetEns() ? GetEns()->GetComponent<StaticMeshRenderer>() : nullptr;
    if (renderer && renderer->mesh.Get() == generatedMesh) renderer->mesh.Set(nullptr);
    Object::DeleteInstance(generatedMesh);
    if (ownsRuntimeMaterial) Object::DeleteInstance(runtimeMaterial);
    Object::DeleteInstance(noiseTexture);
    generatedMesh = nullptr;
    noiseTexture = nullptr;
    runtimeMaterial = nullptr;
    ownsRuntimeMaterial = false;
}

/// <summary>获取可共享的地形材质。</summary>
Material* HeightFieldComponent::GetSurfaceMaterial() const
{
    return runtimeMaterial ? runtimeMaterial : material.Get();
}

//按当前参数重建高度场、渲染网格与噪声贴图。
void HeightFieldComponent::Regenerate()
{
    RebuildHeights();
    RebuildNoiseTexture();
    RebuildRenderMesh();
}

//双线性采样世界 XZ 处的高度。
float32 HeightFieldComponent::GetHeightAtWorldXZ(float32 x, float32 z) const
{
    if (heights.empty()) return 0.0f;

    int32 rows = std::max(rowCount, 2);
    int32 columns = std::max(columnCount, 2);
    TransformComponent* transform = GetEns() ? GetEns()->Transform() : nullptr;
    vector3 origin = transform ? (transform->transformCacheInitialized ? transform->worldPosition : transform->GetLocalPosition()) : vector3();
    x -= origin.x;
    z -= origin.z;
    float32 halfX = sizeX * 0.5f;
    float32 halfZ = sizeZ * 0.5f;
    float32 gridX = (x + halfX) / std::max(sizeX, 0.001f) * (rows - 1);
    float32 gridZ = (z + halfZ) / std::max(sizeZ, 0.001f) * (columns - 1);
    int32 row = std::clamp(static_cast<int32>(gridX), 0, rows - 2);
    int32 column = std::clamp(static_cast<int32>(gridZ), 0, columns - 2);
    float32 tx = std::clamp(gridX - row, 0.0f, 1.0f);
    float32 tz = std::clamp(gridZ - column, 0.0f, 1.0f);
    float32 h00 = heights[static_cast<size_t>(row) * columns + column];
    float32 h10 = heights[static_cast<size_t>(row) * columns + column + 1];
    float32 h01 = heights[static_cast<size_t>(row + 1) * columns + column];
    float32 h11 = heights[static_cast<size_t>(row + 1) * columns + column + 1];
    return origin.y + Lerp(Lerp(h00, h10, tz), Lerp(h01, h11, tz), tx);
}

//获取当前高度采样。
const std::vector<float32>& HeightFieldComponent::GetHeights() const
{
    return heights;
}

//获取高度场行间距（X 方向）。
float32 HeightFieldComponent::GetRowScale() const
{
    return sizeX / std::max(rowCount - 1, 1);
}

//获取高度场列间距（Z 方向）。
float32 HeightFieldComponent::GetColumnScale() const
{
    return sizeZ / std::max(columnCount - 1, 1);
}

//获取当前生成代数。
uint32 HeightFieldComponent::GetGeneration() const
{
    return generation;
}

//物理固定步兜底：material 解析后完整重建（噪声贴图和运行时材质依赖它）。
void HeightFieldComponent::SyncPendingGeneration()
{
    if (meshPending && material.Get())
    {
        meshPending = false;
        Regenerate();
    }
}

//确定性二维 value noise，输出 [0,1]。
float32 HeightFieldComponent::SampleNoise(double x, double z, int32 noiseSeed) const
{
    int64 xi = static_cast<int64>(std::floor(x));
    int64 zi = static_cast<int64>(std::floor(z));
    float32 xf = static_cast<float32>(x - std::floor(x));
    float32 zf = static_cast<float32>(z - std::floor(z));

    auto lattice = [noiseSeed](int64 gridX, int64 gridZ)
    {
        uint32 h = HashInt(static_cast<uint32>(noiseSeed)
            ^ (static_cast<uint32>(gridX) * 0x9E3779B9u)
            ^ (static_cast<uint32>(gridZ) * 0x85EBCA6Bu)
            ^ HashInt(static_cast<uint32>(static_cast<uint64>(gridX) >> 32))
            ^ HashInt(static_cast<uint32>(static_cast<uint64>(gridZ) >> 32)));
        return static_cast<float32>(h % 10000u) / 9999.0f;
    };

    float32 x00 = lattice(xi, zi);
    float32 x10 = lattice(xi + 1, zi);
    float32 x01 = lattice(xi, zi + 1);
    float32 x11 = lattice(xi + 1, zi + 1);
    float32 tx = Smoothstep(xf);
    float32 tz = Smoothstep(zf);
    return Lerp(Lerp(x00, x10, tx), Lerp(x01, x11, tx), tz);
}

/// <summary>按统一全局坐标采样，并在机场周围平滑过渡到平整地面。</summary>
float32 HeightFieldComponent::SampleHeight(double x, double z) const
{
    float32 total = 0.0f;
    float32 weight = 0.0f;
    double layerFrequency = frequency;
    float32 layerAmplitude = 1.0f;
    for (int32 octave = 0; octave < std::max(octaves, 1); ++octave)
    {
        total += SampleNoise(x * layerFrequency, z * layerFrequency, seed + octave * 101) * layerAmplitude;
        weight += layerAmplitude;
        layerAmplitude *= 0.5f;
        layerFrequency *= 2.0;
    }
    float32 height = total / weight * amplitude;
    if (flattenMinX < flattenMaxX && flattenMinZ < flattenMaxZ)
    {
        double dx = std::max({ static_cast<double>(flattenMinX) - x, 0.0, x - flattenMaxX });
        double dz = std::max({ static_cast<double>(flattenMinZ) - z, 0.0, z - flattenMaxZ });
        float32 blend = flattenBlendDistance > 0.0f
            ? Smoothstep(static_cast<float32>(std::clamp(std::max(dx, dz) / flattenBlendDistance, 0.0, 1.0)))
            : (dx == 0.0 && dz == 0.0 ? 0.0f : 1.0f);
        height = Lerp(flattenHeight, height, blend);
    }
    return height;
}

/// <summary>重建地形块采样，边界使用相同全局坐标。</summary>
void HeightFieldComponent::RebuildHeights()
{
    int32 rows = std::max(rowCount, 2);
    int32 columns = std::max(columnCount, 2);
    heights.resize(static_cast<size_t>(rows) * columns);
    double originX = static_cast<double>(sampleTileX) * sizeX - sizeX * 0.5;
    double originZ = static_cast<double>(sampleTileZ) * sizeZ - sizeZ * 0.5;
    for (int32 row = 0; row < rows; ++row)
    {
        double x = originX + static_cast<double>(row) * sizeX / (rows - 1);
        for (int32 column = 0; column < columns; ++column)
        {
            double z = originZ + static_cast<double>(column) * sizeZ / (columns - 1);
            heights[static_cast<size_t>(row) * columns + column] = SampleHeight(x, z);
        }
    }
    ++generation;
}

//重建渲染网格并写入同 Ens 的 StaticMeshRenderer。
void HeightFieldComponent::RebuildRenderMesh()
{
    Ens* ens = GetEns();
    StaticMeshRenderer* renderer = ens ? ens->GetComponent<StaticMeshRenderer>() : nullptr;
    if (!renderer) return;

    //material 尚未解析时保留占位网格，由 SyncPendingGeneration 兜底。
    if (!material.Get())
    {
        meshPending = true;
        return;
    }

    int32 rows = std::max(rowCount, 2);
    int32 columns = std::max(columnCount, 2);
    int32 vertexCount = rows * columns;
    if (heights.size() != static_cast<size_t>(vertexCount)) return;

    Mesh* mesh = generatedMesh ? generatedMesh : Object::CreateInstance<Mesh>();
    if (!mesh) return;

    std::vector<vector3> vertices(static_cast<size_t>(vertexCount));
    std::vector<vector2> texcoords(static_cast<size_t>(vertexCount));
    std::vector<vector3> normals(static_cast<size_t>(vertexCount));
    float32 halfX = sizeX * 0.5f;
    float32 halfZ = sizeZ * 0.5f;
    for (int32 row = 0; row < rows; row++)
    {
        for (int32 column = 0; column < columns; column++)
        {
            size_t index = static_cast<size_t>(row) * columns + column;
            float32 x = (rows > 1 ? static_cast<float32>(row) / (rows - 1) : 0.0f) * sizeX - halfX;
            float32 z = (columns > 1 ? static_cast<float32>(column) / (columns - 1) : 0.0f) * sizeZ - halfZ;
            vertices[index] = { x, heights[index], z };
            double globalX = static_cast<double>(sampleTileX) * sizeX + x;
            double globalZ = static_cast<double>(sampleTileZ) * sizeZ + z;
            double textureSize = std::max(tileSize, 0.001f);
            texcoords[index] = {
                static_cast<float32>(std::fmod(static_cast<double>(sampleTileX) * sizeX / textureSize, 1.0) + x / textureSize),
                static_cast<float32>(std::fmod(static_cast<double>(sampleTileZ) * sizeZ / textureSize, 1.0) + z / textureSize) };
            float32 dx = std::max(GetRowScale(), 0.001f);
            float32 dz = std::max(GetColumnScale(), 0.001f);
            vector3 normal = {
                (SampleHeight(globalX - dx, globalZ) - SampleHeight(globalX + dx, globalZ)) / (2 * dx),
                1.0f,
                (SampleHeight(globalX, globalZ - dz) - SampleHeight(globalX, globalZ + dz)) / (2 * dz) };
            float32 length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            normals[index] = { normal.x / length, normal.y / length, normal.z / length };
        }
    }

    int32 quadCount = (rows - 1) * (columns - 1);
    std::vector<uint32> indices(static_cast<size_t>(quadCount) * 6);
    size_t indexOffset = 0;
    for (int32 row = 0; row < rows - 1; row++)
    {
        for (int32 column = 0; column < columns - 1; column++)
        {
            uint32 a = static_cast<uint32>(row * columns + column);
            uint32 b = a + 1;
            uint32 c = a + static_cast<uint32>(columns);
            uint32 d = c + 1;
            indices[indexOffset++] = a;
            indices[indexOffset++] = b;
            indices[indexOffset++] = c;
            indices[indexOffset++] = b;
            indices[indexOffset++] = d;
            indices[indexOffset++] = c;
        }
    }

    mesh->SetVertexPositions(vertices.data(), vertexCount);
    mesh->SetVertexTexcoords(texcoords.data(), vertexCount);
    mesh->SetIndexData(indices.data(), static_cast<int32>(indices.size()));
    mesh->ResizeSubMeshes(1);
    //运行时材质创建失败时回退到源材质，保证地形始终可见。
    Material* submeshMaterial = runtimeMaterial ? runtimeMaterial : material.Get();
    mesh->ConfigureSubMesh(0, "Main", 0, static_cast<uint32>(indices.size()), submeshMaterial);
    mesh->SetVertexNormals(normals.data(), vertexCount);

    renderer->mesh = Ref<Mesh>(mesh);
    mesh->MarkDirty();
    meshPending = false;
    generatedMesh = mesh;
}

//重建噪声贴图并绑定到运行时材质。
void HeightFieldComponent::RebuildNoiseTexture()
{
    Material* source = material.Get();
    if (!source)
    {
        meshPending = true;
        return;
    }

    if (!generateNoiseTexture)
    {
        if (ownsRuntimeMaterial) Object::DeleteInstance(runtimeMaterial);
        Object::DeleteInstance(noiseTexture);
        noiseTexture = nullptr;
        runtimeMaterial = source;
        ownsRuntimeMaterial = false;
        return;
    }

    //重新创建纹理以触发 GPU 上传，材质保持身份供地形块共享。
    Object::DeleteInstance(noiseTexture);
    noiseTexture = nullptr;

    noiseTexture = Object::CreateInstance<Texture2D>();
    if (!noiseTexture) return;

    int32 size = std::clamp(noiseTextureSize, 16, 1024);
    noiseTexture->width = size;
    noiseTexture->height = size;
    noiseTexture->channels = 4;
    noiseTexture->pixels.resize(static_cast<size_t>(size) * size * 4);

    for (int32 y = 0; y < size; y++)
    {
        for (int32 x = 0; x < size; x++)
        {
            float32 nx = static_cast<float32>(x) / size * 4.0f;
            float32 nz = static_cast<float32>(y) / size * 4.0f;
            float32 total = 0.0f;
            float32 weight = 0.0f;
            float32 layerFrequency = 1.0f;
            float32 layerAmplitude = 1.0f;
            for (int32 octave = 0; octave < std::max(octaves, 1); octave++)
            {
                float32 tx = static_cast<float32>(x) / size;
                float32 tz = static_cast<float32>(y) / size;
                int32 noiseSeed = seed + 977 + octave * 101;
                float32 a = SampleNoise(nx * layerFrequency, nz * layerFrequency, noiseSeed);
                float32 b = SampleNoise((nx - 4) * layerFrequency, nz * layerFrequency, noiseSeed);
                float32 c = SampleNoise(nx * layerFrequency, (nz - 4) * layerFrequency, noiseSeed);
                float32 d = SampleNoise((nx - 4) * layerFrequency, (nz - 4) * layerFrequency, noiseSeed);
                total += Lerp(Lerp(a, b, tx), Lerp(c, d, tx), tz) * layerAmplitude;
                weight += layerAmplitude;
                layerAmplitude *= 0.5f;
                layerFrequency *= 2.0f;
            }
            float32 value = std::clamp(total / weight, 0.0f, 1.0f);

            size_t pixel = (static_cast<size_t>(y) * size + x) * 4;
            noiseTexture->pixels[pixel + 0] = static_cast<uint8>(Lerp(noiseLowColor.r, noiseHighColor.r, value) * 255.0f);
            noiseTexture->pixels[pixel + 1] = static_cast<uint8>(Lerp(noiseLowColor.g, noiseHighColor.g, value) * 255.0f);
            noiseTexture->pixels[pixel + 2] = static_cast<uint8>(Lerp(noiseLowColor.b, noiseHighColor.b, value) * 255.0f);
            noiseTexture->pixels[pixel + 3] = 255;
        }
    }

    if (!ownsRuntimeMaterial)
    {
        runtimeMaterial = Object::CreateInstance<Material>();
        if (!runtimeMaterial) return;
        ownsRuntimeMaterial = true;
    }

    runtimeMaterial->colorSlots = source->colorSlots;
    runtimeMaterial->floatSlots = source->floatSlots;
    runtimeMaterial->SetShader(source->shader.Get());
    runtimeMaterial->SetTexture("u_DiffuseTexture", noiseTexture);
    runtimeMaterial->MarkDirty();
}
