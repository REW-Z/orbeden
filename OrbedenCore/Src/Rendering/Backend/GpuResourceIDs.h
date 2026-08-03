#pragma once

#include "Defines/types.h"

//GPU 顶点缓冲对象，在 OpenGL 后端对应 VBO。
struct GpuVertexBufferID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 索引缓冲对象，在 OpenGL 后端对应 EBO/IBO。
struct GpuIndexBufferID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 顶点输入对象，在 OpenGL 后端对应 VAO。
struct GpuVertexInputID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 纹理对象，在 OpenGL 后端对应 texture object。
struct GpuTextureID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 深度纹理对象，在 OpenGL 后端对应 depth texture。
struct GpuDepthTextureID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 立方体纹理对象，在 OpenGL 后端对应 cube map texture。
struct GpuCubeTextureID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU 渲染目标对象，在 OpenGL 后端对应 framebuffer object。
struct GpuRenderTargetID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};

//GPU shader 程序对象，在 OpenGL 后端对应 program object。
struct GpuShaderProgramID
{
public:
    uint32 id = 0;
    bool IsValid() const { return id != 0; }
};
