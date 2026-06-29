#include "Rendering/Backend/OpenGLRenderBackend.h"

#include "Log/Log.h"
#include <glad/gl.h>

#include <string>
#include <vector>

namespace
{
    const char* ToOpenGLErrorName(GLenum error)
    {
        switch (error)
        {
        case GL_NO_ERROR: return "GL_NO_ERROR";
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
        default: return "UNKNOWN_GL_ERROR";
        }
    }

    void LogOpenGLError(const char* stage)
    {
        GLenum error = glGetError();
        if (error == GL_NO_ERROR) return;

        std::string message = "OpenGL error after ";
        message += stage ? stage : "unknown stage";
        message += ": ";
        message += ToOpenGLErrorName(error);
        message += " (";
        message += std::to_string(error);
        message += ")";
        Log::Error(message.c_str());
    }

    const char* ToOpenGLString(const GLubyte* value)
    {
        return value ? reinterpret_cast<const char*>(value) : "unknown";
    }

    uint32 CreateOpenGLBuffer(GLenum target, const GpuBufferDesc& desc)
    {
        if (!desc.data || desc.size == 0) return 0;

        GLuint id = 0;
        glGenBuffers(1, &id);
        glBindBuffer(target, id);
        glBufferData(target, static_cast<GLsizeiptr>(desc.size), desc.data, GL_STATIC_DRAW);
        glBindBuffer(target, 0);
        return id;
    }

    void DeleteOpenGLBuffer(uint32 buffer)
    {
        if (buffer == 0) return;

        GLuint id = buffer;
        glDeleteBuffers(1, &id);
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

    std::string rendererInfo = "OpenGL renderer: ";
    rendererInfo += ToOpenGLString(glGetString(GL_VENDOR));
    rendererInfo += " | ";
    rendererInfo += ToOpenGLString(glGetString(GL_RENDERER));
    rendererInfo += " | ";
    rendererInfo += ToOpenGLString(glGetString(GL_VERSION));
    Log::Info(rendererInfo.c_str());

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    LogOpenGLError("OpenGLRenderBackend::Initialize");
    return true;
}

void OpenGLRenderBackend::Shutdown()
{
    for (auto& pair : renderTargetColorAttachments)
    {
        GLuint texture = pair.second;
        glDeleteTextures(1, &texture);
    }

    renderTargetColorAttachments.clear();
    indexBufferCounts.clear();
    vertexInputIndexBuffers.clear();
    vertexInputIndexCounts.clear();
    currentVertexInput = GpuVertexInputID();
    currentShaderProgram = GpuShaderProgramID();
}

void OpenGLRenderBackend::BeginFrame()
{
}

void OpenGLRenderBackend::EndFrame()
{
}

void OpenGLRenderBackend::BeginPass(const RenderPassDesc& desc)
{
    glBindFramebuffer(GL_FRAMEBUFFER, desc.renderTarget.id);
    glViewport(0, 0, desc.width, desc.height);

    GLbitfield clearMask = 0;
    if (desc.clearMode == ClearMode::SolidColor)
    {
        glClearColor(desc.clearColor.r, desc.clearColor.g, desc.clearColor.b, desc.clearColor.a);
        clearMask |= GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
    }
    else if (desc.clearMode == ClearMode::DepthOnly)
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

GpuVertexBufferID OpenGLRenderBackend::CreateVertexBuffer(const GpuBufferDesc& desc)
{
    return { CreateOpenGLBuffer(GL_ARRAY_BUFFER, desc) };
}

void OpenGLRenderBackend::DeleteVertexBuffer(GpuVertexBufferID id)
{
    DeleteOpenGLBuffer(id.id);
}

GpuIndexBufferID OpenGLRenderBackend::CreateIndexBuffer(const GpuBufferDesc& desc)
{
    uint32 id = CreateOpenGLBuffer(GL_ARRAY_BUFFER, desc);
    if (id != 0)
    {
        indexBufferCounts[id] = static_cast<uint32>(desc.size / sizeof(uint32));
    }
    return { id };
}

void OpenGLRenderBackend::DeleteIndexBuffer(GpuIndexBufferID id)
{
    indexBufferCounts.erase(id.id);
    DeleteOpenGLBuffer(id.id);
}

GpuVertexInputID OpenGLRenderBackend::CreateVertexInput(const GpuVertexInputDesc& desc)
{
    if (!desc.vertexBuffer.IsValid() || !desc.indexBuffer.IsValid() || desc.stride == 0) return GpuVertexInputID();

    auto indexCountIt = indexBufferCounts.find(desc.indexBuffer.id);
    if (indexCountIt == indexBufferCounts.end() || indexCountIt->second == 0) return GpuVertexInputID();

    GLuint id = 0;
    glGenVertexArrays(1, &id);
    glBindVertexArray(id);
    glBindBuffer(GL_ARRAY_BUFFER, desc.vertexBuffer.id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, desc.indexBuffer.id);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(sizeof(float32) * 3));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(sizeof(float32) * 6));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(sizeof(float32) * 8));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    vertexInputIndexBuffers[id] = desc.indexBuffer.id;
    vertexInputIndexCounts[id] = indexCountIt->second;
    return { id };
}

void OpenGLRenderBackend::DeleteVertexInput(GpuVertexInputID id)
{
    if (!id.IsValid()) return;

    vertexInputIndexBuffers.erase(id.id);
    vertexInputIndexCounts.erase(id.id);
    GLuint vertexInput = id.id;
    glDeleteVertexArrays(1, &vertexInput);
}

GpuTextureID OpenGLRenderBackend::CreateTexture(const GpuTextureDesc& desc)
{
    if (desc.width <= 0 || desc.height <= 0 || !desc.pixels) return GpuTextureID();

    GLenum format = ToTextureFormat(desc.channels);
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, format, desc.width, desc.height, 0, format, GL_UNSIGNED_BYTE, desc.pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return { id };
}

void OpenGLRenderBackend::DeleteTexture(GpuTextureID id)
{
    if (!id.IsValid()) return;

    GLuint texture = id.id;
    glDeleteTextures(1, &texture);
}

GpuDepthTextureID OpenGLRenderBackend::CreateDepthTexture(const GpuDepthTextureDesc& desc)
{
    if (desc.width <= 0 || desc.height <= 0) return GpuDepthTextureID();

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, desc.width, desc.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    return { id };
}

void OpenGLRenderBackend::DeleteDepthTexture(GpuDepthTextureID id)
{
    if (!id.IsValid()) return;

    GLuint texture = id.id;
    glDeleteTextures(1, &texture);
}

GpuCubeTextureID OpenGLRenderBackend::CreateCubeTexture(const GpuCubeTextureDesc& desc)
{
    if (desc.width <= 0 || desc.height <= 0) return GpuCubeTextureID();

    GLenum format = ToTextureFormat(desc.channels);
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    for (uint32 face = 0; face < 6; ++face)
    {
        if (!desc.faces[face])
        {
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glDeleteTextures(1, &id);
            return GpuCubeTextureID();
        }

        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, format, desc.width, desc.height, 0, format, GL_UNSIGNED_BYTE, desc.faces[face]);
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return { id };
}

void OpenGLRenderBackend::DeleteCubeTexture(GpuCubeTextureID id)
{
    if (!id.IsValid()) return;

    GLuint texture = id.id;
    glDeleteTextures(1, &texture);
}

GpuRenderTargetID OpenGLRenderBackend::CreateRenderTarget(const GpuRenderTargetDesc& desc)
{
    if (!desc.depthTexture.IsValid() || desc.width <= 0 || desc.height <= 0) return GpuRenderTargetID();

    GLuint colorTexture = 0;
    glGenTextures(1, &colorTexture);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, desc.width, desc.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLuint id = 0;
    glGenFramebuffers(1, &id);
    glBindFramebuffer(GL_FRAMEBUFFER, id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, desc.depthTexture.id, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_NONE);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Log::Error("OpenGL framebuffer creation failed.");
        glDeleteFramebuffers(1, &id);
        glDeleteTextures(1, &colorTexture);
        return GpuRenderTargetID();
    }

    renderTargetColorAttachments[id] = colorTexture;
    return { id };
}

void OpenGLRenderBackend::DeleteRenderTarget(GpuRenderTargetID id)
{
    if (!id.IsValid()) return;

    GLuint framebuffer = id.id;
    glDeleteFramebuffers(1, &framebuffer);

    auto it = renderTargetColorAttachments.find(id.id);
    if (it != renderTargetColorAttachments.end())
    {
        GLuint colorTexture = it->second;
        glDeleteTextures(1, &colorTexture);
        renderTargetColorAttachments.erase(it);
    }
}

GpuShaderProgramID OpenGLRenderBackend::CreateShaderProgram(const GpuShaderProgramDesc& desc)
{
    uint32 vertexShader = CompileShader(GL_VERTEX_SHADER, desc.vertexSource);
    uint32 fragmentShader = CompileShader(GL_FRAGMENT_SHADER, desc.fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);
        return GpuShaderProgramID();
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
        return GpuShaderProgramID();
    }

    return { program };
}

void OpenGLRenderBackend::DeleteShaderProgram(GpuShaderProgramID id)
{
    if (!id.IsValid()) return;

    glDeleteProgram(id.id);
    if (currentShaderProgram.id == id.id) currentShaderProgram = GpuShaderProgramID();
}

void OpenGLRenderBackend::BindShaderProgram(GpuShaderProgramID id)
{
    currentShaderProgram = id;
    glUseProgram(id.id);
}

void OpenGLRenderBackend::BindVertexInput(GpuVertexInputID id)
{
    currentVertexInput = id;
    glBindVertexArray(id.id);
    if (id.IsValid())
    {
        auto it = vertexInputIndexBuffers.find(id.id);
        if (it != vertexInputIndexBuffers.end())
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second);
        }
    }
}

void OpenGLRenderBackend::BindTexture(uint32 slot, GpuTextureID id)
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, id.id);
}

void OpenGLRenderBackend::BindDepthTexture(uint32 slot, GpuDepthTextureID id)
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, id.id);
}

void OpenGLRenderBackend::BindCubeTexture(uint32 slot, GpuCubeTextureID id)
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id.id);
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

void OpenGLRenderBackend::SetUniformColor(const char* name, const color& value)
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

void OpenGLRenderBackend::SetUniformFloat(const char* name, float32 value)
{
    int32 location = GetUniformLocation(name);
    if (location < 0) return;

    glUniform1f(location, value);
}

void OpenGLRenderBackend::SetDepthTest(bool enabled)
{
    if (enabled)
    {
        glEnable(GL_DEPTH_TEST);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
}

void OpenGLRenderBackend::SetDepthWrite(bool enabled)
{
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}

void OpenGLRenderBackend::DrawIndexed(uint32 indexStart, uint32 indexCount)
{
    if (!currentVertexInput.IsValid() || indexCount == 0) return;

    auto bufferIt = vertexInputIndexBuffers.find(currentVertexInput.id);
    if (bufferIt == vertexInputIndexBuffers.end() || bufferIt->second == 0)
    {
        Log::Error("OpenGL draw skipped: vertex input has no index buffer.");
        return;
    }

    auto countIt = vertexInputIndexCounts.find(currentVertexInput.id);
    if (countIt != vertexInputIndexCounts.end())
    {
        usize start = static_cast<usize>(indexStart);
        usize count = static_cast<usize>(indexCount);
        usize available = static_cast<usize>(countIt->second);
        if (start > available || count > available - start)
        {
            Log::Error("OpenGL draw skipped: index range exceeds index buffer.");
            return;
        }
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferIt->second);
    const void* offset = reinterpret_cast<const void*>(static_cast<uintptr>(indexStart) * sizeof(uint32));
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, offset);
}

int32 OpenGLRenderBackend::GetUniformLocation(const char* name) const
{
    if (!currentShaderProgram.IsValid() || !name) return -1;

    return glGetUniformLocation(currentShaderProgram.id, name);
}
