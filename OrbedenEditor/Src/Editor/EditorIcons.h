#pragma once

#include "Defines/types.h"

#include <imgui.h>

#include <filesystem>
#include <string>

//编辑器自带图标：从资源目录读取 PNG 并上传为 OpenGL 纹理
class EditorIcons
{
public:
    //设置图标目录，首次取图标时才真正读取文件
    static void SetDirectory(const std::filesystem::path& directory);

    //按图标名取纹理，目录里没有同名 PNG 时返回 0
    static ImTextureID Get(const std::string& name);

    //释放全部图标纹理
    static void Shutdown();
};
