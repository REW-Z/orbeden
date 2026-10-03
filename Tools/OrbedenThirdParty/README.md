# OrbedenThirdParty

第三方库生成器。把 `vendor/` 下的第三方源码编成静态库，并把「头文件 + 库文件 + 授权文件」发布到
`OrbedenCore/Src/ThirdParty/<库>/`，主工程只按**库依赖**引用，永远不编译第三方源码。

## 为什么单独一个工程

`vcxproj` 的「重新生成」按 MSBuild 语义等于 Clean + Build。第三方源码一旦进入主工程的 `ClCompile`，
每次重新生成都要重啃整个库。放到这里之后：

- 主工程的日常构建完全不碰第三方源码；
- 本工程自身也有增量跳过：产物比输入新时整段不做，实测约 0.3 秒。

判断标准很简单：**`OrbedenCore/Src/ThirdParty/<库>/` 下只允许出现 `include/`、`lib/`、`LICENSE*`
与说明文件，不允许出现 `.c`／`.cpp`**。出现源码就说明这个库还没纳入本工程。

## 目录

| 路径 | 内容 |
|---|---|
| `vendor/<库>/` | 第三方源码，是唯一的版本来源 |
| `src/` | 我们自己写的胶水编译单元（单头库的实现、后端包装） |
| `CMakeLists.txt` | 各库的编译选项；上游自带 CMake 的用 `add_subdirectory`，其余用 `add_library` 直接列源文件 |
| `versions.txt` | 版本清单，同时是增量输入：改版本号即触发重新生成 |
| `physx-preset.xml` | PhysX 生成预设（静态库、CPU-only、ninja）；`physx-build.bat` 会把它复制进 SDK 的预设目录 |
| `physx-build.bat` | PhysX 的生成与构建驱动：vcvars + 自带 ninja + 八个静态库目标 |
| `IMGUI_SUBSET.txt` | vendor 里 imgui 收录了哪些文件、为什么 |
| `Build/` | CMake 生成物与中间产物，不进版本管理 |

发布出去的目录：

```
OrbedenCore/Src/ThirdParty/<库>/include/         头文件
OrbedenCore/Src/ThirdParty/<库>/lib/WindowsX64/<配置>/  静态库
OrbedenCore/Src/ThirdParty/<库>/LICENSE*          授权文件
```

imgui 与 glfw 的目录形状按主工程的既有 include 路径发布：`ThirdParty/imgui/`（根、`backends/`、`misc/cpp/`）
与 `ThirdParty/glfw/include/`。

## 带全局状态的库必须全进程唯一

GLFW、ImGui、GLAD 都在进程内持有单例状态（窗口表、`GImGui` 上下文、GL 函数指针表）。
编辑器 exe 与 `OrbedenCore.dll` 都会直接调用它们，静态链接等于各持一份、互不相通：

- **GLFW** 建动态库：宿主动态加载 `glfw3.dll`，两边共用同一份窗口状态；
- **ImGui / GLAD** 按 `dllexport` 编译，符号由 `OrbedenCore.dll` 导出，编辑器从导入库取，
  自身不再链接这两份静态库。静态宿主（Player）下 dllexport 只是多一张导出表，无副作用。

新增第三方库时先问一句：它有全局状态吗？有，就必须让进程内只有一份。

## 增量逻辑

`GenerateThirdPartyLibraries` 的输入是 `vendor/` 全部文件、`CMakeLists.txt`、`versions.txt` 与本工程文件；
输出是发布目录里的库文件。任一个输出缺失，或任一个输入更新，就会重新配置并构建；否则整段跳过。

有一处必须保留：目标末尾的 `Touch`。`Copy` 在内容未变时会跳过复制，目标文件的时间戳因此不会推进，
下一次增量检查又会认为输出比输入旧，于是无限重跑。`Touch` 把输出统一抬到最新。

## 升级某个库

1. 换掉 `vendor/<库>` 的源码；
2. 改 `versions.txt` 里的版本号；
3. 重新生成该工程（Debug 与 Release 都要）；
4. 把 `OrbedenCore/Src/ThirdParty/<库>/` 下变化的 include 与 lib 一起提交。

## 现有库

| 库 | 版本 | 编译选项 |
|---|---|---|
| FreeType | VER-2-14-3 | 关掉 ZLIB/BZIP2/PNG/HARFBUZZ/BROTLI，只保留自带模块 |
| msdfgen | v1.13 | core-only：不建 standalone、不接 Skia、不开 OpenMP、不产 SVG/PNG |
| GLFW | 3.4 | 动态库（`glfw3.dll` + `glfw3dll.lib`）；不建示例、文档与测试 |
| Dear ImGui | 1.92.8 | 上游文件原样入库，`src/imgui_impl_opengl3_orbeden.cpp` 用引擎的 GLAD2 包一层；按 dllexport 编译，由 OrbedenCore.dll 统一导出 |
| glad | 2.0.8 | 生成产物直接入库（GL 4.3 core），编成静态库 |
| cgltf | 1.15 | 单头库，实现放在 `src/cgltf_impl.c` |
| stb_image | 2.30 | 单头库，实现放在 `src/stb_image_impl.c` |
| PhysX | 5.9.0 | 官方生成器 + 自定义预设：静态库、CPU-only、ninja 生成器（本机只有 VS18，上游预设停在 VS2022） |

注意 `src/` 下的 `.c` 编译单元必须是纯 ASCII：MSVC 按系统代码页读 C 文件，
中文注释的 UTF-8 尾字节可能被当成行继续符，把后面的 `#define` 一起吞掉。
