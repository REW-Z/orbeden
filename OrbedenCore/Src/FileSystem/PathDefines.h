#pragma once

#include <string>

//当前项目路径定义，负责把项目相对路径和 Resource 路径映射到磁盘路径。
class PathDefines
{
public:
    //设置当前项目根目录和资源根目录
    static void SetProjectRoot(const std::string& root, const std::string& resourceRoot = "Resource");

    //清空当前项目路径定义
    static void Clear();

    //判断是否已经设置项目根目录
    static bool HasProjectRoot();

    //获取当前项目根目录
    static const std::string& GetProjectRoot();

    //获取当前项目资源根目录
    static const std::string& GetResourceRoot();

    //把项目相对路径解析为磁盘路径
    static std::string GetProjectFilePath(const std::string& path);

    //把 Resource/... 逻辑路径解析为当前项目的磁盘路径
    static std::string GetResourceFilePath(const std::string& path);

};
