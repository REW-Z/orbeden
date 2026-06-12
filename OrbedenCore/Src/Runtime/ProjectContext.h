#pragma once

#include <string>

//当前打开的 Orbeden 项目上下文，负责把逻辑资源路径映射到项目目录。
class ProjectContext
{
public:
    //设置当前项目根目录和资源根目录
    static void SetProjectRoot(const std::string& root, const std::string& resourceRoot = "Resources");

    //清空当前项目上下文
    static void Clear();

    //判断是否已经打开项目
    static bool HasProject();

    //获取当前项目根目录
    static const std::string& GetProjectRoot();

    //获取当前项目资源根目录
    static const std::string& GetResourceRoot();

    //把项目相对路径解析为磁盘路径
    static std::string ResolveProjectPath(const std::string& path);

    //把 Resources/... 逻辑路径解析为当前项目的磁盘路径
    static std::string ResolveResourcePath(const std::string& path);

    //从当前目录和可执行文件目录向上查找项目目录
    static std::string FindProjectRoot(const std::string& projectDirectoryName, const std::string& executablePath = "");
};
