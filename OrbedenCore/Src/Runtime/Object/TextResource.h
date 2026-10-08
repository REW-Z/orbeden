#pragma once

#include "Runtime/Object/Object.h"

#include <string>

//CPU 文本资源：源文件原样保存，供组件按 Key 引用后自行解析。
//文本在导入时读进 cooked 产物，运行时不依赖源文件路径，打包也不需要带上源文件。
class TextResource : public Object
{
    OBJECT_TYPE_DECLARE(TextResource)

public:
    //源文件的原始文本，不解释内容、不解析格式
    std::string text;
};
