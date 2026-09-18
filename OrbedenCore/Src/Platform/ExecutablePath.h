#pragma once

#include <filesystem>
#include <string>

//当前进程可执行文件位置。
//优先向系统查询权威路径，查询不到时才回退到调用方给出的启动路径：
//启动路径是相对的、或只有裸文件名时无法推导出真正的位置。
namespace ExecutablePath
{
    //获取可执行文件所在目录；查询失败且没有回退路径时返回进程工作目录。
    std::filesystem::path GetDirectory(const std::string& fallbackExecutablePath = std::string());
}
