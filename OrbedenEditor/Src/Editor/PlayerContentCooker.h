#pragma once

#include <string>

//把内容根内可导入的资源 cook 成打包用二进制，并复制场景与生成清单。
//只有可导入的源文件会被导入；无导入器的文件（脚本、源码、MTL、包含文件）不进入产物。
class PlayerContentCooker
{
public:
    //把源内容根内的资源全部 cook 到输出目录，输出目录会被清空重建
    static bool Cook(const std::string& sourceContentRoot, const std::string& cookedOutputRoot, std::string& error);
};
