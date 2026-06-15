#pragma once

#include "Runtime/Object/Object.h"
#include "Runtime/Object/Texture2D.h"

//CPU 天空盒资源，保存六个跨 API 的纹理面引用。
class Skybox : public Object
{
    OBJECT_TYPE_DECLARE(Skybox)

public:
    Ref<Texture2D> right;
    Ref<Texture2D> left;
    Ref<Texture2D> top;
    Ref<Texture2D> bottom;
    Ref<Texture2D> front;
    Ref<Texture2D> back;
};

