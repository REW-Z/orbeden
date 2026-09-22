#pragma once

#include "Rendering/Backend/GpuResourceIDs.h"
#include "Runtime/Object/Object.h"

#include <string>

class GpuResourceManager;

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
    List<uint8> pixels;

    //判断 GPU 纹理是否需要重新上传
    bool IsDirty() const;

    //标记 GPU 纹理需要重新上传
    void MarkDirty();

    //清除 GPU 刷新标记
    void ClearDirty();
};
