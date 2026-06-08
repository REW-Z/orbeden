#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/World.h"

#include <unordered_map>

//每帧空间矩阵缓存
class SpaceCache
{
private:
    World* world = nullptr;
    std::unordered_map<uint64, matrix4x4> worldMatrices;

public:
    //重建世界矩阵缓存
    void Update(World& currentWorld);

    //获取世界矩阵
    matrix4x4 GetWorldMatrix(EnsId ens) const;

private:
    matrix4x4 BuildWorldMatrix(EnsId ens);
};

