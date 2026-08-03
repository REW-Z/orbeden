#pragma once

#include "Rendering/Backend/GpuResourceIDs.h"
#include "Runtime/Object/Object.h"
#include "Runtime/Object/Texture2D.h"

class GpuResourceManager;

//CPU 天空盒资源，保存六个跨 API 的纹理面引用。
class Skybox : public Object
{
    OBJECT_TYPE_DECLARE(Skybox)

private:
    friend class GpuResourceManager;

    //GPU 天空盒句柄及其管理器存储位置。
    GpuCubeTextureID gpuSkybox;
    int32 gpuSkyboxStorageIndex = -1;

public:
    Ref<Texture2D> right;
    Ref<Texture2D> left;
    Ref<Texture2D> top;
    Ref<Texture2D> bottom;
    Ref<Texture2D> front;
    Ref<Texture2D> back;
};
