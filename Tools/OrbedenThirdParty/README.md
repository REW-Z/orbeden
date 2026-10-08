# OrbedenThirdParty

Orbeden 的第三方依赖管理与构建工程。`dependencies.lock.json` 是唯一的依赖版本来源，记录固定提交、下载地址、SHA-256、选用许可、许可快照和 GLAD 生成参数。还原不会读取上游 main 或自动升级版本。

Git 跟踪锁文件、构建脚本、Orbeden 自己的胶水代码和 `licenses/` 中的许可全文。第三方源码、发布头文件、下载缓存、.lib、.dll 与默认字体均不入库。

## 首次使用

需要 .NET 10 SDK 与 Visual Studio 2026+ 的 C++、Windows SDK、CMake 和 Ninja 工具。依赖管理器使用 C# 和 .NET 标准库，不需要安装系统 Python 或 pip。首次下载需要访问 GitHub、PyPI、NuGet、python.org 及 PhysX 的官方工具服务器。GLAD 使用锁定的嵌入式解释器，PhysX 使用上游 packman 准备的解释器。

在 Visual Studio 中完成构建：

1. 打开根目录的 orbeden.slnx，选择 Debug | x64 或 Release | x64。
2. 右键 OrbedenThirdParty，点击“生成”。工程会自动还原依赖、编译各个库并发布头文件、.lib 和 .dll。
3. 首次构建主工程时，依次生成 OrbedenCore 与 OrbedenEditor。

“重新生成”会清理当前配置的 CMake 编译目录，再构建并发布；“清理”只清理当前配置的中间目录，不删除下载的上游源码和归档缓存。要同时准备 Debug 与 Release，切换配置并各生成一次。

生成的引擎库位于 OrbedenCore/Src/ThirdParty/<组件>/lib/WindowsX64/<配置>/。GLFW 提供 glfw3dll.lib 和 glfw3.dll，其余引擎库使用现有静态链接策略；.NET 宿主的头文件、导入库和 DLL 发布到 OrbedenEditor/Src/ThirdParty/dotnet/。

OrbedenThirdParty.csproj 是唯一工程。Program.cs 负责下载与还原，LibraryBuild.cs 直接调用 CMake/PhysX 并发布产物。构建自动查找 VS 安装位置，无需手写路径或运行 PowerShell/Python 脚本。

工程关闭 VS 的快速更新检查，每次“生成”都会进入完整校验流程；内容未变且产物完整时，由 C# 管理器跳过第三方编译。缺失头文件或库文件时会重新构建并发布。设计时构建不会下载或编译依赖。

本工程独立于 Core/Editor 的构建链；日常单独重新生成 Core/Editor 不编译第三方源码。普通 MSBuild Build 也默认执行依赖构建，不依赖 BuildingInsideVisualStudio 标记。

## 文件与产物

| 路径 | 用途 | Git 跟踪 |
|---|---|---|
| `dependencies.lock.json` | 版本、固定来源、归档哈希、生成参数和许可快照哈希 | 是 |
| `Program.cs`、`LibraryBuild.cs` | C# 还原、构建与发布实现 | 是 |
| `licenses/<组件>/` | 完整许可、版权声明及附加声明 | 是 |
| `BuildAdapters/` | Orbeden 自有编译适配代码：单头库实现入口与后端加载器配置 | 是 |
| OrbedenThirdParty.csproj、CMakeLists.txt、physx-* | 工程与上游编译规则 | 是 |
| `vendor/<组件>/` | 上游工作副本；PhysX 保留 SDK 目录和仓库根许可，排除未使用的其他产品 | 否 |
| `.cache/` | 按 SHA-256 命名的归档及 PhysX packman 工具缓存 | 否 |
| Build/、obj/ | 编译中间文件 | 否 |
| `../../OrbedenCore/Src/ThirdParty/` | 引擎使用的头文件与库 | 否 |
| `../../OrbedenEditor/Src/ThirdParty/` | .NET 原生宿主头文件、导入库和 DLL | 否 |
| `../../OrbedenEditor/Templates/Builtin/Fonts/Default.ttf` | 默认 UI 字体 Cubic 11 | 否 |

引擎库发布路径仍是 `<组件>/lib/WindowsX64/<Debug|Release>/`，头文件路径保持现有 include 契约。许可发布改为直接读取受 Git 跟踪的 licenses/，不依赖忽略目录里的副本。

## 固定依赖与编译策略

| 组件 | 版本 | 编译/发布策略 |
|---|---|---|
| FreeType | 2.14.3 | FTL；关闭外部 ZLIB/BZIP2/PNG/HARFBUZZ/BROTLI |
| msdfgen | 1.13 | core-only；关闭 standalone、Skia、OpenMP、SVG/PNG |
| GLFW | 3.4 | 动态库；不建示例、文档与测试 |
| Dear ImGui | 1.92.8 | 静态库；后端通过 Orbeden 包装使用 GLAD |
| GLAD | 2.0.8 | 使用生成器内置规范，GL 4.3 core、零扩展、loader |
| cgltf | 1.15 | 单头库，实现位于 BuildAdapters/cgltf_impl.c |
| stb_image | 2.30 | 固定提交，选 MIT，实现位于 BuildAdapters/stb_image_impl.c |
| PhysX | 5.9.0 | 上游官方生成器；静态库、CPU-only、Ninja |
| .NET native host | 10.0.9 | NuGet 官方 win-x64 / win-x86 host 包 |
| Jinja2 / MarkupSafe | 3.1.6 / 3.0.3 | 仅用于运行 GLAD，不进入引擎 |
| CPython embedded | 3.13.9，Windows x64 | 自动还原到 vendor/python-runtime，仅运行上游 GLAD 生成器 |
| Cubic 11（俐方體11號） | 1.500，锁文件固定提交 | 字体字节不修改，OFL-1.1；默认 Bitmap 导入 |

GLFW 的全局状态由 glfw3.dll 统一持有。ImGui 和 GLAD 按 dllexport 编译，由 OrbedenCore.dll 导出，编辑器从导入库引用；保留现有进程内共享状态的行为。运行时使用 /MDd 或 /MD。

## 校验、增量和离线

下载成功后先校验 SHA-256，再解压。还原目录记录原始文件的哈希，每次还原检查文件缺失或变化；发现变化时从已校验归档重新创建整个工作副本。本地修改上游源码会被覆盖，应把需要维护的改动显式纳入构建脚本或自己的胶水代码。

GLAD 使用 --reproducible，规范与 Khronos 头文件从固定生成器内置资源读取，不访问实时的 Khronos 注册表。许可快照不匹配时直接失败。

C# 程序在还原后记录包状态、胶水代码、锁文件和构建规则的内容哈希，并校验所有已发布头文件与库的哈希。输入不变且产物完整时跳过编译；产物缺失或变化时重新构建与发布。状态保存到 Build/completed-<配置>.json，上游生成器的缓存不参与输入。

已有归档缓存时，可设置项目属性 ThirdPartyOffline=true，仅从校验过的本地归档还原。缓存缺失或哈希错误会报错。首次 PhysX 构建仍需要官方 packman 下载其固定工具包。编译器、Windows SDK、.NET SDK、CMake/Ninja 属于本机工具链；PhysX 工具版本由锁定 SDK 内的上游 packman 清单控制。

## 升级依赖

1. 在锁文件中更新目标版本、固定提交和下载地址；使用实际下载文件计算新的 SHA-256，不能只改显示版本号。
2. 从该归档原样提取许可到 licenses/<组件>/，同步 licenseFiles 和 licenseSnapshots 的 SHA-256；补齐子组件、NOTICE 和生成代码的许可。
3. 如涉及头文件结构或编译选项，更新 CMakeLists.txt、LibraryBuild.cs 与胶水代码。
4. 执行还原，构建 Debug 和 Release，并构建主工程验证引用契约。
5. 提交锁文件、脚本与许可快照。源码和产物不提交。

## 许可与新仓库

根 LICENSE 只许可 Orbeden 自有代码；第三方保留原许可，见根 THIRD-PARTY-NOTICES.md。Core 发布 SDK、编辑器和 Player 构建会复制根许可、第三方声明及整个 licenses/ 到产物中。游戏重新打包时也要保留这些文件。字体的 OFL 同时位于内置字体目录。

重新初始化 Git 后，git add . 会遵循新的忽略规则。旧仓库已经跟踪的文件不会因为新增 .gitignore 自动取消跟踪；本次按计划由你删除旧 .git 并创建新仓库，因此不改动当前 Git 索引。新 Git 历史不改变已发布旧版本的授权。

重建 GitHub 仓库时不要让 GitHub 自动生成 README、LICENSE 或 .gitignore，以保持远端为空。重新初始化后首次提交按你的计划执行：

```powershell
git init
git add .
git commit -m "relicensing mpl2.0 to mit"
```
