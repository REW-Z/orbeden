#include "Rendering/VertexLayout.h"

#include <array>
#include <stdexcept>

//获取共享顶点输入布局
const VertexLayout& GetVertexLayout(GpuVertexLayout layout)
{
    using F = VertexAttributeFormat;
    static const std::array<VertexLayout, 7> layouts = []
    {
        std::array<VertexLayout, 7> result;
        result[0].attributes = { {0,0,F::Float3,0}, {1,0,F::Float3,12}, {2,0,F::Float2,24}, {3,0,F::Float3,32} };
        result[1] = result[0]; result[1].instanceStride = 144;
        for (uint32 index = 0; index < 9; ++index) result[1].attributes.push_back({ 5 + index, 1, F::Float4, index * 16 });
        result[2].attributes = { {0,0,F::Float3,0}, {1,0,F::Float3,12}, {2,0,F::Float2,24}, {3,0,F::Float3,32}, {4,0,F::Float4,44} };
        result[3] = result[0]; result[3].instanceStride = 112;
        for (uint32 index = 0; index < 7; ++index) result[3].attributes.push_back({ 5 + index, 1, F::Float4, index * 16 });
        result[4].attributes = { {0,0,F::Float3,0}, {1,0,F::Float2,12}, {2,0,F::Float4,20} };
        result[5].attributes = { {0,0,F::Float3,0} };
        result[6].attributes = { {0,0,F::Float2,0}, {1,0,F::Float2,8}, {2,0,F::UNorm8x4,16} };
        return result;
    }();
    uint32 index = static_cast<uint32>(layout);
    if (index >= layouts.size()) throw std::invalid_argument("Vertex layout is invalid.");
    return layouts[index];
}
