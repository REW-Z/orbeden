# Orbeden 品牌图标

源图：`Docs/Brand/orbeden-icon-white.svg`。

运行 `pwsh -STA -File Build/ExportEditorBrand.ps1` 从矢量源图重新导出：

- `Resources/Icons/Orbeden.png` 和 `Resources/Icons/256/Orbeden.png`：32 / 256 像素透明白色图标，由 WelcomePanel 按背景亮度染为黑色或白色。
- `Resources/Brand/Orbeden.ico`：16、20、24、32、40、48、64、128、256 像素，白色标志搭配黑色圆角底，供 Windows 窗口、任务栏和可执行文件使用。

`Resources/OrbedenEditor.rc` 将 ICO 嵌入为 GLFW 约定的 `GLFW_ICON` 资源；主窗口与浮动窗口创建时自动使用。PNG 随现有 Resources 目录复制流程分发，不依赖游戏项目或 Content 目录。
