#pragma once

#include <string>

//当前内容路径定义：把内容根相对路径映射到磁盘路径。
//内容根由项目布局决定（<项目根>/Content），Object 派生资源的 stringid 一律是内容根相对路径，
//例如 "Meshes/cube.obj" 对应 <项目根>/Content/Meshes/cube.obj。
class PathDefines
{
public:
    //设置当前内容根目录
    static void SetContentRoot(const std::string& root);

    //清空当前内容路径定义
    static void Clear();

    //判断是否已经设置内容根目录
    static bool HasContentRoot();

    //获取当前内容根目录
    static const std::string& GetContentRoot();

    //把内容相对路径解析为磁盘路径
    static std::string GetContentFilePath(const std::string& path);
};
