#pragma once

#include <string>

//把内容根内可导入的资源 cook 成打包用二进制，并复制场景与生成清单。
//RawContent 原样复制；其余内容只导入支持的资源并复制场景
class PlayerContentCooker
{
public:
    //把源内容根内的资源全部 cook 到输出目录，输出目录会被清空重建
    static bool Cook(const std::string& sourceContentRoot, const std::string& cookedOutputRoot, std::string& error,
        const std::string& settingsTable = {});
};
