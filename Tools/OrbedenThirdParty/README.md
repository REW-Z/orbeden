# OrbedenThirdParty

第三方库生成器。把 `vendor/` 下的第三方源码编成静态库，并把「头文件 + 库文件 + 授权文件」发布到
`OrbedenCore/Src/ThirdParty/<库>/`，主工程只按**库依赖**引用，永远不编译第三方源码。

## 为什么单独一个工程

`vcxproj` 的「重新生成」按 MSBuild 语义等于 Clean + Build。第三方源码一旦进入主工程的 `ClCompile`，
每次重新生成都要重啃整个库。放到这里之后：

- 主工程的日常构建完全不碰第三方源码；
- 本工程自身也有增量跳过：产物比输入新时整段不做，实测约 0.3 秒。

## 目录

| 路径 | 内容 |
|---|---|
| `vendor/<库>/` | 第三方源码，是唯一的版本来源 |
| `CMakeLists.txt` | 各库的编译选项；新增库在这里 `add_subdirectory` |
| `versions.txt` | 版本清单，同时是增量输入：改版本号即触发重新生成 |
| `Build/` | CMake 生成物与中间产物，不进版本管理 |

发布出去的目录：

```
OrbedenCore/Src/ThirdParty/<库>/include/         头文件
OrbedenCore/Src/ThirdParty/<库>/lib/WindowsX64/<配置>/  静态库
OrbedenCore/Src/ThirdParty/<库>/LICENSE*          授权文件
```

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

`glad` 与 `imgui` 目前仍是主工程的源码依赖，属待整理项；未来的目标是也纳入本工程。
