#pragma once

#include <string>

//当前内容路径定义，负责把内容相对路径和 Resource 路径映射到磁盘路径。
class PathDefines
{
public:
    //设置当前内容根目录和资源根目录
    static void SetContentRoot(const std::string& root, const std::string& resourceRoot = "Resource");

    //清空当前内容路径定义
    static void Clear();

    //判断是否已经设置内容根目录
    static bool HasContentRoot();

    //获取当前内容根目录
    static const std::string& GetContentRoot();

    //获取当前资源根目录
    static const std::string& GetResourceRoot();

    //把内容相对路径解析为磁盘路径
    static std::string GetContentFilePath(const std::string& path);

    //把 Resource/... 逻辑路径解析为当前内容目录中的磁盘路径
    static std::string GetResourceFilePath(const std::string& path);

};
