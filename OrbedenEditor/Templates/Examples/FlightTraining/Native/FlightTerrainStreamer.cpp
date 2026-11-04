#include "FlightTerrainStreamer.h"
#include "Runtime/Object/HeightField.h"
#include "Runtime/Object/Ens.h"
#include "Runtime/World.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/Object/Transform.h"
#include "Profiler/Profiler.h"
#include "Runtime/Coroutines/ThreadOperation.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

OBJECT_TYPE_IMPLEMENT(FlightTerrainStreamer, Script)

//接管机场地形并启动后台加载
void FlightTerrainStreamer::OnStart()
{
    PROFILE("Terrain/Start");
    World* world = GetWorld();
    if (!world)
    {
        return;
    }
    activeChunkSize = std::clamp(chunkSize, 128.0f, 2048.0f);
    activeSamples = std::clamp(samplesPerSide, 17, 129);
    world->ForEachComponent<HeightField>([&](HeightField* terrain)
    {
        if (terrain->GetEns()->GetName() == "Terrain")
        {
            airportTerrain = terrain->GetEnsId();
        }
    });
    Ens* airport = world->GetEns(airportTerrain);
    if (!airport)
    {
        return;
    }
    HeightField* terrain = airport->GetComponent<HeightField>();

    //按配置变化重建机场地形
    if (terrain->sizeX != activeChunkSize || terrain->sizeZ != activeChunkSize
        || terrain->rowCount != activeSamples || terrain->columnCount != activeSamples
        || terrain->GetGeneration() == 0)
    {
        terrain->sizeX = activeChunkSize;
        terrain->sizeZ = activeChunkSize;
        terrain->rowCount = activeSamples;
        terrain->columnCount = activeSamples;
        terrain->Regenerate();
    }

    chunks.push_back({ 0, 0, airportTerrain });
    RunCo(StreamChunks());
}

//在物理步前移动浮动原点
void FlightTerrainStreamer::OnFixedUpdate(float32 deltaTime)
{
    (void)deltaTime;
    Transform* transform = GetEns() ? GetEns()->Transform() : nullptr;
    if (!transform)
    {
        return;
    }
    vector3 position = transform->GetLocalPosition();
    float32 threshold = std::max(rebaseDistance, activeChunkSize * 2);
    if (std::abs(position.x) > threshold || std::abs(position.z) > threshold)
    {
        RebaseWorld({ std::round(position.x / activeChunkSize) * activeChunkSize, 0,
            std::round(position.z / activeChunkSize) * activeChunkSize });
    }
}

//后台生成附近地形并逐帧提交
CoRoutine FlightTerrainStreamer::StreamChunks()
{
    for (;;)
    {
        World* world = GetWorld();
        Ens* airport = world->GetEns(airportTerrain);
        HeightField* source = airport ? airport->GetComponent<HeightField>() : nullptr;
        if (!source)
        {
            co_return;
        }
        source->SyncPendingGeneration();
        if (!source->GetSurfaceMaterial())
        {
            co_yield nullptr;
            continue;
        }

        //卸载远处地形
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
            else
            {
                ++it;
            }
        }

        //按距离准备参数快照
        int32 sourceId = source->GetObjectId();
        uint32 sourceGeneration = source->GetGeneration();
        auto batch = std::make_shared<std::vector<HeightField::GeometryData>>();
        int32 budget = std::clamp(chunksPerBatch, 1, 2);
        for (int32 ring = 0; ring <= radius && budget > 0; ++ring)
        {
            for (int32 dz = -ring; dz <= ring && budget > 0; ++dz)
            {
                for (int32 dx = -ring; dx <= ring && budget > 0; ++dx)
                {
                    if (std::max(std::abs(dx), std::abs(dz)) != ring)
                    {
                        continue;
                    }
                    int32 x = centerX + dx;
                    int32 z = centerZ + dz;
                    if (std::any_of(chunks.begin(), chunks.end(), [&](const TerrainChunk& chunk)
                        { return chunk.x == x && chunk.z == z; }))
                    {
                        continue;
                    }
                    HeightField::GeometryData data = source->CaptureGeometry();
                    data.sampleTileX = x;
                    data.sampleTileZ = z;
                    data.sizeX = activeChunkSize;
                    data.sizeZ = activeChunkSize;
                    data.rowCount = activeSamples;
                    data.columnCount = activeSamples;
                    batch->push_back(std::move(data));
                    --budget;
                }
            }
        }
        if (batch->empty())
        {
            co_yield nullptr;
            continue;
        }

        //后台计算独立数据
        auto work = std::make_shared<ThreadOperation>([batch]
        {
            for (HeightField::GeometryData& data : *batch)
            {
                HeightField::GenerateGeometry(data);
            }
        });
        co_yield work;
        if (!work->Succeeded())
        {
            throw std::runtime_error(work->GetError());
        }

        //逐帧提交有效地形
        for (HeightField::GeometryData& data : *batch)
        {
            world = GetWorld();
            airport = world->GetEns(airportTerrain);
            source = airport ? airport->GetComponent<HeightField>() : nullptr;
            if (!source)
            {
                co_return;
            }
            if (source->GetObjectId() != sourceId || source->GetGeneration() != sourceGeneration)
            {
                break;
            }
            source->SyncPendingGeneration();
            Material* surface = source->GetSurfaceMaterial();
            if (!surface)
            {
                break;
            }
            position = GetEns()->Transform()->GetLocalPosition();
            centerX = static_cast<int32>(std::floor((originX + position.x) / activeChunkSize + 0.5));
            centerZ = static_cast<int32>(std::floor((originZ + position.z) / activeChunkSize + 0.5));
            radius = std::clamp(loadRadius, 2, 6);
            int32 x = data.sampleTileX;
            int32 z = data.sampleTileZ;
            if (std::abs(static_cast<int64>(x) - centerX) > radius
                || std::abs(static_cast<int64>(z) - centerZ) > radius)
            {
                continue;
            }

            {
                PROFILE("Terrain/ApplyChunk");
                Ens* ens = world->CreateEns("Terrain_" + std::to_string(x) + "_" + std::to_string(z));
                ens->Transform()->SetLocalPosition({ static_cast<float32>(static_cast<double>(x) * activeChunkSize - originX),
                    0, static_cast<float32>(static_cast<double>(z) * activeChunkSize - originZ) });
                StaticMeshRenderer* renderer = ens->AddComponent<StaticMeshRenderer>();
                renderer->castShadows = false;
                ens->SetLayer(airport->GetLayer());
                ens->AddComponent<HeightField>([&](HeightField& terrain)
                {
                    terrain.generateNoiseTexture = false;
                    terrain.material.Set(surface);
                    terrain.ApplyGeometry(std::move(data));
                });
                chunks.push_back({ x, z, ens->GetId() });
            }
            co_yield nullptr;
        }
    }
}

/// <summary>移动根节点而保留速度，下一物理步同步 Actor 的新坐标。</summary>
void FlightTerrainStreamer::RebaseWorld(const vector3& shift)
{
    World* world = GetWorld();
    if (!world)
    {
        return;
    }
    originX += shift.x;
    originZ += shift.z;
    world->ForEachEns([&](Ens& ens)
    {
        Transform* transform = ens.Transform();
        if (!transform || !transform->parent.IsNull())
        {
            return;
        }
        vector3 position = transform->GetLocalPosition();
        transform->SetLocalPosition({ position.x - shift.x, position.y, position.z - shift.z });
    });
    //地形位置始终由整数块索引重新计算，避免多次平移累积舍入误差。
    for (const TerrainChunk& chunk : chunks)
    {
        if (chunk.ens == airportTerrain)
        {
            continue;
        }
        if (Ens* ens = world->GetEns(chunk.ens))
        {
            ens->Transform()->SetLocalPosition({ static_cast<float32>(static_cast<double>(chunk.x) * activeChunkSize - originX),
                0, static_cast<float32>(static_cast<double>(chunk.z) * activeChunkSize - originZ) });
        }
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
    StopAllCo();
    ResetOrigin();
    if (World* world = GetWorld())
    {
        for (const TerrainChunk& chunk : chunks)
        {
            if (chunk.ens != airportTerrain)
            {
                world->DestroyEns(chunk.ens);
            }
        }
    }
    chunks.clear();
    originX = 0;
    originZ = 0;
}
