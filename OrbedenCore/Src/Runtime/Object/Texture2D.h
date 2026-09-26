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

//CPU纹理数据，不绑定具体渲染API
class Texture2D : public Object
{
    OBJECT_TYPE_DECLARE(Texture2D)

private:
    friend class GpuResourceManager;

    //GPU 纹理句柄及其管理器存储位置。
    GpuTextureID gpuTexture;
    int32 gpuTextureStorageIndex = -1;
    //像素数据变化后置位，由资源管理器重新上传。
    bool gpuDirty = true;

public:
    std::string name;
    int32 width = 0;
    int32 height = 0;
    int32 channels = 0;
    int32 format = 0;
    //改动后必须重传 GPU 纹理，否则会静默复用按旧颜色空间创建的纹理。
    ORBEDEN_BIND_CHANGED(MarkDirty)
    TextureColorSpace colorSpace = TextureColorSpace::SRGB;
    List<uint8> pixels;

    //判断 GPU 纹理是否需要重新上传
    bool IsDirty() const;

    //标记 GPU 纹理需要重新上传
    void MarkDirty();

    //清除 GPU 刷新标记
    void ClearDirty();
};
