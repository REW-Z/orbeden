#pragma once

#include <string>
#include <functional>

//构建资源二进制、XML 场景和发布清单
//RawContent 原样复制，场景由编辑器处理关联
class PlayerContentCooker
{
public:
    //把源内容根内的资源全部 cook 到输出目录，输出目录会被清空重建
    static bool Cook(const std::string& sourceContentRoot, const std::string& cookedOutputRoot, std::string& error,
        const std::string& settingsTable,
        const std::function<bool(const std::string&, const std::string&, std::string&)>& cookWorld);
};
