#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EnsId.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"

class World;

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
};

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
};
