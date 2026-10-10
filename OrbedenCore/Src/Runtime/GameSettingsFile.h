#pragma once

#include <string>

//项目根的 GameSettings.ini：分块存放的项目级设置，每个分块由一个子系统整块读写，其他分块原样保留。
//放的是游戏侧设置（曝光、层与碰撞矩阵等），随包发布；编辑器自身的外观在 .oeproj 的 <EditorGuiConfig> 里。
class GameSettingsFile
{
public:
    static constexpr const char* FileName = "GameSettings.ini";

    //配置文件的完整路径。项目根是内容根的上级目录（内容根固定为 <项目根>/Content），
    //因此这个文件不放在会被 cook 与打包的内容根里；Player 侧内容根是 <exe>/Content，
    //同一套推导指到包根那份。
    static std::string GetPath();
};
