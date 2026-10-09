#pragma once

#include "Defines/types.h"

//项目存档版本。`.oeproj` 的 version 属性记录项目建立或上次更新时的值。
//**引擎侧改动会影响已有游戏项目时手工递增本值**，并在 Docs/BuildAndPackaging.md 的
//「引擎更新与项目同步」一节记录该次改动做了什么。
//忘记递增的后果是打开老项目时不会提示更新，而构建会在别处以难以排查的方式失败
//（工具集不匹配、MetaGen 参数过期、绑定签名不符）。宁可多递增，也不要漏。
//编辑器载入项目时比对存档值：相等直接加载，落后提示更新，超前只提示不拦。
constexpr uint32 OrbedenProjectVersion = 64;
