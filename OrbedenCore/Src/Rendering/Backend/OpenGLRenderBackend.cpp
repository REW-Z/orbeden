#include "Rendering/Backend/OpenGLRenderBackend.h"

#include "Log/Log.h"
#include "Rendering/DrawBatchBuilder.h"
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

    //GLsizei 能表达的最大元素或实例数量上界
    constexpr uint32 MaxGlSizei = 0x7FFFFFFFu;

    uint32 CreateOpenGLBuffer(GLenum target, const GpuBufferDesc& desc)
    {
        if (desc.size == 0) return 0;
        //静态缓冲必须一次给足数据；流式缓冲允许只按容量分配，之后由流式上传填充。
        if (!desc.data && desc.usage != GpuBufferUsage::Stream) return 0;

        GLuint id = 0;
        glGenBuffers(1, &id);
        glBindBuffer(target, id);
        glBufferData(target, static_cast<GLsizeiptr>(desc.size), desc.data,
            desc.usage == GpuBufferUsage::Stream ? GL_STREAM_DRAW : GL_STATIC_DRAW);
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

    //sRGB 只定义了 3 与 4 通道变体，单通道没有对应格式，只能保持线性。
    GLenum ToSrgbTextureFormat(int32 channels)
    {
        if (channels == 3) return GL_SRGB8;
        if (channels == 4) return GL_SRGB8_ALPHA8;
        return ToTextureFormat(channels);
    }

    GLenum ToRenderTargetFormat(GpuRenderTargetFormat format)
    {
        return format == GpuRenderTargetFormat::RGBA16F ? GL_RGBA16F : GL_RGBA8;
    }

    GLenum ToRenderTargetSourceType(GpuRenderTargetFormat format)
    {
        return format == GpuRenderTargetFormat::RGBA16F ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
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
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);

    currentShaderProgram = GpuShaderProgramID();
    currentVertexInput = GpuVertexInputID();
    currentTextureSlot = 0;
    depthTestEnabled = true;
    depthWriteEnabled = true;
    blendEnabled = true;
    cullMode = CullMode::None;
    blendMode = BlendMode::Alpha;
    boundInstanceBuffer = GpuVertexBufferID();
    boundInstanceOffset = 0;
    boundTexture2Ds.clear();
    boundCubeTextures.clear();
    uniformLocations.clear();

    //实例化能力在此判定一次：网格顶点 4 个属性加实例属性 10 个，共需 14 个 attribute 槽位。
    instancingSupported = false;
    GLint maxVertexAttributes = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttributes);
    instancingSupported = maxVertexAttributes >= 14;
    if (!instancingSupported)
    {
        Log::Warning("OpenGL backend: GL_MAX_VERTEX_ATTRIBS is below 14, instanced drawing is disabled.");
    }

    //片元纹理槽上限在此查询一次，供需要额外纹理槽的功能判断能否绑定
    GLint maxFragmentTextures = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxFragmentTextures);
    fragmentTextureUnitCount = static_cast<uint32>(std::max(maxFragmentTextures, 0));

    LogOpenGLError("OpenGLRenderBackend::Initialize");
    return true;
}

void OpenGLRenderBackend::Shutdown()
{
    while (!depthDistributions.empty()) DeleteDepthDistribution({ depthDistributions.begin()->first });
    DeleteShaderProgram(depthDistributionShader);
    depthDistributionShader = {};
    DeleteShaderProgram(debugLineShader);
    debugLineShader = {};
    if (debugLineVertexArray) glDeleteVertexArrays(1, &debugLineVertexArray);
    if (debugLineVertexBuffer) glDeleteBuffers(1, &debugLineVertexBuffer);
    debugLineVertexArray = 0;
    debugLineVertexBuffer = 0;
    for (auto& pair : renderTargetColorAttachments)
    {
        GLuint framebuffer = pair.first;
        glDeleteFramebuffers(1, &framebuffer);
        if (pair.second != 0)
        {
            GLuint texture = pair.second;
            glDeleteTextures(1, &texture);
        }
    }

    renderTargetColorAttachments.clear();
    indexBufferCounts.clear();
    vertexBufferCapacities.clear();
    indexBufferCapacities.clear();
    vertexInputIndexBuffers.clear();
    vertexInputIndexCounts.clear();
    vertexInputLayouts.clear();
    boundTexture2Ds.clear();
    boundCubeTextures.clear();
    uniformLocations.clear();
    currentVertexInput = GpuVertexInputID();
    currentShaderProgram = GpuShaderProgramID();
    currentTextureSlot = 0;
    depthTestEnabled = false;
    depthWriteEnabled = false;
    blendEnabled = false;
    cullMode = CullMode::None;
    blendMode = BlendMode::Alpha;
    boundInstanceBuffer = GpuVertexBufferID();
    boundInstanceOffset = 0;
    instancingSupported = false;
}

void OpenGLRenderBackend::BeginFrame()
{
    //每帧基线是普通 Alpha 混合，避免上一帧的加法方程污染天空盒、描边与 GUI。
    SetBlendMode(BlendMode::Alpha);
}

void OpenGLRenderBackend::EndFrame()
{
}

void OpenGLRenderBackend::BeginPass(const RenderPassDesc& desc)
{
    glBindFramebuffer(GL_FRAMEBUFFER, desc.renderTarget.id);
    glViewport(desc.x, desc.y, desc.width, desc.height);

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
    else if (desc.clearMode == ClearMode::ColorOnly)
    {
        glClearColor(desc.clearColor.r, desc.clearColor.g, desc.clearColor.b, desc.clearColor.a);
        clearMask |= GL_COLOR_BUFFER_BIT;
    }

    if (clearMask != 0)
    {
        bool restoreDepthMask = (clearMask & GL_DEPTH_BUFFER_BIT) != 0 && !depthWriteEnabled;
        if (restoreDepthMask) glDepthMask(GL_TRUE);
        glEnable(GL_SCISSOR_TEST);
        glScissor(desc.x, desc.y, desc.width, desc.height);
        glClear(clearMask);
        glDisable(GL_SCISSOR_TEST);
        if (restoreDepthMask) glDepthMask(GL_FALSE);
    }
}

void OpenGLRenderBackend::EndPass()
{
}

GpuVertexBufferID OpenGLRenderBackend::CreateVertexBuffer(const GpuBufferDesc& desc)
{
    uint32 id = CreateOpenGLBuffer(GL_ARRAY_BUFFER, desc);
    if (id != 0) vertexBufferCapacities[id] = desc.size;
    return { id };
}

void OpenGLRenderBackend::DeleteVertexBuffer(GpuVertexBufferID id)
{
    vertexBufferCapacities.erase(id.id);
    if (boundInstanceBuffer.id == id.id) boundInstanceBuffer = GpuVertexBufferID();
    DeleteOpenGLBuffer(id.id);
}

GpuIndexBufferID OpenGLRenderBackend::CreateIndexBuffer(const GpuBufferDesc& desc)
{
    uint32 id = CreateOpenGLBuffer(GL_ARRAY_BUFFER, desc);
    if (id != 0)
    {
        indexBufferCounts[id] = static_cast<uint32>(desc.size / sizeof(uint32));
        indexBufferCapacities[id] = desc.size;
    }
    return { id };
}

void OpenGLRenderBackend::DeleteIndexBuffer(GpuIndexBufferID id)
{
    indexBufferCounts.erase(id.id);
    indexBufferCapacities.erase(id.id);
    DeleteOpenGLBuffer(id.id);
}

GpuVertexInputID OpenGLRenderBackend::CreateVertexInput(const GpuVertexInputDesc& desc)
{
    if (!desc.vertexBuffer.IsValid() || !desc.indexBuffer.IsValid() || desc.stride == 0) return GpuVertexInputID();

    auto indexCountIt = indexBufferCounts.find(desc.indexBuffer.id);
    if (indexCountIt == indexBufferCounts.end() || indexCountIt->second == 0) return GpuVertexInputID();

    //展开顶点固定 5 个属性，最大的属性在偏移 44、长 16 字节，步长必须容得下整条记录。
    if (desc.layout == GpuVertexLayout::Expanded && desc.stride < static_cast<uint32>(sizeof(float32) * 15))
    {
        Log::Error("OpenGL vertex input creation failed: expanded layout stride is too small.");
        return GpuVertexInputID();
    }

    GLuint id = 0;
    glGenVertexArrays(1, &id);
    glBindVertexArray(id);
    glBindBuffer(GL_ARRAY_BUFFER, desc.vertexBuffer.id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, desc.indexBuffer.id);

    if (desc.layout == GpuVertexLayout::Expanded)
    {
        //展开顶点已经是世界空间，只读自己的顶点缓冲，不引用实例缓冲。
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(12));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(24));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(32));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(44));
        glEnableVertexAttribArray(4);
    }
    else
    {
        //网格顶点属性。实例布局在这里只建立这一半，实例属性留给 BindInstanceBuffer，
        //普通 Mesh 布局也不会被 divisor 污染：两种布局使用各自的 VAO。
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(sizeof(float32) * 3));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(sizeof(float32) * 6));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, desc.stride, reinterpret_cast<void*>(sizeof(float32) * 8));
        glEnableVertexAttribArray(3);
    }

    glBindVertexArray(currentVertexInput.id);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    vertexInputIndexBuffers[id] = desc.indexBuffer.id;
    vertexInputIndexCounts[id] = indexCountIt->second;
    vertexInputLayouts[id] = desc.layout;
    return { id };
}

void OpenGLRenderBackend::DeleteVertexInput(GpuVertexInputID id)
{
    if (!id.IsValid()) return;

    if (currentVertexInput.id == id.id)
    {
        glBindVertexArray(0);
        currentVertexInput = GpuVertexInputID();
        boundInstanceBuffer = GpuVertexBufferID();
    }

    vertexInputIndexBuffers.erase(id.id);
    vertexInputIndexCounts.erase(id.id);
    vertexInputLayouts.erase(id.id);
    GLuint vertexInput = id.id;
    glDeleteVertexArrays(1, &vertexInput);
}

GpuTextureID OpenGLRenderBackend::CreateTexture(const GpuTextureDesc& desc)
{
    if (desc.width <= 0 || desc.height <= 0 || !desc.pixels) return GpuTextureID();

    GLenum format = ToTextureFormat(desc.channels);
    //像素字节保持不变，sRGB 内部格式让采样阶段由硬件解码到线性。
    GLenum internalFormat = desc.srgb ? ToSrgbTextureFormat(desc.channels) : format;
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, desc.width, desc.height, 0, format, GL_UNSIGNED_BYTE, desc.pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    boundTexture2Ds[currentTextureSlot] = 0;
    return { id };
}

void OpenGLRenderBackend::DeleteTexture(GpuTextureID id)
{
    if (!id.IsValid()) return;

    InvalidateTexture2D(id.id);
    GLuint texture = id.id;
    glDeleteTextures(1, &texture);
}

GpuDepthTextureID OpenGLRenderBackend::CreateDepthTexture(const GpuDepthTextureDesc& desc)
{
    if (desc.width <= 0 || desc.height <= 0) return GpuDepthTextureID();
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (desc.width > maxSize || desc.height > maxSize)
    {
        Log::Error("Depth texture exceeds GL_MAX_TEXTURE_SIZE.");
        return {};
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, desc.floatingPoint ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT24, desc.width, desc.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    boundTexture2Ds[currentTextureSlot] = 0;
    return { id };
}

void OpenGLRenderBackend::DeleteDepthTexture(GpuDepthTextureID id)
{
    if (!id.IsValid()) return;

    InvalidateTexture2D(id.id);
    GLuint texture = id.id;
    glDeleteTextures(1, &texture);
}

GpuCubeTextureID OpenGLRenderBackend::CreateCubeTexture(const GpuCubeTextureDesc& desc)
{
    if (desc.width <= 0 || desc.height != desc.width || desc.channels < 1 || desc.channels > 4) return GpuCubeTextureID();
    for (const uint8* face : desc.faces) if (!face) return GpuCubeTextureID();

    GLenum format = ToTextureFormat(desc.channels);
    GLenum internalFormat = desc.srgb ? ToSrgbTextureFormat(desc.channels) : format;
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, desc.generateMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    GLint unpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (uint32 face = 0; face < 6; ++face)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, internalFormat, desc.width, desc.height, 0, format, GL_UNSIGNED_BYTE, desc.faces[face]);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
    if (desc.generateMipmaps) glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    boundCubeTextures[currentTextureSlot] = 0;
    return { id };
}

void OpenGLRenderBackend::DeleteCubeTexture(GpuCubeTextureID id)
{
    if (!id.IsValid()) return;

    for (auto& pair : boundCubeTextures)
    {
        if (pair.second == id.id) pair.second = 0;
    }

    GLuint texture = id.id;
    glDeleteTextures(1, &texture);
}

GpuRenderTargetID OpenGLRenderBackend::CreateRenderTarget(const GpuRenderTargetDesc& desc)
{
    //颜色专用目标不带深度，且必须与纯深度目标互斥；其余目标仍要求调用方提供深度纹理
    if (desc.width <= 0 || desc.height <= 0) return GpuRenderTargetID();
    if (desc.depthOnly && desc.colorOnly) return GpuRenderTargetID();
    if (desc.colorOnly && desc.depthTexture.IsValid()) return GpuRenderTargetID();
    if (!desc.colorOnly && !desc.depthTexture.IsValid()) return GpuRenderTargetID();
    const bool colorOnly = desc.colorOnly;

    GLuint colorTexture = 0;
    if (!desc.depthOnly)
    {
        glGenTextures(1, &colorTexture);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        GLint colorFilter = desc.linearColorFilter ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, colorFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, colorFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, ToRenderTargetFormat(desc.format), desc.width, desc.height, 0, GL_RGBA, ToRenderTargetSourceType(desc.format), nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        boundTexture2Ds[currentTextureSlot] = 0;
    }

    GLuint id = 0;
    glGenFramebuffers(1, &id);
    glBindFramebuffer(GL_FRAMEBUFFER, id);
    if (!colorOnly) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, desc.depthTexture.id, 0);
    if (desc.depthOnly)
    {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
    else
    {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Log::Error("OpenGL framebuffer creation failed.");
        glDeleteFramebuffers(1, &id);
        if (colorTexture != 0) glDeleteTextures(1, &colorTexture);
        return GpuRenderTargetID();
    }

    renderTargetColorAttachments[id] = colorTexture;
    renderTargetFormats[id] = desc.format;
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
        if (colorTexture != 0)
        {
            InvalidateTexture2D(colorTexture);
            glDeleteTextures(1, &colorTexture);
        }
        renderTargetColorAttachments.erase(it);
    }

    renderTargetFormats.erase(id.id);
}

GpuTextureID OpenGLRenderBackend::GetRenderTargetColorTexture(GpuRenderTargetID id) const
{
    auto it = renderTargetColorAttachments.find(id.id);
    if (it == renderTargetColorAttachments.end() || it->second == 0) return GpuTextureID();

    return { it->second };
}

//复制渲染目标颜色和深度
bool OpenGLRenderBackend::CopyRenderTarget(const GpuRenderTargetCopyDesc& desc)
{
    if (!desc.destinationRenderTarget.IsValid() || desc.width <= 0 || desc.height <= 0) return false;

    //验证拷贝目标
    auto destination = renderTargetColorAttachments.find(desc.destinationRenderTarget.id);
    if (destination == renderTargetColorAttachments.end() || destination->second == 0) return false;
    if (desc.sourceRenderTarget.IsValid())
    {
        auto source = renderTargetColorAttachments.find(desc.sourceRenderTarget.id);
        if (source == renderTargetColorAttachments.end() || source->second == 0) return false;

        //相机快照必须同级格式，否则 blit 会静默把 HDR 高光钳到 [0,1]。
        //colorOnly 用于把显示目标搬进场景缓冲，此时由窄到宽是有意为之。
        if (!desc.colorOnly)
        {
            auto sourceFormat = renderTargetFormats.find(desc.sourceRenderTarget.id);
            auto destinationFormat = renderTargetFormats.find(desc.destinationRenderTarget.id);
            if (sourceFormat != renderTargetFormats.end() && destinationFormat != renderTargetFormats.end() &&
                sourceFormat->second != destinationFormat->second)
            {
                Log::Error("OpenGL render target copy skipped: source and destination formats differ.");
                return false;
            }
        }
    }

    //拷贝颜色和深度附件
    glBindFramebuffer(GL_READ_FRAMEBUFFER, desc.sourceRenderTarget.id);
    glReadBuffer(desc.sourceRenderTarget.IsValid() ? GL_COLOR_ATTACHMENT0 : GL_BACK);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, desc.destinationRenderTarget.id);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    GLbitfield blitMask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
    if (desc.colorOnly) blitMask = GL_COLOR_BUFFER_BIT;
    glBlitFramebuffer(
        desc.sourceX,
        desc.sourceY,
        desc.sourceX + desc.width,
        desc.sourceY + desc.height,
        0,
        0,
        desc.width,
        desc.height,
        blitMask,
        GL_NEAREST);

    GLenum error = glGetError();

    //恢复主 Pass 帧缓冲
    glBindFramebuffer(GL_FRAMEBUFFER, desc.sourceRenderTarget.id);
    if (desc.sourceRenderTarget.IsValid())
    {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    else
    {
        glReadBuffer(GL_BACK);
    }

    if (error == GL_NO_ERROR) return true;

    std::string message = "OpenGL camera texture copy failed: ";
    message += ToOpenGLErrorName(error);
    Log::Error(message.c_str());
    return false;
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

    if (currentShaderProgram.id == id.id)
    {
        glUseProgram(0);
        currentShaderProgram = GpuShaderProgramID();
    }

    glDeleteProgram(id.id);
    uniformLocations.erase(id.id);
}

void OpenGLRenderBackend::BindShaderProgram(GpuShaderProgramID id)
{
    if (currentShaderProgram.id == id.id) return;

    currentShaderProgram = id;
    glUseProgram(id.id);
}

void OpenGLRenderBackend::BindVertexInput(GpuVertexInputID id)
{
    if (currentVertexInput.id == id.id) return;

    currentVertexInput = id;
    //实例 attribute 属于 VAO 自身状态，换 VAO 后原来的实例缓冲只对旧 VAO 有效。
    boundInstanceBuffer = GpuVertexBufferID();
    boundInstanceOffset = 0;
    glBindVertexArray(id.id);
}

void OpenGLRenderBackend::BindTexture(uint32 slot, GpuTextureID id)
{
    BindTexture2D(slot, id.id);
}

void OpenGLRenderBackend::BindDepthTexture(uint32 slot, GpuDepthTextureID id)
{
    BindTexture2D(slot, id.id);
}

void OpenGLRenderBackend::BindCubeTexture(uint32 slot, GpuCubeTextureID id)
{
    auto it = boundCubeTextures.find(slot);
    if (it != boundCubeTextures.end() && it->second == id.id) return;

    ActivateTextureSlot(slot);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id.id);
    boundCubeTextures[slot] = id.id;
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
    if (depthTestEnabled == enabled) return;

    if (enabled)
    {
        glEnable(GL_DEPTH_TEST);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }

    depthTestEnabled = enabled;
}

void OpenGLRenderBackend::SetDepthCompare(DepthCompare compare)
{
    if (depthCompare == compare) return;

    glDepthFunc(compare == DepthCompare::LessEqual ? GL_LEQUAL : GL_LESS);
    depthCompare = compare;
}

void OpenGLRenderBackend::SetPolygonOffset(bool enabled, float32 factor, float32 units)
{
    if (polygonOffsetEnabled != enabled)
    {
        if (enabled) glEnable(GL_POLYGON_OFFSET_FILL);
        else glDisable(GL_POLYGON_OFFSET_FILL);
        polygonOffsetEnabled = enabled;
    }

    //偏移量每帧都可能不同，启用时每次都提交
    if (enabled) glPolygonOffset(factor, units);
}

void OpenGLRenderBackend::SetDepthWrite(bool enabled)
{
    if (depthWriteEnabled == enabled) return;

    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    depthWriteEnabled = enabled;
}

void OpenGLRenderBackend::SetBlend(bool enabled)
{
    if (blendEnabled == enabled) return;

    if (enabled)
    {
        glEnable(GL_BLEND);
    }
    else
    {
        glDisable(GL_BLEND);
    }

    blendEnabled = enabled;
}

//设置混合方程，RGB 与 Alpha 因子分开指定
void OpenGLRenderBackend::SetBlendMode(BlendMode mode)
{
    if (blendMode == mode) return;

    if (mode == BlendMode::Additive)
    {
        //加法混合的 Alpha 不累加，目标覆盖率保持不变，否则叠加后会溢出成不透明。
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
    }
    else
    {
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }

    blendMode = mode;
}

//设置正面、背面或关闭三角形剔除
void OpenGLRenderBackend::SetCullMode(CullMode mode)
{
    if (mode == CullMode::Auto) mode = CullMode::None;
    if (cullMode == mode) return;

    if (mode == CullMode::None)
    {
        glDisable(GL_CULL_FACE);
    }
    else
    {
        glEnable(GL_CULL_FACE);
        glCullFace(mode == CullMode::Front ? GL_FRONT : GL_BACK);
    }

    cullMode = mode;
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

    const void* offset = reinterpret_cast<const void*>(static_cast<uintptr>(indexStart) * sizeof(uint32));
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, offset);
}

//实例布局的每条实例记录字节数
uint32 OpenGLRenderBackend::GetInstanceRecordSize(GpuVertexLayout layout)
{
    if (layout == GpuVertexLayout::InstancedMesh) return static_cast<uint32>(sizeof(GpuMeshInstance));
    if (layout == GpuVertexLayout::InstancedTrail) return static_cast<uint32>(sizeof(GpuTrailInstance));
    return 0;
}

//实例化绘制的范围校验
bool OpenGLRenderBackend::ValidateInstancedRange(uint32 indexStart, uint32 indexCount, uint32 instanceCount)
{
    if (!currentVertexInput.IsValid())
    {
        Log::Error("OpenGL instanced draw skipped: no vertex input is bound.");
        return false;
    }
    if (!currentShaderProgram.IsValid())
    {
        Log::Error("OpenGL instanced draw skipped: no shader program is bound.");
        return false;
    }
    //零数量是空操作，不属于错误。
    if (indexCount == 0 || instanceCount == 0) return false;

    auto countIt = vertexInputIndexCounts.find(currentVertexInput.id);
    if (countIt != vertexInputIndexCounts.end())
    {
        usize start = static_cast<usize>(indexStart);
        usize count = static_cast<usize>(indexCount);
        usize available = static_cast<usize>(countIt->second);
        if (start > available || count > available - start)
        {
            Log::Error("OpenGL instanced draw skipped: index range exceeds index buffer.");
            return false;
        }
    }

    //实例属性按整条记录读取，缺最后一条也要拒绝，不能读越界存储。
    GpuVertexLayout layout = GpuVertexLayout::Mesh;
    auto layoutIt = vertexInputLayouts.find(currentVertexInput.id);
    if (layoutIt != vertexInputLayouts.end()) layout = layoutIt->second;

    uint32 recordSize = GetInstanceRecordSize(layout);
    if (recordSize != 0)
    {
        if (!boundInstanceBuffer.IsValid())
        {
            Log::Error("OpenGL instanced draw skipped: instance buffer is not bound.");
            return false;
        }

        auto capacityIt = vertexBufferCapacities.find(boundInstanceBuffer.id);
        if (capacityIt == vertexBufferCapacities.end())
        {
            Log::Error("OpenGL instanced draw skipped: instance buffer capacity is unknown.");
            return false;
        }

        usize required = boundInstanceOffset + static_cast<usize>(instanceCount) * static_cast<usize>(recordSize);
        if (required > capacityIt->second)
        {
            Log::Error("OpenGL instanced draw skipped: instance range exceeds instance buffer.");
            return false;
        }
    }

    if (indexStart > MaxGlSizei || indexCount > MaxGlSizei || instanceCount > MaxGlSizei)
    {
        Log::Error("OpenGL instanced draw skipped: count exceeds GLsizei range.");
        return false;
    }

    return true;
}

//实例化索引绘制
void OpenGLRenderBackend::DrawIndexedInstanced(uint32 indexStart, uint32 indexCount, uint32 instanceCount)
{
    if (!ValidateInstancedRange(indexStart, indexCount, instanceCount)) return;

    const void* offset = reinterpret_cast<const void*>(static_cast<uintptr>(indexStart) * sizeof(uint32));
    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, offset,
        static_cast<GLsizei>(instanceCount));
}

//实例化能力查询
bool OpenGLRenderBackend::SupportsInstancing() const
{
    return instancingSupported;
}

//查询片元纹理槽上限
uint32 OpenGLRenderBackend::GetFragmentTextureUnitCount() const
{
    return fragmentTextureUnitCount;
}

//流式更新顶点缓冲
bool OpenGLRenderBackend::UploadVertexBuffer(GpuVertexBufferID id, const void* data, usize size, usize capacity)
{
    if (!id.IsValid())
    {
        Log::Error("OpenGL vertex buffer upload skipped: buffer id is invalid.");
        return false;
    }

    return WriteVertexBuffer(id.id, data, size, capacity);
}

//流式更新索引缓冲
bool OpenGLRenderBackend::UploadIndexBuffer(GpuIndexBufferID id, const uint32* data, uint32 count, uint32 capacity)
{
    if (!id.IsValid())
    {
        Log::Error("OpenGL index buffer upload skipped: buffer id is invalid.");
        return false;
    }

    return WriteIndexBuffer(id.id, data, count, capacity);
}

//按容量写入顶点缓冲数据
bool OpenGLRenderBackend::WriteVertexBuffer(uint32 buffer, const void* data, usize size, usize capacity)
{
    if (size > capacity || (size != 0 && !data))
    {
        Log::Error("OpenGL vertex buffer upload rejected: size exceeds capacity or the data pointer is null.");
        return false;
    }

    auto capacityIt = vertexBufferCapacities.find(buffer);
    if (capacityIt == vertexBufferCapacities.end())
    {
        Log::Error("OpenGL vertex buffer upload rejected: the buffer is not managed by this backend.");
        return false;
    }

    GLint previousBuffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    //orphan 之后整块重新分配，驱动不必等待上一批绘制读完旧存储。
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity), nullptr, GL_STREAM_DRAW);
    if (size != 0) glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(size), data);
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));

    capacityIt->second = capacity;
    return true;
}

//按容量写入索引缓冲数据，并刷新引用它的顶点输入缓存
bool OpenGLRenderBackend::WriteIndexBuffer(uint32 buffer, const uint32* data, uint32 count, uint32 capacity)
{
    if (count > capacity || (count != 0 && !data))
    {
        Log::Error("OpenGL index buffer upload rejected: count exceeds capacity or the data pointer is null.");
        return false;
    }

    auto capacityIt = indexBufferCapacities.find(buffer);
    if (capacityIt == indexBufferCapacities.end())
    {
        Log::Error("OpenGL index buffer upload rejected: the buffer is not managed by this backend.");
        return false;
    }

    //索引缓冲与顶点缓冲共用缓冲对象。这里绑到 GL_ARRAY_BUFFER 覆盖内容，
    //避免改动当前 VAO 记录的 GL_ELEMENT_ARRAY_BUFFER。
    usize byteCapacity = static_cast<usize>(capacity) * sizeof(uint32);
    GLint previousBuffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(byteCapacity), nullptr, GL_STREAM_DRAW);
    if (count != 0) glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(static_cast<usize>(count) * sizeof(uint32)), data);
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));

    capacityIt->second = byteCapacity;
    indexBufferCounts[buffer] = count;
    //引用同一索引缓冲的顶点输入必须同步索引数量，否则绘制范围校验会用旧值。
    for (const auto& pair : vertexInputIndexBuffers)
    {
        if (pair.second == buffer) vertexInputIndexCounts[pair.first] = count;
    }
    return true;
}

//按当前顶点输入的实例布局绑定实例缓冲
bool OpenGLRenderBackend::BindInstanceBuffer(GpuVertexBufferID id, usize byteOffset)
{
    if (!currentVertexInput.IsValid() || !id.IsValid())
    {
        Log::Error("OpenGL instance buffer bind skipped: vertex input or buffer id is invalid.");
        return false;
    }

    GpuVertexLayout layout = GpuVertexLayout::Mesh;
    auto layoutIt = vertexInputLayouts.find(currentVertexInput.id);
    if (layoutIt != vertexInputLayouts.end()) layout = layoutIt->second;

    uint32 recordSize = GetInstanceRecordSize(layout);
    if (recordSize == 0)
    {
        Log::Error("OpenGL instance buffer bind skipped: the bound vertex input is not an instanced layout.");
        return false;
    }
    if (byteOffset % sizeof(float32) != 0)
    {
        Log::Error("OpenGL instance buffer bind skipped: offset is not 4-byte aligned.");
        return false;
    }

    auto capacityIt = vertexBufferCapacities.find(id.id);
    if (capacityIt == vertexBufferCapacities.end() || byteOffset > capacityIt->second)
    {
        Log::Error("OpenGL instance buffer bind skipped: the buffer is unknown or the offset exceeds its capacity.");
        return false;
    }

    GLint previousBuffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, id.id);

    if (layout == GpuVertexLayout::InstancedMesh)
    {
        //模型四列，零平移偏移在 OrbedenGetModel 里由填充列补足。
        for (uint32 column = 0; column < 4; ++column)
        {
            uint32 location = 5 + column;
            glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, recordSize,
                reinterpret_cast<void*>(static_cast<uintptr>(byteOffset + column * 16)));
            glEnableVertexAttribArray(location);
            glVertexAttribDivisor(location, 1);
        }
        //法线三列，每列补齐到 vec4。
        for (uint32 column = 0; column < 3; ++column)
        {
            uint32 location = 9 + column;
            glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, recordSize,
                reinterpret_cast<void*>(static_cast<uintptr>(byteOffset + 64 + column * 16)));
            glEnableVertexAttribArray(location);
            glVertexAttribDivisor(location, 1);
        }
        glVertexAttribPointer(12, 4, GL_FLOAT, GL_FALSE, recordSize,
            reinterpret_cast<void*>(static_cast<uintptr>(byteOffset + 112)));
        glEnableVertexAttribArray(12);
        glVertexAttribDivisor(12, 1);
        glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, recordSize,
            reinterpret_cast<void*>(static_cast<uintptr>(byteOffset + 128)));
        glEnableVertexAttribArray(13);
        glVertexAttribDivisor(13, 1);
    }
    else
    {
        //拖尾：四个角点、两端颜色、uv 区间。
        for (uint32 corner = 0; corner < 4; ++corner)
        {
            uint32 location = 5 + corner;
            glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, recordSize,
                reinterpret_cast<void*>(static_cast<uintptr>(byteOffset + corner * 16)));
            glEnableVertexAttribArray(location);
            glVertexAttribDivisor(location, 1);
        }
        glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, recordSize,
            reinterpret_cast<void*>(static_cast<uintptr>(byteOffset + 64)));
        glEnableVertexAttribArray(9);
        glVertexAttribDivisor(9, 1);
        glVertexAttribPointer(10, 4, GL_FLOAT, GL_FALSE, recordSize,
            reinterpret_cast<void*>(static_cast<uintptr>(byteOffset + 80)));
        glEnableVertexAttribArray(10);
        glVertexAttribDivisor(10, 1);
        glVertexAttribPointer(11, 4, GL_FLOAT, GL_FALSE, recordSize,
            reinterpret_cast<void*>(static_cast<uintptr>(byteOffset + 96)));
        glEnableVertexAttribArray(11);
        glVertexAttribDivisor(11, 1);
    }

    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
    boundInstanceBuffer = id;
    boundInstanceOffset = byteOffset;
    return true;
}

void OpenGLRenderBackend::ActivateTextureSlot(uint32 slot)
{
    if (currentTextureSlot == slot) return;

    glActiveTexture(GL_TEXTURE0 + slot);
    currentTextureSlot = slot;
}

void OpenGLRenderBackend::BindTexture2D(uint32 slot, uint32 texture)
{
    auto it = boundTexture2Ds.find(slot);
    if (it != boundTexture2Ds.end() && it->second == texture) return;

    ActivateTextureSlot(slot);
    glBindTexture(GL_TEXTURE_2D, texture);
    boundTexture2Ds[slot] = texture;
}

void OpenGLRenderBackend::InvalidateTexture2D(uint32 texture)
{
    for (auto& pair : boundTexture2Ds)
    {
        if (pair.second == texture) pair.second = 0;
    }
}

int32 OpenGLRenderBackend::GetUniformLocation(const char* name)
{
    if (!currentShaderProgram.IsValid() || !name) return -1;

    auto& locations = uniformLocations[currentShaderProgram.id];
    auto it = locations.find(name);
    if (it != locations.end()) return it->second;

    int32 location = glGetUniformLocation(currentShaderProgram.id, name);
    locations.emplace(name, location);
    return location;
}

/// <summary>绘制不受光照影响的调试线，使用相机矩阵进行三维裁剪。</summary>
void OpenGLRenderBackend::DrawLines(const List<DebugLine>& lines, const matrix4x4& viewProjection, uint32 layerMask)
{
    if (lines.empty()) return;
    if (!debugLineShader.IsValid())
    {
        GpuShaderProgramDesc desc;
        desc.vertexSource = "#version 430 core\nlayout(location=0) in vec3 a_Position; uniform mat4 u_ViewProjection; void main(){gl_Position=u_ViewProjection*vec4(a_Position,1.0);}";
        desc.fragmentSource = "#version 430 core\nuniform vec4 u_Color; out vec4 FragColor; void main(){FragColor=u_Color;}";
        debugLineShader = CreateShaderProgram(desc);
        if (!debugLineShader.IsValid()) return;
        glGenVertexArrays(1, &debugLineVertexArray);
        glGenBuffers(1, &debugLineVertexBuffer);
        glBindVertexArray(debugLineVertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, debugLineVertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vector3) * 2, nullptr, GL_STREAM_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vector3), nullptr);
        glEnableVertexAttribArray(0);
    }

    BindShaderProgram(debugLineShader);
    SetUniformMatrix4("u_ViewProjection", viewProjection);
    SetDepthWrite(false);
    SetBlend(true);
    SetCullMode(CullMode::None);
    glBindVertexArray(debugLineVertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, debugLineVertexBuffer);
    for (const DebugLine& line : lines)
    {
        if ((line.drawLayer & layerMask) == 0) continue;
        const vector3 points[2] = { line.start, line.end };
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(points), points);
        SetDepthTest(line.depthTest);
        SetUniformColor("u_Color", line.tint);
        glDrawArrays(GL_LINES, 0, 2);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(currentVertexInput.id);
}


//创建异步深度统计资源
GpuDepthDistributionID OpenGLRenderBackend::CreateDepthDistribution()
{
    //编译组内归约直方图程序
    if (!depthDistributionShader.IsValid())
    {
        const char* source = R"(#version 430 core
layout(local_size_x=16, local_size_y=16) in;
layout(std430, binding=0) buffer Distribution { uint bins[64]; };
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
        uint32 shader = CompileShader(GL_COMPUTE_SHADER, source);
        if (!shader) return {};
        uint32 program = glCreateProgram();
        glAttachShader(program, shader);
        glLinkProgram(program);
        glDeleteShader(shader);
        GLint success = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success)
        {
            Log::Error(("Depth distribution link failed: " + GetProgramLog(program)).c_str());
            glDeleteProgram(program);
            return {};
        }
        depthDistributionShader = { program };
    }
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GpuDepthDistribution::bins), nullptr, GL_DYNAMIC_READ);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    if (!buffer) return {};
    depthDistributions.emplace(buffer, DepthDistributionState{});
    return { buffer };
}

//释放统计缓冲与完成信号
void OpenGLRenderBackend::DeleteDepthDistribution(GpuDepthDistributionID id)
{
    auto found = depthDistributions.find(id.id);
    if (found == depthDistributions.end()) return;
    if (found->second.fence) glDeleteSync(static_cast<GLsync>(found->second.fence));
    GLuint buffer = id.id;
    glDeleteBuffers(1, &buffer);
    depthDistributions.erase(found);
}

//提交冻结相机深度分布
bool OpenGLRenderBackend::SubmitDepthDistribution(GpuDepthDistributionID id, GpuDepthTextureID depth,
    const matrix4x4& inverseProjection, float32 nearPlane, float32 farPlane)
{
    auto found = depthDistributions.find(id.id);
    if (found == depthDistributions.end() || found->second.fence || !depth.IsValid() ||
        !(nearPlane > 0.0f && farPlane > nearPlane)) return false;

    //保存绘制状态并清空统计
    GpuShaderProgramID previousProgram = currentShaderProgram;
    uint32 previousSlot = currentTextureSlot;
    uint32 previousTexture = boundTexture2Ds[0];
    BindDepthTexture(0, depth);
    GLint width = 0, height = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    if (width <= 0 || height <= 0)
    {
        BindTexture2D(0, previousTexture);
        ActivateTextureSlot(previousSlot);
        return false;
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id.id);
    uint32 zero = 0;
    glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, id.id);
    BindShaderProgram(depthDistributionShader);
    SetUniformInt("u_Depth", 0);
    SetUniformMatrix4("u_InverseProjection", inverseProjection);
    SetUniformFloat("u_Near", nearPlane);
    SetUniformFloat("u_Far", farPlane);
    glDispatchCompute((width + 15) / 16, (height + 15) / 16, 1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    found->second.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    found->second.nearPlane = nearPlane;
    found->second.farPlane = farPlane;
    glFlush();

    //恢复前向绘制状态
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    BindShaderProgram(previousProgram);
    BindTexture2D(0, previousTexture);
    ActivateTextureSlot(previousSlot);
    return found->second.fence != nullptr;
}

//零超时读取已完成的统计
bool OpenGLRenderBackend::TryReadDepthDistribution(GpuDepthDistributionID id, GpuDepthDistribution& distribution)
{
    auto found = depthDistributions.find(id.id);
    if (found == depthDistributions.end() || !found->second.fence) return false;
    GLsync fence = static_cast<GLsync>(found->second.fence);
    GLenum status = glClientWaitSync(fence, 0, 0);
    if (status == GL_TIMEOUT_EXPIRED) return false;
    glDeleteSync(fence);
    found->second.fence = nullptr;
    if (status == GL_WAIT_FAILED)
    {
        Log::Error("Depth distribution fence polling failed.");
        return false;
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id.id);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(distribution.bins), distribution.bins);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    distribution.nearPlane = found->second.nearPlane;
    distribution.farPlane = found->second.farPlane;
    return true;
}
