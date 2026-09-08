#pragma once

#include "Runtime/EnsId.h"
#include "Runtime/Object/Material.h"

#include <vector>

class Mesh;
class Texture2D;

//由 fBm 噪声生成高度场的地形组件：
//运行时生成渲染网格（写入同 Ens 的 StaticMeshRenderer）与 PhysX HeightField 碰撞体
//（由 PhysicsSystem 消费），可选生成平铺噪声贴图并绑定到指定材质。
//修改参数字段会立即调用 Regenerate 重建（由 MetaGen 生成的 setter 触发）。
class HeightFieldComponent final : public Component
{
    OBJECT_TYPE_DECLARE(HeightFieldComponent)

public:
    bool enabled = true;
    int32 seed = 1337;
    //全局采样块坐标；使用双精度计算噪声位置，浮动原点不改变地形。
    int32 sampleTileX = 0;
    int32 sampleTileZ = 0;
    float32 sizeX = 400.0f;
    float32 sizeZ = 400.0f;
    int32 rowCount = 129;
    int32 columnCount = 129;
    float32 amplitude = 3.5f;
    float32 frequency = 0.02f;
    int32 octaves = 4;
    float32 flattenMinX = -24.0f;
    float32 flattenMaxX = 24.0f;
    float32 flattenMinZ = -34.0f;
    float32 flattenMaxZ = 10.0f;
    float32 flattenHeight = 0.0f;
    float32 flattenBlendDistance = 24.0f;
    uint32 collisionLayer = 1u;
    Ref<Material> material;
    bool generateNoiseTexture = true;
    int32 noiseTextureSize = 256;
    color noiseLowColor = { 0.24f, 0.32f, 0.22f, 1.0f };
    color noiseHighColor = { 0.62f, 0.58f, 0.42f, 1.0f };
    float32 tileSize = 96.0f;

    //按当前参数重建高度场、渲染网格与噪声贴图。
    void Regenerate();

    /// <summary>获取生成的表面材质，供相邻地形块共享噪声纹理。</summary>
    Material* GetSurfaceMaterial() const;

    //双线性采样世界 XZ 处的高度（解析兜底）。
    float32 GetHeightAtWorldXZ(float32 x, float32 z) const;

    //获取当前高度采样（行沿 X 方向，rowCount*columnCount 个）。
    const std::vector<float32>& GetHeights() const;

    //获取高度场行间距（X 方向）。
    float32 GetRowScale() const;

    //获取高度场列间距（Z 方向）。
    float32 GetColumnScale() const;

    //获取当前生成代数，供物理侧检测数据变化。
    uint32 GetGeneration() const;

    //物理固定步兜底：material 解析后完成渲染网格替换。
    void SyncPendingGeneration();

protected:
    void OnAttach() override;
    void OnDetach() override;

private:
    //确定性二维 value noise，输出 [0,1]。
    float32 SampleNoise(double x, double z, int32 noiseSeed) const;

    /// <summary>采样全局坐标的连续地形高度。</summary>
    float32 SampleHeight(double x, double z) const;

    //按当前参数重建高度采样。
    void RebuildHeights();

    //重建渲染网格并写入同 Ens 的 StaticMeshRenderer。
    void RebuildRenderMesh();

    //重建噪声贴图并绑定到运行时材质。
    void RebuildNoiseTexture();

    std::vector<float32> heights;
    uint32 generation = 0;
    bool meshPending = false;
    Mesh* generatedMesh = nullptr;
    Texture2D* noiseTexture = nullptr;
    Material* runtimeMaterial = nullptr;
    bool ownsRuntimeMaterial = false;
};
