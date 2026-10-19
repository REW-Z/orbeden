#include "Rendering/Backend/OpenGLRenderBackend.h"

#include "Log/Log.h"
#include "Profiler/Profiler.h"
#include "Rendering/DepthDistributionShader.h"
#include "Rendering/VertexLayout.h"
#include "Platform/GlfwWindow.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <cstring>
#include <stdexcept>
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

    uint32 CreateOpenGLBuffer(const GpuBufferDesc& desc)
    {
        if (desc.size == 0) return 0;
        //静态缓冲必须一次给足数据；流式缓冲允许只按容量分配，之后由流式上传填充。
        if (!desc.data && desc.usage != GpuBufferUsage::Stream) return 0;

        GLuint id = 0;
        glGenBuffers(1, &id);
        GLint previous = 0;
        glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &previous);
        glBindBuffer(GL_COPY_WRITE_BUFFER, id);
        glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(desc.size), desc.data,
            desc.usage == GpuBufferUsage::Stream ? GL_STREAM_DRAW : GL_STATIC_DRAW);
        glBindBuffer(GL_COPY_WRITE_BUFFER, static_cast<uint32>(previous));
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
        if (format == GpuRenderTargetFormat::RGBA16F) return GL_RGBA16F;
        if (format == GpuRenderTargetFormat::R8) return GL_R8;
        return GL_RGBA8;
    }

    GLenum ToRenderTargetSourceType(GpuRenderTargetFormat format)
    {
        return format == GpuRenderTargetFormat::RGBA16F ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
    }

    //渲染目标创建只给格式、不给像素，外部格式必须与内部格式的通道数一致。
    GLenum ToRenderTargetSourceFormat(GpuRenderTargetFormat format)
    {
        return format == GpuRenderTargetFormat::R8 ? GL_RED : GL_RGBA;
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
    this->window = window;
    surfaces[0] = window;

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
    rasterStateValid = false;
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
    programUniforms.clear();
    currentUniforms = nullptr;

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

    //最大纹理边长同样查一次并登记到后端基类：动态纹理与字体图集要按它决定分页上限。
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    SetMaxTextureSize(static_cast<int32>(std::max(maxTextureSize, 0)));

    LogOpenGLError("OpenGLRenderBackend::Initialize");
    return true;
}

void OpenGLRenderBackend::Shutdown()
{
    if (!window) return;
    SetSurface({});
    for (auto& [surface, arrays] : vertexArrays)
    {
        SetSurface({ surface });
        for (auto& [input, array] : arrays) glDeleteVertexArrays(1, &array);
    }
    vertexArrays.clear(); vertexInputDescriptions.clear();
    SetSurface({});
    while (!depthDistributions.empty()) DeleteDepthDistribution({ depthDistributions.begin()->first });
    DeleteShaderProgram(depthDistributionShader);
    depthDistributionShader = {};
    for (auto& pair : renderTargetColorAttachments)
    {
        GLuint framebuffer = pair.first;
        glDeleteFramebuffers(1, &framebuffer);
        if (pair.second != 0)
        {
            GLuint texture = pair.second;
            glDeleteTextures(1, &texture);
            ownedTextures.erase(texture);
            textureInfo.erase(texture);
        }
    }

    for (uint32 texture : ownedTextures) glDeleteTextures(1, &texture);
    ownedTextures.clear(); textureInfo.clear();
    for (const auto& [id, capacity] : vertexBufferCapacities) glDeleteBuffers(1, &id);
    for (const auto& [id, capacity] : indexBufferCapacities) glDeleteBuffers(1, &id);
    while (!programUniforms.empty()) DeleteShaderProgram({ programUniforms.begin()->first });
    renderTargetFormats.clear();
    renderTargetColorAttachments.clear();
    indexBufferCounts.clear();
    vertexBufferCapacities.clear();
    indexBufferCapacities.clear();
    vertexInputIndexBuffers.clear();
    vertexInputIndexCounts.clear();
    vertexInputLayouts.clear();
    boundTexture2Ds.clear();
    boundCubeTextures.clear();
    programUniforms.clear();
    currentUniforms = nullptr;
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
    surfaces.clear(); pendingSurfaces.clear();
    window = nullptr;
}

void OpenGLRenderBackend::BeginFrame()
{
    GetPipelineState().blendMode = BlendMode::Alpha;
}

void OpenGLRenderBackend::EndFrame()
{
}

//等待已提交 GPU 工作完成
void OpenGLRenderBackend::WaitForIdle()
{
    glFinish();
}

//提交主窗口显示
void OpenGLRenderBackend::Present()
{
    for (uint32 id : pendingSurfaces)
    {
        auto platform = dynamic_cast<GlfwWindow*>(surfaces.at(id));
        glfwMakeContextCurrent(platform->GetGlfwWindow());
        glfwSwapBuffers(platform->GetGlfwWindow());
    }
    pendingSurfaces.clear();
    SetSurface({});
    pendingSurfaces.clear();
}

//设置像素裁剪区域
void OpenGLRenderBackend::SetScissor(bool enabled, int32 x, int32 y, int32 width, int32 height)
{
    if (enabled) { glEnable(GL_SCISSOR_TEST); glScissor(x, y, width, height); }
    else glDisable(GL_SCISSOR_TEST);
}

void OpenGLRenderBackend::BeginPass(const RenderPassDesc& desc)
{
    PROFILE("Render/BeginPass");
    pendingSurfaces.insert(currentSurface);
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
    uint32 id = CreateOpenGLBuffer(desc);
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
    uint32 id = CreateOpenGLBuffer(desc);
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

//创建共享顶点输入描述
GpuVertexInputID OpenGLRenderBackend::CreateVertexInput(const GpuVertexInputDesc& desc)
{
    if (!desc.vertexBuffer.IsValid() || !desc.indexBuffer.IsValid() || !desc.stride) return {};
    auto count = indexBufferCounts.find(desc.indexBuffer.id);
    if (count == indexBufferCounts.end()) return {};
    uint32 id = nextVertexInput++;
    vertexInputDescriptions[id] = desc;
    vertexInputIndexBuffers[id] = desc.indexBuffer.id;
    vertexInputIndexCounts[id] = count->second;
    vertexInputLayouts[id] = desc.layout;
    return { id };
}

//创建当前窗口的原生顶点输入
uint32 OpenGLRenderBackend::CreateVertexArray(const GpuVertexInputDesc& desc)
{
    uint32 id;
    glGenVertexArrays(1, &id);
    glBindVertexArray(id);
    glBindBuffer(GL_ARRAY_BUFFER, desc.vertexBuffer.id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, desc.indexBuffer.id);
    for (const VertexAttribute& attribute : GetVertexLayout(desc.layout).attributes)
    {
        if (attribute.binding != 0) continue;
        bool packed = attribute.format == VertexAttributeFormat::UNorm8x4;
        int32 components = attribute.format == VertexAttributeFormat::Float2 ? 2 : attribute.format == VertexAttributeFormat::Float3 ? 3 : 4;
        glVertexAttribPointer(attribute.location, components, packed ? GL_UNSIGNED_BYTE : GL_FLOAT, packed ? GL_TRUE : GL_FALSE,
            desc.stride, reinterpret_cast<void*>(static_cast<uintptr>(attribute.offset)));
        glEnableVertexAttribArray(attribute.location);
    }
    return id;
}

//获取当前窗口的原生顶点输入
uint32 OpenGLRenderBackend::GetVertexArray(GpuVertexInputID id)
{
    if (!id.IsValid()) return 0;
    auto& arrays = vertexArrays[currentSurface];
    auto found = arrays.find(id.id);
    if (found != arrays.end()) return found->second;
    uint32 array = CreateVertexArray(vertexInputDescriptions.at(id.id));
    arrays.emplace(id.id, array);
    return array;
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
    uint32 previousSurface = currentSurface;
    for (auto& [surface, arrays] : vertexArrays)
    {
        auto found = arrays.find(id.id);
        if (found == arrays.end()) continue;
        SetSurface({ surface });
        glDeleteVertexArrays(1, &found->second);
        arrays.erase(found);
    }
    vertexInputDescriptions.erase(id.id);
    SetSurface({ previousSurface });
}

GpuTextureID OpenGLRenderBackend::CreateTexture(const GpuTextureDesc& desc)
{
    if (desc.width <= 0 || desc.height <= 0 || !desc.pixels || (desc.channels != 1 && desc.channels != 3 && desc.channels != 4)) return GpuTextureID();

    GLenum format = ToTextureFormat(desc.channels);
    //像素字节保持不变，sRGB 内部格式让采样阶段由硬件解码到线性。
    GLenum internalFormat = desc.srgb ? ToSrgbTextureFormat(desc.channels) : format;
    GLuint id = 0;
    glGenTextures(1, &id);
    ownedTextures.insert(id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, desc.generateMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLint wrap = desc.clampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    GLint unpackAlignment;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, desc.width, desc.height, 0, format, GL_UNSIGNED_BYTE, desc.pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
    if (desc.generateMipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    textureInfo[id] = { desc.width, desc.height, desc.channels, desc.generateMipmaps };
    glBindTexture(GL_TEXTURE_2D, 0);
    boundTexture2Ds[currentTextureSlot] = 0;
    return { id };
}

//局部覆盖纹理区域：解包状态与纹理绑定用完即恢复，不把行距泄漏给后续上传。
bool OpenGLRenderBackend::UploadTextureRegion(GpuTextureID id, int32 x, int32 y, int32 width, int32 height,
    int32 channels, const uint8* pixels, int32 rowStride)
{
    if (!id.IsValid() || !pixels || width <= 0 || height <= 0 || rowStride <= 0) return false;
    if (channels != 1 && channels != 3 && channels != 4) return false;
    if (rowStride < width * channels) return false;

    auto info = textureInfo.find(id.id);
    if (info == textureInfo.end() || info->second.channels != channels || x < 0 || y < 0 || x + width > info->second.width || y + height > info->second.height) return false;
    List<uint8> packed;
    if (rowStride % channels != 0)
    {
        packed.resize(static_cast<usize>(width) * height * channels);
        for (int32 row = 0; row < height; ++row)
            std::memcpy(packed.data() + static_cast<usize>(row) * width * channels, pixels + static_cast<usize>(row) * rowStride, static_cast<usize>(width) * channels);
        pixels = packed.data(); rowStride = width * channels;
    }

    GLint previousRowLength = 0;
    GLint previousAlignment = 0;
    GLint previousSkipPixels = 0;
    GLint previousSkipRows = 0;
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &previousRowLength);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &previousSkipPixels);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &previousSkipRows);

    //行距以像素计，按通道数换算；对齐取 1，避免行首被补齐影响部分上传。
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowStride / channels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);

    uint32 previousTexture = boundTexture2Ds[currentTextureSlot];
    BindTexture2D(currentTextureSlot, id.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, ToTextureFormat(channels), GL_UNSIGNED_BYTE, pixels);
    if (info->second.mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    BindTexture2D(currentTextureSlot, previousTexture);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, previousRowLength);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, previousSkipPixels);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, previousSkipRows);
    return true;
}

//读取一个像素的深度：只动读绑定，绘制绑定与后端缓存都不受影响。
bool OpenGLRenderBackend::ReadDepthPixel(GpuRenderTargetID target, int32 x, int32 y, float32& depth)
{
    depth = 0.0f;

    GLint previousRead = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target.id);
    glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, previousRead);
    return true;
}

void OpenGLRenderBackend::DeleteTexture(GpuTextureID id)
{
    if (!id.IsValid()) return;

    InvalidateTexture2D(id.id);
    GLuint texture = id.id;
    glDeleteTextures(1, &texture);
    ownedTextures.erase(texture);
    textureInfo.erase(texture);
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
    ownedTextures.insert(id);
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
    ownedTextures.erase(texture);
    textureInfo.erase(texture);
}

GpuCubeTextureID OpenGLRenderBackend::CreateCubeTexture(const GpuCubeTextureDesc& desc)
{
    if (desc.width <= 0 || desc.height != desc.width || desc.channels < 1 || desc.channels > 4) return GpuCubeTextureID();
    for (const uint8* face : desc.faces) if (!face) return GpuCubeTextureID();

    GLenum format = ToTextureFormat(desc.channels);
    GLenum internalFormat = desc.srgb ? ToSrgbTextureFormat(desc.channels) : format;
    GLuint id = 0;
    glGenTextures(1, &id);
    ownedTextures.insert(id);
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
    ownedTextures.erase(texture);
    textureInfo.erase(texture);
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
        ownedTextures.insert(colorTexture);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        GLint colorFilter = desc.linearColorFilter ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, colorFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, colorFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, ToRenderTargetFormat(desc.format), desc.width, desc.height, 0,
            ToRenderTargetSourceFormat(desc.format), ToRenderTargetSourceType(desc.format), nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        boundTexture2Ds[currentTextureSlot] = 0;
    }

    GLint previousRead = 0, previousDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
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
    glBindFramebuffer(GL_READ_FRAMEBUFFER, previousRead);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousDraw);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Log::Error("OpenGL framebuffer creation failed.");
        glDeleteFramebuffers(1, &id);
        if (colorTexture != 0) { glDeleteTextures(1, &colorTexture); ownedTextures.erase(colorTexture); }
        return GpuRenderTargetID();
    }

    renderTargetColorAttachments[id] = colorTexture;
    renderTargetFormats[id] = desc.format;
    return { id };
}

/// <summary>绑定渲染目标并更新深度附件。</summary>
void OpenGLRenderBackend::AttachRenderTargetDepthTexture(GpuRenderTargetID target, GpuDepthTextureID depthTexture)
{
    if (!target.IsValid()) return;
    GLint previous = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.id);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture.id, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previous);
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
            ownedTextures.erase(colorTexture);
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

    GLint previousRead = 0, previousDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
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

    //恢复调用方的附件绑定
    glBindFramebuffer(GL_READ_FRAMEBUFFER, previousRead);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousDraw);

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

    programUniforms.try_emplace(program);
    return { program };
}

void OpenGLRenderBackend::DeleteShaderProgram(GpuShaderProgramID id)
{
    if (!id.IsValid()) return;

    if (currentShaderProgram.id == id.id)
    {
        glUseProgram(0);
        currentShaderProgram = GpuShaderProgramID();
        currentUniforms = nullptr;
    }

    glDeleteProgram(id.id);
    programUniforms.erase(id.id);
    ReleaseShaderParameters(id);
}

void OpenGLRenderBackend::OnBindShaderProgram(GpuShaderProgramID id)
{
    if (currentShaderProgram.id == id.id) return;

    currentShaderProgram = id;
    currentUniforms = id.IsValid() ? &programUniforms[id.id] : nullptr;
    glUseProgram(id.id);
}

//查询指定程序实际使用的 uniform
bool OpenGLRenderBackend::HasShaderParameter(GpuShaderProgramID program, const char* name)
{
    const UniformState* state = GetUniformState(program, name);
    return state && state->location >= 0;
}

void OpenGLRenderBackend::BindVertexInput(GpuVertexInputID id)
{
    if (currentVertexInput.id == id.id) return;

    currentVertexInput = id;
    //实例 attribute 属于 VAO 自身状态，换 VAO 后原来的实例缓冲只对旧 VAO 有效。
    boundInstanceBuffer = GpuVertexBufferID();
    boundInstanceOffset = 0;
    glBindVertexArray(GetVertexArray(id));
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
    int32 location = UpdateUniformValue(name, UniformValueType::Matrix4, value.m, sizeof(value.m));
    if (location < 0) return;

    glUniformMatrix4fv(location, 1, GL_FALSE, value.m);
}

void OpenGLRenderBackend::SetUniformVector3(const char* name, const vector3& value)
{
    const float32 components[] = { value.x, value.y, value.z };
    int32 location = UpdateUniformValue(name, UniformValueType::Vector3, components, sizeof(components));
    if (location < 0) return;

    glUniform3f(location, value.x, value.y, value.z);
}

void OpenGLRenderBackend::SetUniformColor(const char* name, const color& value)
{
    const float32 components[] = { value.r, value.g, value.b, value.a };
    int32 location = UpdateUniformValue(name, UniformValueType::Color, components, sizeof(components));
    if (location < 0) return;

    glUniform4f(location, value.r, value.g, value.b, value.a);
}

void OpenGLRenderBackend::SetUniformInt(const char* name, int32 value)
{
    int32 location = UpdateUniformValue(name, UniformValueType::Int, &value, sizeof(value));
    if (location < 0) return;

    glUniform1i(location, value);
}

void OpenGLRenderBackend::SetUniformFloat(const char* name, float32 value)
{
    int32 location = UpdateUniformValue(name, UniformValueType::Float, &value, sizeof(value));
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
    else if (mode == BlendMode::PremultipliedAlpha)
    {
        //源色已经是预乘的，RGB 与 A 都不能再乘一遍源 Alpha。
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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
    ApplyDrawState();
    glDrawElements(GetPipelineState().topology == PrimitiveTopology::Lines ? GL_LINES : GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, offset);
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

    uint32 recordSize = GetVertexLayout(layout).instanceStride;
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
    ApplyDrawState();
    glDrawElementsInstanced(GetPipelineState().topology == PrimitiveTopology::Lines ? GL_LINES : GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, offset,
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

    uint32 recordSize = GetVertexLayout(layout).instanceStride;
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

    for (const VertexAttribute& attribute : GetVertexLayout(layout).attributes)
    {
        if (attribute.binding != 1) continue;
        glVertexAttribPointer(attribute.location, 4, GL_FLOAT, GL_FALSE, recordSize,
            reinterpret_cast<void*>(static_cast<uintptr>(byteOffset + attribute.offset)));
        glEnableVertexAttribArray(attribute.location);
        glVertexAttribDivisor(attribute.location, 1);
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

//获取指定程序的 uniform 缓存
OpenGLRenderBackend::UniformState* OpenGLRenderBackend::GetUniformState(GpuShaderProgramID program, const char* name)
{
    if (!program.IsValid() || !name) return nullptr;

    ProgramUniformState& uniforms = program.id == currentShaderProgram.id && currentUniforms
        ? *currentUniforms : programUniforms[program.id];
    auto found = uniforms.names.find(std::string_view(name));
    if (found != uniforms.names.end()) return found->second;

    int32 location = glGetUniformLocation(program.id, name);
    UniformState* state = nullptr;
    if (location >= 0)
    {
        state = &uniforms.locations[location];
        state->location = location;
    }
    uniforms.names.emplace(name, state);
    return state;
}

//缓存变化的 uniform 值并返回待提交位置
int32 OpenGLRenderBackend::UpdateUniformValue(const char* name, UniformValueType type, const void* data, usize size)
{
    UniformState* state = GetUniformState(currentShaderProgram, name);
    if (!state || state->location < 0) return -1;
    if (state->type == type && std::memcmp(state->value, data, size) == 0) return -1;

    std::memcpy(state->value, data, size);
    state->type = type;
    return state->location;
}

/// <summary>绘制不受光照影响的调试线，使用相机矩阵进行三维裁剪。</summary>
GpuDepthDistributionID OpenGLRenderBackend::CreateDepthDistribution()
{
    //编译组内归约直方图程序
    if (!depthDistributionShader.IsValid())
    {
        std::string text = DepthDistributionSource;
        text.insert(text.find('\n') + 1, "#define ORBEDEN_DISTRIBUTION_BINDING 0\n");
        const char* source = text.c_str();
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
    GetParameters().SetInt("u_Depth", 0);
    GetParameters().SetMatrix4("u_InverseProjection", inverseProjection);
    GetParameters().SetFloat("u_Near", nearPlane);
    GetParameters().SetFloat("u_Far", farPlane);
    ApplyParameters();
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

//提交当前着色器参数
void OpenGLRenderBackend::ApplyParameters()
{
    if (!currentShaderProgram.IsValid()) return;
    ShaderParameters& parameters = GetParameters();
    for (const ShaderParameterValue* entry : parameters.GetChanges())
    {
        const ShaderParameterValue& value = *entry;
        switch (value.type)
        {
        case ShaderParameterType::Matrix4: SetUniformMatrix4(value.name, value.Read<matrix4x4>()); break;
        case ShaderParameterType::Vector3: SetUniformVector3(value.name, value.Read<vector3>()); break;
        case ShaderParameterType::Color: SetUniformColor(value.name, value.Read<color>()); break;
        case ShaderParameterType::Int: SetUniformInt(value.name, value.Read<int32>()); break;
        case ShaderParameterType::Float: SetUniformFloat(value.name, value.Read<float32>()); break;
        }
    }
    parameters.ClearChanges();
}

//提交参数块与绘制状态
void OpenGLRenderBackend::ApplyDrawState()
{
    const PipelineState& state = GetPipelineState();
    SetDepthTest(state.depthTest);
    SetDepthWrite(state.depthWrite);
    SetDepthCompare(state.depthCompare);
    SetBlend(state.blend);
    SetBlendMode(state.blendMode);
    SetCullMode(state.cull);
    bool modeChanged = !rasterStateValid || polygonMode != state.polygonMode;
    if (modeChanged)
    {
        glPolygonMode(GL_FRONT_AND_BACK, state.polygonMode == PolygonMode::Line ? GL_LINE : GL_FILL);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_POLYGON_OFFSET_LINE);
    }
    if (modeChanged || depthBiasEnabled != state.depthBias)
    {
        GLenum offset = state.polygonMode == PolygonMode::Line ? GL_POLYGON_OFFSET_LINE : GL_POLYGON_OFFSET_FILL;
        if (state.depthBias) glEnable(offset); else glDisable(offset);
    }
    if (!rasterStateValid || depthBiasSlope != state.depthBiasSlope || depthBiasConstant != state.depthBiasConstant)
        glPolygonOffset(state.depthBiasSlope, state.depthBiasConstant);
    polygonMode = state.polygonMode;
    depthBiasEnabled = state.depthBias;
    depthBiasSlope = state.depthBiasSlope;
    depthBiasConstant = state.depthBiasConstant;
    rasterStateValid = true;
    ApplyParameters();
}

//创建附加窗口呈现表面
GpuSurfaceID OpenGLRenderBackend::CreateSurface(IWindow* target)
{
    if (!dynamic_cast<GlfwWindow*>(target) || target->GetGraphicsApi() != WindowGraphicsApi::OpenGL) return {};
    uint32 id = nextSurface++;
    surfaces.emplace(id, target);
    return { id };
}

//释放附加窗口的顶点输入
void OpenGLRenderBackend::DeleteSurface(GpuSurfaceID id)
{
    if (!id.IsValid() || !surfaces.contains(id.id)) return;
    SetSurface(id);
    for (auto& [input, array] : vertexArrays[id.id]) glDeleteVertexArrays(1, &array);
    vertexArrays.erase(id.id);
    SetSurface({});
    pendingSurfaces.erase(id.id);
    surfaces.erase(id.id);
}

//切换窗口上下文与绑定缓存
void OpenGLRenderBackend::SetSurface(GpuSurfaceID id)
{
    if (!surfaces.contains(id.id)) throw std::invalid_argument("OpenGL surface handle is invalid.");
    auto target = dynamic_cast<GlfwWindow*>(surfaces.at(id.id));
    if (currentSurface == id.id && glfwGetCurrentContext() == target->GetGlfwWindow()) return;
    glfwMakeContextCurrent(target->GetGlfwWindow());
    glfwSwapInterval(target->GetVsync() ? 1 : 0);
    currentSurface = id.id;
    rasterStateValid = false;
    currentShaderProgram = {}; currentVertexInput = {}; currentUniforms = nullptr;
    boundInstanceBuffer = {}; boundInstanceOffset = 0;
    boundTexture2Ds.clear(); boundCubeTextures.clear(); currentTextureSlot = 0;
    glUseProgram(0); glBindVertexArray(0); glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST); depthTestEnabled = true;
    glDepthMask(GL_TRUE); depthWriteEnabled = true;
    glDepthFunc(GL_LESS); depthCompare = DepthCompare::Less;
    glDisable(GL_BLEND); blendEnabled = false;
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); blendMode = BlendMode::Alpha;
    glDisable(GL_CULL_FACE); cullMode = CullMode::None;
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}
