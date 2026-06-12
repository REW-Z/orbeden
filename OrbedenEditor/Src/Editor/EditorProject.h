#pragma once

#include <string>

class Application;

//编辑器当前打开的项目，负责读取 .oeproj 并加载启动 World。
class EditorProject
{
private:
    Application& app;
    std::string projectRoot;
    std::string projectName;
    std::string resourceRoot = "Resources";
    std::string startupWorld;
    std::string lastError;

public:
    explicit EditorProject(Application& application);

    //从项目目录中查找并加载 .oeproj
    bool LoadProjectFolder(const std::string& folder);

    //读取指定 .oeproj 并加载启动 World
    bool LoadProjectFile(const std::string& projectFile);

    //保存当前 World 到项目启动场景
    bool SaveStartupWorld();

    //获取当前项目根目录
    const std::string& GetProjectRoot() const;

    //获取当前项目名
    const std::string& GetProjectName() const;

    //获取启动场景完整路径
    std::string GetStartupWorldPath() const;

    //获取最近一次错误
    const std::string& GetLastError() const;

    //判断是否已经打开项目
    bool HasProject() const;
};
