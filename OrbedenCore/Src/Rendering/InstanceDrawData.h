#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EnsId.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"

#include <optional>

class World;

//每个渲染帧接收的显式实例总数
constexpr uint32 MaximumSubmittedInstances = 1048576u;

//显式实例提交的每实例数据。
//这一层只表达 TRS：父级变换与 shear 不由该记录表示，静态渲染器仍走完整世界矩阵。
struct MeshInstanceData
{
public:
    vector3 position = { 0.0f, 0.0f, 0.0f };
    quaternion rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    vector3 scale = { 1.0f, 1.0f, 1.0f };
    //输入的 RGB 是 sRGB，Alpha 原样；管线在提交时统一转成线性
    color tint = { 1.0f, 1.0f, 1.0f, 1.0f };
    //图集 uv 变换，只作为四个浮点使用，禁止颜色空间转换
    color uvRect = { 0.0f, 0.0f, 1.0f, 1.0f };

    //检查实例变换和着色数据
    bool IsValid() const;
};

//与托管实例缓冲共享连续的浮点布局
static_assert(std::is_trivially_copyable_v<MeshInstanceData> && std::is_standard_layout_v<MeshInstanceData>);
static_assert(sizeof(MeshInstanceData) == 72 && offsetof(MeshInstanceData, rotation) == 12
    && offsetof(MeshInstanceData, scale) == 28 && offsetof(MeshInstanceData, tint) == 40 && offsetof(MeshInstanceData, uvRect) == 56);

//显式实例提交的绘制选项
struct InstanceDrawOptions
{
public:
    uint32 drawLayer = 1u;
    bool castShadows = true;
    bool receiveShadows = true;
    //空表示参与全部相机；非空时只参与该相机的主 Pass 与阴影
    EnsId camera;
};

//每实例网格数据：世界矩阵、法线矩阵、tint 与图集 uv 变换。
//布局是后端 ABI，只允许内部使用，不暴露成生成 Span 的元素类型。
struct GpuMeshInstance
{
public:
    //列主序世界矩阵，attribute locations 5..8，每列一个 vec4。
    float32 model[16] = {};
    //法线矩阵的三列，每列补齐成 vec4，locations 9..11。
    float32 normalColumns[12] = {};
    //线性 tint，Alpha 是覆盖率，location 12。
    float32 tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    //offsetU/offsetV/scaleU/scaleV，location 13。
    float32 uvRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
};

//一次 Submit 复制出的提交快照，只在一次 Render 内有效
struct InstanceSubmission
{
public:
    int32 sourceObjectId = 0;
    World* world = nullptr;
    uint64 contentRevision = 0;
    uint64 submissionId = 0;
    InstanceDrawOptions options;
    Ref<Mesh> mesh;
    Ref<Material> material;
    uint32 subMeshIndex = 0;
    List<MeshInstanceData> instances;
    List<DrawParameterData> drawParameters;
    //有指定队列的列表直接绑定实例缓冲
    std::optional<DrawQueue> drawQueue;
    int32 drawSubQueue = 0;
    List<GpuMeshInstance> gpuInstances;
    bounds3 worldBounds;
};
