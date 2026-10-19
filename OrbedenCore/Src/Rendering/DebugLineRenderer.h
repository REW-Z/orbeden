#pragma once

#include "Rendering/Backend/RenderBackend.h"

//提交世界空间调试线
class DebugLineRenderer
{
private:
    RenderBackend* backend = nullptr;
    GpuShaderProgramID program;
    GpuVertexBufferID vertices;
    GpuIndexBufferID indices;
    GpuVertexInputID input;
    usize capacity = 0;

public:
    //绑定渲染后端
    void Initialize(RenderBackend* value);
    //释放调试绘制资源
    void Shutdown();
    //绘制相机可见的线条
    void Render(const List<DebugLine>& lines, const matrix4x4& viewProjection, uint32 layerMask);
};
