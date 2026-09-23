# 原始资源文件图标

由 `EditorSourceIconCatalog` 独立映射，与导入对象按类型使用的上级目录图标区分。
mtl、fbx、png、jpg、obj 等非代码文件使用 `Fallback.png`。

Shader 代码使用 `ShaderFile.png`，C# 使用 `CSharpScript.png`，C/C++ 代码及头文件使用 `CppScript.png`。这些图标复制自对应通用图标，在 SourceFiles 中独立维护；代码文件不显示展开按钮。

World 使用 `World.png`（透明玻璃球、土壤和树）；txt、json、xml 等使用 `TextFile.png`。World 与纯文本文件均不显示展开按钮。

本目录 PNG 均为 32×32，256×256 对应版本在 `../256/SourceFiles/`。

Fallback 使用内置 image_gen 生成，从高清原图导出透明的 32×32 和 256×256 PNG。提示词：

> Generate one square PNG UI icon with true transparent alpha background: a simple closed kraft-brown cardboard shipping box with pale packing tape across its top, three-quarter isometric view, centered with transparent padding. Clean flat shaded game editor icon, large simple shapes readable at 16-24 pixels. No text, labels, logos, badges, ground, cast shadow or checkerboard. This is a fallback icon for original asset source files.
