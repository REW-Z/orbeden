#pragma once

#include "Defines/types.h"

//游戏项目（.oeproj）的格式版本。
//引擎侧改动会影响已有游戏项目时手工递增本值，并在 Docs/ProjectConventions.md 记录该级迁移。
//Editor 载入项目时比对存档版本：相等直接加载，落后走升级流程，超前拒绝加载。
constexpr uint32 OrbedenProjectVersion = 3;
