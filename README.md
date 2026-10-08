# orbeden
a game engine project.

自有代码采用 [MIT License](LICENSE)。第三方组件保留各自许可，见 [第三方声明](THIRD-PARTY-NOTICES.md)。

安装 .NET 10 SDK 与 Visual Studio 2026 的 C++、Windows SDK、CMake/Ninja 工具，打开 `orbeden.slnx`：

1. 选择 `Debug | x64` 或 `Release | x64`。
2. 在解决方案资源管理器中右键 **OrbedenThirdParty → 生成**，自动下载、校验并编译第三方库，发布头文件、`.lib` 和 `.dll`。
3. 首次构建时依次生成 **OrbedenCore → OrbedenEditor**。

无需手动执行 PowerShell、Python 或命令行脚本。源码、第三方库和默认字体由锁文件还原，不入 Git；依赖管理、升级与离线用法见 [OrbedenThirdParty](Tools/OrbedenThirdParty/README.md)。
