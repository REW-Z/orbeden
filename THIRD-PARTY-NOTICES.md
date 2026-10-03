# 第三方组件与许可

本文件列出 Orbeden 引擎、编辑器与用它构建的游戏产物中包含的第三方组件、版本、许可与必须保留的署名。
完整许可文本随源码保存在各自的目录里；**分发二进制时必须把对应的许可文本一并附上**。

分发形式：这些库以静态库或生成代码的形式进入 `OrbedenCoreStatic.lib`、编辑器可执行文件与游戏 Player，
属于“以二进制形式再分发”，因此下列署名与许可文本是义务而不是可选项。

## 汇总

| 组件 | 版本 | 许可 | 完整文本 | 进入的产物 |
|---|---|---|---|---|
| FreeType | 2.14.3 | FTL（FreeType License） | `OrbedenCore/Src/ThirdParty/freetype/LICENSE.TXT`、`FTL.TXT` | 引擎、编辑器、游戏 |
| msdfgen | v1.13 | MIT | `OrbedenCore/Src/ThirdParty/msdfgen/LICENSE.txt` | 引擎、编辑器、游戏 |
| PhysX | 5.9.0 | BSD-3-Clause | `OrbedenCore/Src/ThirdParty/PhysX/LICENSE.md` | 引擎、游戏 |
| GLFW | 3.4 | zlib/libpng | `OrbedenCore/Src/ThirdParty/glfw/LICENSE.md` | 引擎、编辑器、游戏 |
| Dear ImGui | 1.92.8 | MIT | `OrbedenCore/Src/ThirdParty/imgui/LICENSE.txt` | 引擎、编辑器 |
| cgltf | 1.15 | MIT | `OrbedenCore/Src/ThirdParty/cgltf/LICENSE` | 引擎、游戏 |
| stb_image | 2.30 | MIT 或公有领域（二选一） | `OrbedenCore/Src/ThirdParty/stb/LICENSE`（自 `stb_image.h` 尾部原文提取） | 引擎、编辑器、游戏 |
| glad 生成加载器 | glad 2.0.8 | (WTFPL OR CC0-1.0) AND Apache-2.0 | `OrbedenCore/Src/ThirdParty/glad/LICENSE.md`、`LICENSE-APACHE-2.0.txt` | 引擎、编辑器、游戏 |
| khrplatform.h | Khronos | MIT 式许可 | `OrbedenCore/Src/ThirdParty/glad/include/KHR/khrplatform.h` 头部 | 引擎、编辑器、游戏 |

没有 copyleft 组件进入产物：GPL 一侧的许可（FreeType 的 GPLv2 选项）未被采用，见下。

## FreeType：必须明确选用 FTL

FreeType 是二选一的**双许可**：FTL（类 BSD，含署名条款）或 GPLv2-or-later。
本项目按 **FTL** 分发。选择 GPLv2 会要求整个引擎与游戏以 GPL 发布，因此任何情况下都不得选用。

FTL 的义务：

1. 在产品文档中声明使用了 FreeType。官方推荐措辞（年份取所用版本，当前 2.14.3 的版权行是 1996-2026）：

   > Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.

2. 以源码形式再分发时，未修改的原文件必须保留原有版权声明。本仓库的 `vendor/` 与
   `OrbedenCore/Src/ThirdParty/freetype/` 都是未修改的上游源码，声明完整。

3. 不得使用 FreeType 作者的名义做商业宣传，反之亦然。

**注意**：`vendor/freetype/LICENSE.TXT` 里提到的 `docs/GPLv2.TXT` 并未随源码分发（只保留了 `docs/FTL.TXT`），
这正是“只按 FTL 分发”的形态；整理发布目录时不要删掉 `FTL.TXT`。

## 需要出现在产品文档/关于页的署名

除 FreeType 的措辞外，下列声明需要随二进制产物一并提供（可放在同一份第三方声明文件里）：

- msdfgen — MIT，Copyright (c) 2014-2025 Viktor Chlumsky
- Dear ImGui — MIT，Copyright (c) 2014-2026 Omar Cornut
- cgltf — MIT，Copyright (c) 2018-2021 Johannes Kuhlmann
- PhysX — BSD 3-Clause，Copyright (c) 2008-2025, NVIDIA Corporation
- GLFW — zlib/libpng，Copyright (c) 2002-2006 Marcus Geelnard，Copyright (c) 2006-2019 Camilla Löwy
- stb_image — MIT 或公有领域（二选一），Copyright (c) 2017 Sean Barrett
- glad 生成加载器 — (WTFPL OR CC0-1.0) AND Apache-2.0，保留 SPDX 声明与 Apache-2.0 全文
- khrplatform.h — Khronos Group 的 MIT 式许可，Copyright (c) 2008-2018 The Khronos Group Inc.

MIT、BSD-3、zlib 与 Apache-2.0 都要求“保留版权声明与许可文本”，不要求开源衍生作品。
