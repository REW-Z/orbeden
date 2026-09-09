#include "FlightTerrainStreamer.h"
#include "Physics/HeightFieldComponent.h"
#include "Runtime/Ens.h"
#include "Runtime/World.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/Object/TransformComponent.h"
#include <algorithm>
#include <cmath>
#include <string>

OBJECT_TYPE_IMPLEMENT(FlightTerrainStreamer, Script)

/// <summary>接管场景中的机场地形块，预加载最近邻。</summary>
void FlightTerrainStreamer::OnStart()
{
    World* world = GetWorld();
    if (!world) return;
    activeChunkSize = std::clamp(chunkSize, 128.0f, 2048.0f);
    activeSamples = std::clamp(samplesPerSide, 17, 129);
    world->ForEachComponent<HeightFieldComponent>([&](HeightFieldComponent* terrain)
    {
        if (terrain->GetEns()->GetName() == "Terrain") airportTerrain = terrain->GetEnsId();
    });
    Ens* airport = world->GetEns(airportTerrain);
    if (!airport) return;
    HeightFieldComponent* terrain = airport->GetComponent<HeightFieldComponent>();
    terrain->sizeX = activeChunkSize;
    terrain->sizeZ = activeChunkSize;
    terrain->rowCount = activeSamples;
    terrain->columnCount = activeSamples;
    terrain->Regenerate();
    chunks.push_back({ 0, 0, airportTerrain });
    StreamChunks(8);
}

/// <summary>物理模拟前加载附近碰撞地形，并按块移动浮动原点。</summary>
void FlightTerrainStreamer::OnFixedUpdate(float32 deltaTime)
{
    (void)deltaTime;
    TransformComponent* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform) return;
    vector3 position = transform->GetLocalPosition();
    float32 threshold = std::max(rebaseDistance, activeChunkSize * 2);
    if (std::abs(position.x) > threshold || std::abs(position.z) > threshold)
    {
        RebaseWorld({ std::round(position.x / activeChunkSize) * activeChunkSize, 0,
            std::round(position.z / activeChunkSize) * activeChunkSize });
    }
    StreamChunks(std::clamp(chunksPerStep, 1, 8));
}

/// <summary>连续采样相邻块，共享机场的噪声材质，保留一圈卸载缓冲。</summary>
void FlightTerrainStreamer::StreamChunks(int32 budget)
{
    World* world = GetWorld();
    Ens* airport = world ? world->GetEns(airportTerrain) : nullptr;
    if (!airport || !GetEns()) return;
    HeightFieldComponent* source = airport->GetComponent<HeightFieldComponent>();
    source->SyncPendingGeneration();
    Material* surface = source->GetSurfaceMaterial();
    if (!surface) return;
    vector3 position = GetEns()->Transform()->GetLocalPosition();
    int32 centerX = static_cast<int32>(std::floor((originX + position.x) / activeChunkSize + 0.5));
    int32 centerZ = static_cast<int32>(std::floor((originZ + position.z) / activeChunkSize + 0.5));
    int32 radius = std::clamp(loadRadius, 2, 6);

    for (auto it = chunks.begin(); it != chunks.end();)
    {
        if (it->ens != airportTerrain && (std::abs(static_cast<int64>(it->x) - centerX) > radius + 1
            || std::abs(static_cast<int64>(it->z) - centerZ) > radius + 1))
        {
            world->DestroyEns(it->ens);
            it = chunks.erase(it);
        }
        else ++it;
    }

    for (int32 ring = 0; ring <= radius && budget > 0; ++ring)
    {
        for (int32 dz = -ring; dz <= ring && budget > 0; ++dz)
        {
            for (int32 dx = -ring; dx <= ring && budget > 0; ++dx)
            {
                if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                int32 x = centerX + dx;
                int32 z = centerZ + dz;
                if (std::any_of(chunks.begin(), chunks.end(), [&](const TerrainChunk& chunk)
                    { return chunk.x == x && chunk.z == z; })) continue;
                Ens* ens = world->CreateEns("Terrain_" + std::to_string(x) + "_" + std::to_string(z));
                ens->Transform()->SetLocalPosition({ static_cast<float32>(static_cast<double>(x) * activeChunkSize - originX),
                    0, static_cast<float32>(static_cast<double>(z) * activeChunkSize - originZ) });
                StaticMeshRenderer* renderer = ens->AddComponent<StaticMeshRenderer>();
                renderer->castShadows = false;
                HeightFieldComponent* terrain = ens->AddComponent<HeightFieldComponent>();
                terrain->seed = source->seed;
                terrain->sampleTileX = x;
                terrain->sampleTileZ = z;
                terrain->sizeX = activeChunkSize;
                terrain->sizeZ = activeChunkSize;
                terrain->rowCount = activeSamples;
                terrain->columnCount = activeSamples;
                terrain->amplitude = source->amplitude;
                terrain->frequency = source->frequency;
                terrain->octaves = source->octaves;
                terrain->flattenMinX = source->flattenMinX;
                terrain->flattenMaxX = source->flattenMaxX;
                terrain->flattenMinZ = source->flattenMinZ;
                terrain->flattenMaxZ = source->flattenMaxZ;
                terrain->flattenHeight = source->flattenHeight;
                terrain->flattenBlendDistance = source->flattenBlendDistance;
                terrain->collisionLayer = source->collisionLayer;
                terrain->tileSize = source->tileSize;
                terrain->generateNoiseTexture = false;
                terrain->material.Set(surface);
                terrain->Regenerate();
                chunks.push_back({ x, z, ens->GetId() });
                --budget;
            }
        }
    }
}

/// <summary>移动根节点而保留速度，下一物理步同步 Actor 的新坐标。</summary>
void FlightTerrainStreamer::RebaseWorld(const vector3& shift)
{
    World* world = GetWorld();
    if (!world) return;
    originX += shift.x;
    originZ += shift.z;
    world->ForEachEns([&](Ens& ens)
    {
        TransformComponent* transform = ens.Transform();
        if (!transform || !transform->parent.IsNull()) return;
        vector3 position = transform->GetLocalPosition();
        transform->SetLocalPosition({ position.x - shift.x, position.y, position.z - shift.z });
    });
    //地形位置始终由整数块索引重新计算，避免多次平移累积舍入误差。
    for (const TerrainChunk& chunk : chunks)
    {
        if (chunk.ens == airportTerrain) continue;
        if (Ens* ens = world->GetEns(chunk.ens))
            ens->Transform()->SetLocalPosition({ static_cast<float32>(static_cast<double>(chunk.x) * activeChunkSize - originX),
                0, static_cast<float32>(static_cast<double>(chunk.z) * activeChunkSize - originZ) });
    }
}

/// <summary>恢复机场原点并补齐机场附近地形。</summary>
void FlightTerrainStreamer::ResetOrigin()
{
    RebaseWorld({ static_cast<float32>(-originX), 0, static_cast<float32>(-originZ) });
    originX = 0;
    originZ = 0;
}

/// <summary>读取当前常驻地形块数。</summary>
int32 FlightTerrainStreamer::GetLoadedChunkCount()
{
    return static_cast<int32>(chunks.size());
}

/// <summary>停止 Play 时释放动态地形块，保留场景原有机场块。</summary>
void FlightTerrainStreamer::OnEnd()
{
    ResetOrigin();
    if (World* world = GetWorld())
        for (const TerrainChunk& chunk : chunks)
            if (chunk.ens != airportTerrain) world->DestroyEns(chunk.ens);
    chunks.clear();
    originX = 0;
    originZ = 0;
}
