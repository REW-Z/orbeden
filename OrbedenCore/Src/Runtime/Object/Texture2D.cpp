#include "Runtime/Object/Texture2D.h"

#include <cstring>
#include <limits>

#include "Log/Log.h"
#include "Rendering/Backend/RenderBackend.h"

OBJECT_TYPE_IMPLEMENT(Texture2D, Object)

namespace
{
    //一像素的字节数；不支持的通道数返回 0。
    int32 BytesPerPixel(int32 channels)
    {
        return channels == 1 || channels == 3 || channels == 4 ? channels : 0;
    }

    //按行 stride 复制一块矩形；源与目标每行只考贝宽度对应的字节数。
    void CopyRegionRows(uint8* target, const uint8* source,
        int32 targetStride, int32 sourceStride, int32 regionBytes, int32 rows)
    {
        for (int32 row = 0; row < rows; ++row)
        {
            std::memcpy(target + static_cast<usize>(row) * targetStride,
                source + static_cast<usize>(row) * sourceStride,
                static_cast<usize>(regionBytes));
        }
    }
}

//判断 GPU 纹理是否需要重新上传
bool Texture2D::IsDirty() const
{
    return gpuDirty;
}

//读取内容版本
uint64 Texture2D::GetRevision() const
{
    return revision;
}

//标记 GPU 纹理需要重新上传
void Texture2D::MarkDirty()
{
    gpuDirty = true;
    fullUpload = true;
    ++revision;
    if (revision == 0) revision = 1;
}

//清除 GPU 刷新标记
void Texture2D::ClearDirty()
{
    gpuDirty = false;
    fullUpload = false;
    dirtyX = dirtyY = dirtyWidth = dirtyHeight = 0;
}

//记录一块需要重新上传的区域
void Texture2D::MarkRegionDirty(int32 x, int32 y, int32 regionWidth, int32 regionHeight)
{
    gpuDirty = true;
    //局部写入同样改变内容版本，CPU 采样缓存按它整块失效。
    ++revision;
    if (revision == 0) revision = 1;
    if (fullUpload) return;

    if (dirtyWidth <= 0 || dirtyHeight <= 0)
    {
        dirtyX = x;
        dirtyY = y;
        dirtyWidth = regionWidth;
        dirtyHeight = regionHeight;
        return;
    }

    int32 left = dirtyX < x ? dirtyX : x;
    int32 bottom = dirtyY < y ? dirtyY : y;
    int32 right = (dirtyX + dirtyWidth) > (x + regionWidth) ? (dirtyX + dirtyWidth) : (x + regionWidth);
    int32 top = (dirtyY + dirtyHeight) > (y + regionHeight) ? (dirtyY + dirtyHeight) : (y + regionHeight);
    dirtyX = left;
    dirtyY = bottom;
    dirtyWidth = right - left;
    dirtyHeight = top - bottom;
}

//创建动态纹理
Texture2D* Texture2D::CreateDynamic(int32 width, int32 height, int32 channels)
{
    if (width <= 0 || height <= 0 || BytesPerPixel(channels) == 0)
    {
        Log::Error("Texture2D::CreateDynamic rejected: invalid size or channel count.");
        return nullptr;
    }

    Texture2D* texture = Object::CreateInstance<Texture2D>();
    if (!texture) return nullptr;

    texture->width = width;
    texture->height = height;
    texture->channels = channels;
    texture->renderTarget = false;
    //动态纹理由 CPU 数据定义，颜色空间按数据贴图处理；需要 sRGB 的调用方自行调整。
    texture->colorSpace = TextureColorSpace::Linear;
    texture->pixels.assign(static_cast<usize>(width) * height * channels, 0);
    texture->MarkDirty();
    return texture;
}

//创建渲染目标纹理
Texture2D* Texture2D::CreateRenderTarget(int32 width, int32 height)
{
    if (width <= 0 || height <= 0)
    {
        Log::Error("Texture2D::CreateRenderTarget rejected: invalid size.");
        return nullptr;
    }

    Texture2D* texture = Object::CreateInstance<Texture2D>();
    if (!texture) return nullptr;

    texture->width = width;
    texture->height = height;
    //渲染目标固定 RGBA；像素由 GPU 产生，不要求 CPU 副本。
    texture->channels = 4;
    texture->renderTarget = true;
    texture->colorSpace = TextureColorSpace::Linear;
    texture->MarkDirty();
    return texture;
}

//局部写入像素
bool Texture2D::UpdateRegion(int32 x, int32 y, int32 regionWidth, int32 regionHeight,
    const uint8* source, int32 byteCount, int32 rowStride)
{
    if (renderTarget || source == nullptr || regionWidth <= 0 || regionHeight <= 0) return false;
    if (x < 0 || y < 0 || x + regionWidth > width || y + regionHeight > height) return false;

    int32 pixelBytes = BytesPerPixel(channels);
    if (pixelBytes == 0) return false;
    int32 regionBytes = regionWidth * pixelBytes;
    //源缓冲必须完整覆盖声明的行数与行距，否则会读到越界内存。
    if (rowStride < regionBytes) return false;
    int64 required = static_cast<int64>(rowStride) * (regionHeight - 1) + regionBytes;
    if (required > byteCount) return false;

    int32 targetStride = width * pixelBytes;
    uint8* target = pixels.data() + static_cast<usize>(y) * targetStride + static_cast<usize>(x) * pixelBytes;
    CopyRegionRows(target, source, targetStride, rowStride, regionBytes, regionHeight);

    //CPU 与 GPU 脏区域同步推进：局部写只上传这一块。
    MarkRegionDirty(x, y, regionWidth, regionHeight);
    return true;
}

//调整渲染目标的尺寸
bool Texture2D::ResizeRenderTarget(int32 newWidth, int32 newHeight)
{
    if (!renderTarget || newWidth <= 0 || newHeight <= 0) return false;
    if (newWidth == width && newHeight == height) return true;

    width = newWidth;
    height = newHeight;
    //尺寸变化后内容未定义，整块重建；MarkDirty 顺带推进内容版本。
    MarkDirty();
    return true;
}

//判断当前是否为渲染目标纹理
bool Texture2D::IsRenderTarget() const
{
    return renderTarget;
}

//读取 Alpha 语义
TextureAlphaMode Texture2D::GetAlphaMode() const
{
    return alphaMode;
}

//读取后端允许的最大纹理边长
int32 Texture2D::GetMaximumSize()
{
    //后端未初始化时返回 0：调用方据此判断当前还不能分配 GPU 纹理。
    return RenderBackend::GetMaxTextureSize();
}
