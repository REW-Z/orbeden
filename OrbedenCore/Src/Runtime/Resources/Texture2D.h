#pragma once

#include "Runtime/Object.h"

#include <string>

//CPU纹理数据，不绑定具体渲染API
class Texture2D : public Object
{
    OBJECT_TYPE_DECLARE(Texture2D)

public:
    std::string name;
    int32 width = 0;
    int32 height = 0;
    int32 channels = 0;
    int32 format = 0;
    List<uint8> pixels;
};
