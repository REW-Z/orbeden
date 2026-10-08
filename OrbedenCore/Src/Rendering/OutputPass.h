#pragma once

#include "Rendering/Backend/GpuResourceIDs.h"
#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/FullscreenQuad.h"

//颜色输出转换：场景执行曝光与显示编码，Overlay 合成执行显示解码与纯编码。
//UI 与场景着色器始终输出线性颜色。
class OutputPass
{
public:
    //输出模式
    enum class Mode : uint32
    {
        //曝光 + AgX 色调映射 + sRGB 编码，常规相机走这条
        Display = 0,
        //跳过曝光与色调映射，只做 sRGB 编码
        EncodeOnly = 1,
        //把显示编码的颜色解码到线性缓冲，供 Overlay 混合
        DecodeOnly = 2,
    };

    //一次输出的全部输入
    struct Parameters
    {
    public:
        //待转换的颜色纹理
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
