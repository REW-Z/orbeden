#pragma once

#include "Rendering/Backend/GpuResourceIDs.h"
#include "Runtime/Object/Object.h"

#include <string>

class GpuResourceManager;

//纹理数据的颜色语义。颜色贴图（albedo、自发光、天空盒）的像素是 sRGB 编码，
//采样时需要解码到线性；数据贴图（法线、粗糙度、遮罩、高度）本身就是线性数据，
//解码会破坏数值。这个标记是数据语义，不是可选的渲染开关。
enum class TextureColorSpace : uint32
{
    Linear = 0,
    SRGB = 1,
};

//纹理数据的 Alpha 语义。普通图片是直通 Alpha，画布输出与预乘合成链是预乘 Alpha，
//采样时不能再乘一次。
enum class TextureAlphaMode : uint32
{
    Straight = 0,
    Premultiplied = 1,
};

//CPU纹理数据，不绑定具体渲染API
class Texture2D : public Object
{
    OBJECT_TYPE_DECLARE(Texture2D)

private:
    friend class GpuResourceManager;

    //GPU 纹理句柄及其管理器存储位置。
    GpuTextureID gpuTexture;
    //渲染目标纹理的颜色附件目标；尺寸或内容版本变化时由资源管理器重建，
    //对象身份不变，只有 GPU 侧代次推进。
    GpuRenderTargetID gpuRenderTarget;
    int32 gpuTextureStorageIndex = -1;
    //像素数据变化后置位，由资源管理器重新上传。
    bool gpuDirty = true;
    //渲染目标不要求 CPU 像素，尺寸由 GPU 附件决定。
    bool renderTarget = false;
    //待上传的脏矩形；整块上传时 fullUpload 为真，矩形无意义。
    bool fullUpload = true;
    int32 dirtyX = 0;
    int32 dirtyY = 0;
    int32 dirtyWidth = 0;
    int32 dirtyHeight = 0;

    //记录一块需要重新上传的区域，与已有脏矩形取并集。
    void MarkRegionDirty(int32 x, int32 y, int32 width, int32 height);

public:
    std::string name;
    ORBEDEN_BIND_CHANGED(MarkDirty)
    int32 width = 0;
    ORBEDEN_BIND_CHANGED(MarkDirty)
    int32 height = 0;
    ORBEDEN_BIND_CHANGED(MarkDirty)
    int32 channels = 0;
    ORBEDEN_BIND_CHANGED(MarkDirty)
    int32 format = 0;
    //改动后必须重传 GPU 纹理，否则会静默复用按旧颜色空间创建的纹理。
    ORBEDEN_BIND_CHANGED(MarkDirty)
    TextureColorSpace colorSpace = TextureColorSpace::SRGB;
    //Alpha 语义；画布输出必须是预乘，普通图片保持直通。
    ORBEDEN_BIND_CHANGED(MarkDirty)
    TextureAlphaMode alphaMode = TextureAlphaMode::Straight;
    List<uint8> pixels;
    //内容版本：像素或尺寸变化时推进；托管侧的 CPU 采样缓存据此失效。
    uint64 revision = 1;

    //判断 GPU 纹理是否需要重新上传
    bool IsDirty() const;

    //标记 GPU 纹理需要重新上传；整块上传
    void MarkDirty();

    //清除 GPU 刷新标记
    void ClearDirty();

    //创建动态纹理：保有 CPU 像素，允许局部写入。channels 为 1、3 或 4。
    static Texture2D* CreateDynamic(int32 width, int32 height, int32 channels);

    //创建渲染目标纹理：不要求 CPU 像素，尺寸可随目标调整。
    static Texture2D* CreateRenderTarget(int32 width, int32 height);

    //局部写入像素。byteCount 是 pixels 的字节数，rowStride 是每行字节数。
    //越界或不完整的输入直接拒绝，不做部分写入。
    ORBEDEN_BIND_BUFFER(pixels, byteCount)
    bool UpdateRegion(int32 x, int32 y, int32 width, int32 height, const uint8* pixels, int32 byteCount, int32 rowStride);

    //调整渲染目标的尺寸；非渲染目标拒绝。尺寸变化后内容未定义。
    bool ResizeRenderTarget(int32 width, int32 height);

    //判断当前是否为渲染目标纹理
    bool IsRenderTarget() const;

    //读取 Alpha 语义
    TextureAlphaMode GetAlphaMode() const;

    //读取内容版本；重新导入或局部写入后都会推进。
    uint64 GetRevision() const;

    //读取后端允许的最大纹理边长
    static int32 GetMaximumSize();
};
