#pragma once

#include "Defines/types.h"

//项目共享的物理层矩阵，缺少配置时保持全部层互相碰撞。
//存在项目根的 GameSettings.ini 的 [Layers] 分块里，见 GameSettingsFile。
class LayerSettings
{
public:
    //内容根或配置文件变化时重新读取矩阵。
    static void Refresh();

    //按 Ens 层索引获取允许碰撞的目标层
    static uint32 GetCollisionMask(uint32 layer);
};
