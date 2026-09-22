#include "Runtime/Object/Texture2D.h"

OBJECT_TYPE_IMPLEMENT(Texture2D, Object)

//判断 GPU 纹理是否需要重新上传
bool Texture2D::IsDirty() const
{
    return gpuDirty;
}

//标记 GPU 纹理需要重新上传
void Texture2D::MarkDirty()
{
    gpuDirty = true;
}

//清除 GPU 刷新标记
void Texture2D::ClearDirty()
{
    gpuDirty = false;
}
