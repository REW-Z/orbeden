#include "Rendering/Backend/OpenGLRenderBackend.h"

#include "Log/Log.h"
#include "ThirdParty/glad/glad.h"

#include <string>
#include <vector>

namespace
{
    GLenum ToBufferTarget(BufferKind kind)
    {
        return kind == BufferKind::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    }

    GLenum ToTextureFormat(int32 channels)
    {
        if (channels == 1) return GL_RED;
        if (channels == 3) return GL_RGB;
        return GL_RGBA;
    }

    std::string GetShaderLog(uint32 shader)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        if (length <= 1) return std::string();

        std::vector<char> buffer(static_cast<usize>(length));
        glGetShaderInfoLog(shader, length, nullptr, buffer.data());
        return std::string(buffer.data());
    }

    std::string GetProgramLog(uint32 program)
    {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        if (length <= 1) return std::string();

        std::vector<char> buffer(static_cast<usize>(length));
        glGetProgramInfoLog(program, length, nullptr, buffer.data());
        return std::string(buffer.data());
    }

    uint32 CompileShader(GLenum type, const char* source)
    {
        if (!source || source[0] == '\0') return 0;

        uint32 shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint success = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (success != GL_TRUE)
        {
            std::string log = GetShaderLog(shader);
            Log::Error(("OpenGL shader compile failed: " + log).c_str());
            glDeleteShader(shader);
            return 0;
        }

        return shader;
    }
}

bool OpenGLRenderBackend::Initialize(IWindow* window)
{
    if (!context.Initialize(window)) return false;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    return true;
}

void OpenGLRenderBackend::Shutdown()
{
    currentProgram = GpuProgramHandle();
}

void OpenGLRenderBackend::BeginFrame()
{
}

void OpenGLRenderBackend::EndFrame()
{
}

void OpenGLRenderBackend::BeginPass(const RenderPassInfo& info)
{
    glViewport(0, 0, info.width, info.height);

    GLbitfield clearMask = 0;
    if (info.clearMode == ClearMode::SolidColor)
    {
        glClearColor(info.clearColor.r, info.clearColor.g, info.clearColor.b, info.clearColor.a);
        clearMask |= GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
    }
    else if (info.clearMode == ClearMode::DepthOnly)
    {
        clearMask |= GL_DEPTH_BUFFER_BIT;
    }

    if (clearMask != 0)
    {
        glClear(clearMask);
    }
}

void OpenGLRenderBackend::EndPass()
{
}

GpuBufferHandle OpenGLRenderBackend::CreateBuffer(const BufferInfo& info)
{
    if (!info.data || info.size == 0) return GpuBufferHandle();

    GLuint id = 0;
    GLenum target = ToBufferTarget(info.kind);
    glGenBuffers(1, &id);
    glBindBuffer(target, id);
    glBufferData(target, static_cast<GLsizeiptr>(info.size), info.data, GL_STATIC_DRAW);
    glBindBuffer(target, 0);
    return { id };
}

void OpenGLRenderBackend::DeleteBuffer(GpuBufferHandle handle)
{
    if (!handle.IsValid()) return;

    GLuint id = handle.id;
    glDeleteBuffers(1, &id);
}

GpuVertexArrayHandle OpenGLRenderBackend::CreateVertexArray(const VertexLayoutInfo& info)
{
    if (!info.vertexBuffer.IsValid() || !info.indexBuffer.IsValid() || info.stride == 0) return GpuVertexArrayHandle();

    GLuint id = 0;
    glGenVertexArrays(1, &id);
    glBindVertexArray(id);
    glBindBuffer(GL_ARRAY_BUFFER, info.vertexBuffer.id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, info.indexBuffer.id);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, info.stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, info.stride, reinterpret_cast<void*>(sizeof(float32) * 3));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, info.stride, reinterpret_cast<void*>(sizeof(float32) * 6));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, info.stride, reinterpret_cast<void*>(sizeof(float32) * 8));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return { id };
}

void OpenGLRenderBackend::DeleteVertexArray(GpuVertexArrayHandle handle)
{
    if (!handle.IsValid()) return;

    GLuint id = handle.id;
    glDeleteVertexArrays(1, &id);
}

GpuTextureHandle OpenGLRenderBackend::CreateTexture(const TextureInfo& info)
{
    if (info.width <= 0 || info.height <= 0 || !info.pixels) return GpuTextureHandle();

    GLenum format = ToTextureFormat(info.channels);
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, format, info.width, info.height, 0, format, GL_UNSIGNED_BYTE, info.pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return { id };
}

void OpenGLRenderBackend::DeleteTexture(GpuTextureHandle handle)
{
    if (!handle.IsValid()) return;

    GLuint id = handle.id;
    glDeleteTextures(1, &id);
}

GpuProgramHandle OpenGLRenderBackend::CreateProgram(const ProgramInfo& info)
{
    uint32 vertexShader = CompileShader(GL_VERTEX_SHADER, info.vertexSource);
    uint32 fragmentShader = CompileShader(GL_FRAGMENT_SHADER, info.fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);
        return GpuProgramHandle();
    }

    uint32 program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success != GL_TRUE)
    {
        std::string log = GetProgramLog(program);
        Log::Error(("OpenGL program link failed: " + log).c_str());
        glDeleteProgram(program);
        return GpuProgramHandle();
    }

    return { program };
}

void OpenGLRenderBackend::DeleteProgram(GpuProgramHandle handle)
{
    if (!handle.IsValid()) return;

    glDeleteProgram(handle.id);
    if (currentProgram.id == handle.id) currentProgram = GpuProgramHandle();
}

void OpenGLRenderBackend::BindProgram(GpuProgramHandle handle)
{
    currentProgram = handle;
    glUseProgram(handle.id);
}

void OpenGLRenderBackend::BindVertexArray(GpuVertexArrayHandle handle)
{
    glBindVertexArray(handle.id);
}

void OpenGLRenderBackend::BindTexture(uint32 slot, GpuTextureHandle handle)
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, handle.id);
}

void OpenGLRenderBackend::SetUniformMatrix4(const char* name, const matrix4x4& value)
{
    int32 location = GetUniformLocation(name);
    if (location < 0) return;

    glUniformMatrix4fv(location, 1, GL_FALSE, value.m);
}

void OpenGLRenderBackend::SetUniformVector3(const char* name, const vector3& value)
{
    int32 location = GetUniformLocation(name);
    if (location < 0) return;

    glUniform3f(location, value.x, value.y, value.z);
}

void OpenGLRenderBackend::SetUniformColor4(const char* name, const color4& value)
{
    int32 location = GetUniformLocation(name);
    if (location < 0) return;

    glUniform4f(location, value.r, value.g, value.b, value.a);
}

void OpenGLRenderBackend::SetUniformInt(const char* name, int32 value)
{
    int32 location = GetUniformLocation(name);
    if (location < 0) return;

    glUniform1i(location, value);
}

void OpenGLRenderBackend::DrawIndexed(uint32 indexStart, uint32 indexCount)
{
    const void* offset = reinterpret_cast<const void*>(static_cast<uintptr>(indexStart) * sizeof(uint32));
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, offset);
}

int32 OpenGLRenderBackend::GetUniformLocation(const char* name) const
{
    if (!currentProgram.IsValid() || !name) return -1;

    return glGetUniformLocation(currentProgram.id, name);
}

