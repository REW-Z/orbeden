# 默认 UI 字体

`Default.ttf` 是 Cubic 11（俐方體11號）1.500，供 Text 与 TextField 未指定字体时使用。源文件路径为 `Builtin/Fonts/Default.ttf`，导入对象 Key 为 `Builtin/Fonts/Default.ttf//Font/Main`，随内置内容复制和打包。

它是 11×11 中文点阵字体，默认以 Bitmap 导入，Atlas 页尺寸默认为 1024。修改源文件 Inspector 的 Import Settings 后 Apply，所有使用该 Font 的组件共用新设置。

来源：https://github.com/ACh-K/Cubic-11/releases/tag/v1.500

固定提交：`4c566f7d6cc5c05ee360fe9cff56b5da1fcafd4d`。上游字体为 `fonts/ttf/Cubic_11.ttf`，由 Tools/OrbedenThirdParty 按锁文件下载并发布为 `Default.ttf`。

字体字节未修改，分发许可证见同目录 `OFL.txt`。
