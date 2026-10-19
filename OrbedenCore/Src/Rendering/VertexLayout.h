#pragma once

#include "Rendering/Backend/RenderBackend.h"

//顶点属性格式
enum class VertexAttributeFormat : uint8 { Float2, Float3, Float4, UNorm8x4 };

//跨后端顶点属性描述
struct VertexAttribute
{
    uint32 location;
    uint32 binding;
    VertexAttributeFormat format;
    uint32 offset;
};

//顶点与实例输入布局
struct VertexLayout
{
    List<VertexAttribute> attributes;
    uint32 instanceStride = 0;
};

//获取共享顶点输入布局
const VertexLayout& GetVertexLayout(GpuVertexLayout layout);
