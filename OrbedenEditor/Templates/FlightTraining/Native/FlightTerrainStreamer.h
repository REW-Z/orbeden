#pragma once

#include "Scripting/Script.h"
#include "Runtime/EngineTypes.h"
#include <vector>

class HeightFieldComponent;

//围绕飞机加载连续地形，并通过浮动原点保持物理精度。
class FlightTerrainStreamer final : public Script
{
    OBJECT_TYPE_DECLARE(FlightTerrainStreamer)

public:
    float32 chunkSize = 512.0f;
    int32 samplesPerSide = 65;
    int32 loadRadius = 3;
    int32 chunksPerStep = 2;
    float32 rebaseDistance = 4096.0f;

    /// <summary>回到机场坐标系，供飞机重置使用。</summary>
    void ResetOrigin();

    /// <summary>读取已加载地形块数量。</summary>
    int32 GetLoadedChunkCount();

private:
    struct TerrainChunk
    {
        int32 x = 0;
        int32 z = 0;
        EnsId ens;
    };
    std::vector<TerrainChunk> chunks;
    EnsId airportTerrain;
    double originX = 0;
    double originZ = 0;
    float32 activeChunkSize = 512.0f;
    int32 activeSamples = 65;

    /// <summary>按离飞机的距离补齐地形块，并卸载远处块。</summary>
    void StreamChunks(int32 budget);

    /// <summary>平移世界根节点，保持飞机附近坐标较小。</summary>
    void RebaseWorld(const vector3& shift);

protected:
    void OnStart();
    void OnFixedUpdate(float32 deltaTime);
    void OnEnd();
};
