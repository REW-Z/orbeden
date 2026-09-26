#pragma once

#include "Rendering/Backend/GpuResourceIDs.h"

class RenderBackend;

//全屏四边形顶点着色器源码。输出 v_Uv（原点在左下角）与覆盖整个视口的裁剪空间坐标，
//供输出 Pass 与选择描边等全屏绘制共用。
extern const char* const FullscreenQuadVertexSource;

//全屏四边形几何，供需要铺满视口的 Pass 复用。
//后端固定按 位置/法线/uv/切线 取顶点，四边形按同样步长补齐到 11 个 float。
class FullscreenQuad
{
private:
    RenderBackend* backend = nullptr;
    GpuVertexInputID vertexInput;
    GpuVertexBufferID vertexBuffer;
    GpuIndexBufferID indexBuffer;

public:
    //绑定后端；实际资源在第一次 EnsureReady 时创建
    void Initialize(RenderBackend* renderBackend);

    //释放后端资源
    void Shutdown();

    //按需创建顶点与索引缓冲；失败时返回 false
    bool EnsureReady();

    //顶点输入句柄，未就绪时无效
    GpuVertexInputID GetVertexInput() const { return vertexInput; }
};
