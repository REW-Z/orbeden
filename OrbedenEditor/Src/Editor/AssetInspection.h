#pragma once

#include "Runtime/Object/Object.h"
#include <string>

//Project 与 Inspector 共用的实际资源检查数据。
class AssetInspection
{
public:
    //读取源文件本次导入的对象清单，或内部文件的唯一对象。
    static std::string Inspect(const std::string& sourceKey, const std::string& outputDirectory = "");

    //读取复杂容器的只读摘要，避免复制大型几何或像素数组。
    static bool TryGetFieldSummary(Object* object, const char* fieldName, std::string& result);
};
