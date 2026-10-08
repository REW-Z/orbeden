# 第三方组件与许可

Orbeden 自有代码采用根 LICENSE 中的 MIT 许可。以下第三方组件保留各自的版权、许可和署名，根 MIT 不替换这些许可。

依赖版本与来源由 Tools/OrbedenThirdParty/dependencies.lock.json 固定。许可全文独立保存在受 Git 跟踪的 Tools/OrbedenThirdParty/licenses/<组件>/，不随第三方源码目录一起忽略。还原时检查许可快照哈希；构建 SDK、编辑器与 Player 时复制根 LICENSE、本声明和完整许可目录。重新打包或分发时应保留这些材料。

## 组件清单

| 组件 | 版本 | 选用许可 | 许可全文目录 | 用途 |
|---|---|---|---|---|
| FreeType | 2.14.3 | FTL | licenses/freetype/ | 引擎、编辑器、游戏 |
| msdfgen | 1.13 | MIT | licenses/msdfgen/ | 引擎、编辑器、游戏 |
| PhysX | 5.9.0 | BSD-3-Clause | licenses/physx/ | 引擎、游戏 |
| GLFW | 3.4 | zlib/libpng | licenses/glfw/ | 引擎、编辑器、游戏；动态库 |
| Dear ImGui | 1.92.8 | MIT | licenses/imgui/ | 引擎、编辑器 |
| cgltf | 1.15 | MIT | licenses/cgltf/ | 引擎、游戏 |
| stb_image | 2.30 | MIT（双许可中选用 MIT） | licenses/stb/ | 引擎、编辑器、游戏 |
| GLAD 生成加载器 | 生成器 2.0.8 | (WTFPL OR CC0-1.0) AND Apache-2.0 | licenses/glad/ | 引擎、编辑器、游戏 |
| khrplatform.h | 生成器内置版本 | Khronos MIT 式许可 | licenses/glad/KHRONOS.txt | 引擎、编辑器、游戏 |
| .NET native host | 10.0.9，win-x64 / win-x86 | MIT 与包内第三方声明 | licenses/dotnet-win-x64/、licenses/dotnet-win-x86/ | 编辑器 |
| Cubic 11（俐方體11號） | 1.500，锁文件固定提交 | SIL OFL-1.1 | licenses/cubic-font/OFL.txt | 内置 UI 像素字体 |
| GLAD 生成器 | 2.0.8 | MIT；规范另含 Apache-2.0 / Khronos 声明 | licenses/glad-generator/ | 构建工具 |
| Jinja2 | 3.1.6 | BSD-3-Clause | licenses/jinja2/ | 构建工具 |
| MarkupSafe | 3.0.3 | BSD-3-Clause | licenses/markupsafe/ | 构建工具 |
| CPython embedded | 3.13.9，Windows x64 | PSF-2.0 与包内附加声明 | licenses/python-runtime/ | 仅用于运行上游 GLAD |

表中 licenses/ 均相对于 Tools/OrbedenThirdParty/；产物中相同内容位于 ThirdPartyLicenses/。完整版权年份、作者与许可条款以这些原文为准。

## FreeType

本项目明确选用 FTL，不选用其 GPL 备选许可。完整的 LICENSE.TXT 与 FTL.TXT 必须保留；ADDITIONAL-NOTICES.txt 保存 BDF/PCF、内置 zlib 和 HarfBuzz 派生代码的附加许可声明。完整上游下载包含 GPL 备选文本，不意味着本项目采用 GPL。

本声明同时提供 FreeType 所要求的产品文档署名：

> Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.

分发修改后的 FreeType 源码时，须保留原有声明、FTL 全文，并记录修改。不得未经授权使用作者名字为产品背书。

## GLAD 与 Khronos

本项目选用 CC0-1.0 AND Apache-2.0。保留生成代码中的 SPDX 声明、CC0-1.0.txt、LICENSE-APACHE-2.0.txt 和 KHRONOS.txt。生成器的许可与生成代码的许可分别保存。生成规格固定为 OpenGL 4.3 core、零扩展、启用 loader，使用生成器内置规范。

## .NET 与字体

.NET 宿主包的 LICENSE.TXT 与 THIRD-PARTY-NOTICES.TXT 都随产物复制。主机安装的 .NET runtime/SDK 是独立前置条件；若将运行时纳入发行包，还须附带所分发运行时自己的声明。

默认字体按 OFL-1.1 分发，版权声明和保留字体名称见原文。字体字节不修改；内置字体目录继续保留 OFL.txt。重新打包内容时不能删掉字体许可。
