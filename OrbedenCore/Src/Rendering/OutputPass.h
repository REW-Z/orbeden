#pragma once

#include "Rendering/Backend/GpuResourceIDs.h"
#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/FullscreenQuad.h"

//输出 Pass：把线性 HDR 场景缓冲转换到显示空间，是整个管线的末端。
//只读场景缓冲，因此必须在描边合成与调试线之后调用。
//这里是全引擎唯一做显示编码的地方，着色器不得再自行编码。
class OutputPass
{
public:
    //输出模式。阴影调试视图写出的是显示色，必须绕开色调映射。
    enum class Mode : uint32
    {
        //曝光 + AgX 色调映射 + sRGB 编码，常规相机走这条
        Display = 0,
        //跳过曝光与色调映射，只做 sRGB 编码
        EncodeOnly = 1,
        //原样复制，不做任何转换；调色板要逐位不变时只能用它
        Copy = 2,
    };

    //一次输出的全部输入
    struct Parameters
    {
    public:
        //线性 HDR 场景缓冲的颜色纹理
        GpuTextureID sourceTexture;
        //显示目标，0 表示默认窗口帧缓冲
        GpuRenderTargetID destinationRenderTarget;
        int32 x = 0;
        int32 y = 0;
        int32 width = 0;
        int32 height = 0;
        //线性曝光倍数，仅 Display 模式生效
        float32 exposure = 1.0f;
        Mode mode = Mode::Display;
    };

private:
    RenderBackend* backend = nullptr;
    GpuShaderProgramID program;
    FullscreenQuad quad;
    //内置资源创建失败只报一次
    bool warned = false;

    //按需创建 shader 与全屏四边形
    bool EnsureResources();

public:
    //绑定后端并释放可能属于其它后端的旧资源
    void Initialize(RenderBackend* renderBackend);

    //释放 shader 与全屏四边形
    void Shutdown();

    //按参数输出一次；失败时返回 false，由调用方决定是否跳过
    bool Render(const Parameters& parameters);
};
