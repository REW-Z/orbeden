#pragma once

#include "Runtime/Object/Object.h"

#include <string>

//CPU着色器资源，保存源码但不编译GPU程序
class MaterialShader : public Object
{
    OBJECT_TYPE_DECLARE(MaterialShader)

public:
    std::string name;
    std::string vertexPath;
    std::string fragmentPath;
    std::string vertexSource;
    std::string fragmentSource;
};
