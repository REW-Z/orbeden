#pragma once

#include "Defines/types.h"

//附加窗口的呈现表面，0 表示主窗口
struct GpuSurfaceID
{
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//异步深度分布统计对象
struct GpuDepthDistributionID
{
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 顶点缓冲对象的不透明句柄
struct GpuVertexBufferID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 索引缓冲对象的不透明句柄
struct GpuIndexBufferID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 顶点输入对象的不透明句柄
struct GpuVertexInputID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 纹理对象的不透明句柄
struct GpuTextureID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 深度纹理对象的不透明句柄
struct GpuDepthTextureID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 立方体纹理对象的不透明句柄
struct GpuCubeTextureID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 渲染目标对象的不透明句柄
struct GpuRenderTargetID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU shader 程序对象的不透明句柄
struct GpuShaderProgramID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
    bool operator==(const GpuShaderProgramID& other) const { return id == other.id; }
    bool operator!=(const GpuShaderProgramID& other) const { return id != other.id; }
};
