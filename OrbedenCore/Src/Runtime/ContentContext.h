#pragma once

#include <string>

//当前宿主提供的内容根目录，负责把逻辑资源路径映射到磁盘目录。
class ContentContext
{
public:
    //设置当前内容根目录和资源根目录
    static void SetContentRoot(const std::string& root, const std::string& resourceRoot = "Resource");

    //清空当前内容上下文
    static void Clear();

    //判断是否已经设置内容根目录
    static bool HasContentRoot();

    //获取当前内容根目录
    static const std::string& GetContentRoot();

    //获取当前内容资源根目录
    static const std::string& GetResourceRoot();

    //把内容相对路径解析为磁盘路径
    static std::string ResolveContentPath(const std::string& path);

    //把 Resource/... 逻辑路径解析为当前内容根的磁盘路径
    static std::string ResolveResourcePath(const std::string& path);

    //从当前目录和可执行文件目录向上查找项目目录
    static std::string FindProjectRoot(const std::string& projectDirectoryName, const std::string& executablePath = "");
};
