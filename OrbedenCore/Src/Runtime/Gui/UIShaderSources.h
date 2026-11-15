#pragma once

//UIRenderer 使用的内置着色器源码。覆盖两种绘制：UI 图元本身，以及把裁剪形状写进覆盖率池。
//放在 C++ 里的原因与其它内置 Pass 一致：它们是渲染器的一部分，不参与资源热重载。

namespace UIShaderSources
{
    //UI 顶点：位置在本地空间，逐命令的模型矩阵与视图投影分别传入。
    inline constexpr const char* SurfaceVertexSource = R"(#version 430 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_Uv;
layout(location = 2) in vec4 a_Tint;

uniform mat4 u_Model;
uniform mat4 u_ViewProjection;
uniform vec3 u_ViewportSize;
uniform int u_BitmapPixelSnap;

out vec2 v_Uv;
out vec4 v_Tint;

void main()
{
    v_Uv = a_Uv;
    v_Tint = a_Tint;
    mat4 transform = u_ViewProjection * u_Model;
    gl_Position = transform * vec4(a_Position, 1.0);
    if (u_BitmapPixelSnap == 1)
    {
        vec2 axisX = transform[0].xy * u_ViewportSize.xy;
        vec2 axisY = transform[1].xy * u_ViewportSize.xy;
        if (abs(axisX.y) <= 1e-5 * abs(axisX.x) && abs(axisY.x) <= 1e-5 * abs(axisY.y))
        {
            //对齐屏幕位图字形的像素边界
            vec2 pixel = (gl_Position.xy / gl_Position.w * 0.5 + 0.5) * u_ViewportSize.xy;
            pixel = floor(pixel + 0.5);
            gl_Position.xy = (pixel / u_ViewportSize.xy * 2.0 - 1.0) * gl_Position.w;
        }
    }
})";

    //UI 片元：按材质种类取样，统一输出预乘颜色。
    //u_MaterialKind 取 UIMaterialKind：0 直通图片、1 预乘图片、2 位图字形、3 SDF、4 MSDF。
    inline constexpr const char* SurfaceFragmentSource = R"(#version 430 core
in vec2 v_Uv;
in vec4 v_Tint;

uniform sampler2D u_Texture;
uniform sampler2D u_Coverage;
uniform vec4 u_Tint;
//视口尺寸与原点：gl_FragCoord 相对帧缓冲原点，覆盖率按视口内的相对位置采样。
uniform vec3 u_ViewportSize;
uniform vec3 u_ViewportOrigin;
uniform float u_DistanceRange;
uniform int u_MaterialKind;
uniform int u_BitmapPixelSnap;
//0 表示没有裁剪层，覆盖率恒为 1。
uniform int u_CoverageEnabled;

out vec4 FragColor;

//MSDF 的通道取中位数，SDF 直接取 R。
float Median(vec3 value)
{
    return max(min(value.r, value.g), min(max(value.r, value.g), value.b));
}

//距离场覆盖率：把纹理值按屏幕空间取值范围还原成 0..1 的覆盖。
float DistanceCoverage(float field)
{
    //屏幕上一个像素对应多少 texel：由 UV 导数与图集尺寸得到。
    vec2 unitRange = vec2(u_DistanceRange) / vec2(textureSize(u_Texture, 0));
    vec2 screenTexSize = vec2(1.0) / max(fwidth(v_Uv), vec2(1e-8));
    float screenRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
    return clamp(screenRange * (field - 0.5) + 0.5, 0.0, 1.0);
}

void main()
{
    vec4 tint = u_Tint * v_Tint;
    float coverage = 1.0;
    if (u_CoverageEnabled == 1)
    {
        //覆盖率池与视口同尺寸，按屏幕像素采样。
        coverage = texture(u_Coverage, (gl_FragCoord.xy - u_ViewportOrigin.xy) / u_ViewportSize.xy).r;
    }

    vec4 source;
    if (u_MaterialKind == 3)
    {
        source = vec4(1.0, 1.0, 1.0, DistanceCoverage(texture(u_Texture, v_Uv).r));
    }
    else if (u_MaterialKind == 4)
    {
        source = vec4(1.0, 1.0, 1.0, DistanceCoverage(Median(texture(u_Texture, v_Uv).rgb)));
    }
    else if (u_MaterialKind == 2 && u_BitmapPixelSnap == 1)
    {
        //直接读取屏幕位图字形的纹素覆盖率
        ivec2 atlasSize = textureSize(u_Texture, 0);
        ivec2 texel = clamp(ivec2(floor(v_Uv * vec2(atlasSize))), ivec2(0), atlasSize - ivec2(1));
        source = vec4(1.0, 1.0, 1.0, texelFetch(u_Texture, texel, 0).r);
    }
    else
    {
        //普通图片与位图字形：字形图集是 R8，取样后按白色处理。
        vec4 sampled = texture(u_Texture, v_Uv);
        source = u_MaterialKind == 2 ? vec4(1.0, 1.0, 1.0, sampled.r) : sampled;
    }

    //Alpha 语义：直通按 覆盖率 求预乘，预乘纹理不能再乘一次自身 Alpha。
    float alpha = source.a * tint.a * coverage;
    vec3 rgb = u_MaterialKind == 1
        ? source.rgb * tint.rgb * tint.a * coverage
        : source.rgb * tint.rgb * alpha;
    FragColor = vec4(rgb, alpha);
})";

    //裁剪形状顶点：形状网格本身就是 UI 四边形，复用同一套属性。
    inline constexpr const char* CoverageVertexSource = R"(#version 430 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_Uv;
layout(location = 2) in vec4 a_Tint;

uniform mat4 u_Model;
uniform mat4 u_ViewProjection;

out vec2 v_Uv;

void main()
{
    v_Uv = a_Uv;
    gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
})";

    //裁剪形状片元：写 父覆盖率 * 本层覆盖率，根层的父覆盖率为 1。
    //只写 R 通道：目标是 R8 的覆盖率池。
    inline constexpr const char* CoverageFragmentSource = R"(#version 430 core
in vec2 v_Uv;

uniform sampler2D u_ShapeTexture;
uniform sampler2D u_ParentCoverage;
uniform vec3 u_ViewportSize;
//0 矩形裁剪，1 读 R8 的 R，2 读 RGBA 的 A，3 视为全 1（RGB 纹理）。
uniform int u_ShapeChannelMode;
//0 表示这是根层，父覆盖率恒为 1。
uniform int u_ParentCoverageEnabled;

out vec4 FragColor;

void main()
{
    float own = 1.0;
    if (u_ShapeChannelMode == 1) own = texture(u_ShapeTexture, v_Uv).r;
    else if (u_ShapeChannelMode == 2) own = texture(u_ShapeTexture, v_Uv).a;

    float parent = 1.0;
    if (u_ParentCoverageEnabled == 1)
    {
        parent = texture(u_ParentCoverage, gl_FragCoord.xy / u_ViewportSize.xy).r;
    }

    FragColor = vec4(parent * own, 0.0, 0.0, 1.0);
})";
}
