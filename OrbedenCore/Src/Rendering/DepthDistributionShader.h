#pragma once

//相机对数深度分布的共享计算阶段
inline constexpr const char* DepthDistributionSource = R"(#version 430 core
layout(local_size_x=16, local_size_y=16) in;
layout(std430, binding=ORBEDEN_DISTRIBUTION_BINDING) buffer Distribution { uint bins[64]; };
uniform sampler2D u_Depth;
uniform mat4 u_InverseProjection;
uniform float u_Near;
uniform float u_Far;
shared uint localBins[64];
void main()
{
    uint lane = gl_LocalInvocationIndex;
    if (lane < 64u) localBins[lane] = 0u;
    barrier();
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = textureSize(u_Depth, 0);
    if (all(lessThan(pixel, size)))
    {
        float depth = texelFetch(u_Depth, pixel, 0).r;
        if (depth >= 0.0 && depth < 1.0)
        {
            vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
            vec4 view = u_InverseProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
            float distance = -view.z / view.w;
            if (!isnan(distance) && !isinf(distance) && distance >= u_Near && distance <= u_Far)
            {
                int bin = clamp(int(log(distance/u_Near) / log(u_Far/u_Near) * 64.0), 0, 63);
                atomicAdd(localBins[bin], 1u);
            }
        }
    }
    barrier();
    if (lane < 64u && localBins[lane] != 0u) atomicAdd(bins[lane], localBins[lane]);
})";
