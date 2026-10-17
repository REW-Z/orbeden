# 资源 Inspector、复合资源展开与项目 Layer 设置

## 本次目标和最终决策

主线是资源 Inspector 和原始复合资源展开；Project Settings 只补充本次字段编辑所需的 Layer 名称和物理碰撞矩阵。

- 选择原始文件：Inspector 展示该次导入产生的全部内部资源对象。
- 选择 Project 子资源：Inspector 只展示该对象；子项可拖入类型匹配的 ObjectField。
- 选择内部 `.orbo`：展示其中的单个对象。
- 现有 Ens、组件、多选属性事务和 Undo/Redo 保持原流程。
- **每个源文件有一个伴生 `.resinfo`**（与源文件同址，进版本管理），分两部分：**导入设置**（用户可编辑，重新导入时保留）与**内部隐含资源清单**（每次导入重新生成）。对象产物 `.orbo` 仍在 `ResourceCache/Imported`，可重建、不进版本管理。
- 扩展名只用于识别导入器。资源数量、Key、类型和字段来自原生 AssetPipeline 实际导入结果，不在 C# 重复解析 OBJ/glTF 猜测。
- **导入设置可编辑**：Inspector 改设置 → 写回伴生文件 → 重新导入 → 运行时对象与清单一并更新。设置是持久用户数据，不会因重新导入丢失。清单部分仍然只读。

## 文件布局与职责

```text
Project/
  Content/
    Models/robot.gltf
    Models/robot.gltf.resinfo             伴生文件：导入设置 + 内部资源清单（进版本管理）
    Textures/brick.png
    Textures/brick.png.resinfo            同上
  ResourceCache/
    Imported/
      Models/
        robot.gltf.<对象Key哈希>.orbo      对象产物（可重建，不进版本管理）
        robot.gltf.import.settings         交给导入工作进程的设置表（临时）
        robot.gltf.import.status           完整输入校验记录（可重建，不进版本管理）
      Textures/
        brick.png.<对象Key哈希>.orbo
    Player/
      <对象Key哈希>.orbo
      cooked.index
      ...world
```

Imported 目录结构与 Content 对应；不再使用源 Key 哈希目录、GUID 生成代次或历史版本。每个源文件的对象及可达依赖都带该源文件的完整文件名前缀，包括原始扩展名，同目录不同资源独立管理。

伴生文件与源文件**同址同名**，只多一个 `.resinfo` 后缀，因此随资源一起被复制、移动、删除和版本管理；源文件消失后由目录扫描清理。

Reimport 只删除旧 `.orbo` 产物，**伴生文件里的导入设置要保留**；清单部分连同产物一起重新生成，成功校验后整体写回，失败不恢复旧清单。对象删除后不遗留上一轮对象文件。`.resinfo.tmp` 仅用于写入完成后的文件替换，不是缓存历史代次。

工作进程通过标准输出管道传递导入结果，日志重定向到标准错误；不生成 `.result` 文件。主进程异步收取结果并写入伴生文件。**产物与 ResourceCache 可删除重建，不纳入版本管理**；伴生文件里的导入设置是用户数据，必须进版本管理。Player cook 只清理 `ResourceCache/Player`，不清理 Imported。

### `.resinfo` 的数据分区

采用 UTF-8 JSON，当前 Version=9。字段分两类：**Settings 是用户数据，其余全部是生成数据**。

| 字段 | 类别 | 职责 |
| --- | --- | --- |
| Version | — | 清单与导入协议版本；不兼容变动时递增并重建 |
| Source | 生成 | 相对 Content 的源文件路径，使用 `/` 分隔 |
| **Settings** | **用户** | **导入设置，稀疏字典：只存用户显式改过的键，缺键表示按语义自动推断** |
| Dependencies | 生成 | 按序数排序的 Content 相对引用路径；不含源文件自身、文件长度、时间戳或内容哈希 |
| Blobs | 生成 | 本次生成的全部对象文件名，包括可达依赖 |
| Data.Objects | 生成 | 完整资源 Key、原生类型、BlobName、反射摘要 |
| Data.Messages | 生成 | 导入器警告 |
| Data.ModelNodes | 生成 | 按父子顺序排列的模型节点：名称、父索引、局部矩阵、网格 Key 与按子网格槽位排列的材质 Key |
| ImportStatus | 生成 | 源文件自身的导入状态：Inputs、SettingsHash、ContentHash；不记录依赖内容 |

**重新导入只重建生成部分，`Settings` 原样保留**——这是伴生文件与纯缓存的根本区别。

#### ImportStatus 与 Ctrl+R 条件重导

`Dependencies` 只描述引用关系。同一路径的图片被替换，或被引用的 Shader 增加注释，都不改变该列表。依赖内容改变仍需使对象缓存过期；重导结果若改变了资源对象字段，`Data` 仍按实际结果更新。

`ImportStatus` 由后台导入完成后写入，`Inputs` 只含源文件自身的 `Path`、`Length`、`Modified`、`ContentHash`，另存 `SettingsHash` 和源输入的聚合 `ContentHash`。文本的 `Length` 排除 CR、LF 字节；哈希只把 CRLF 折成 LF，保留换行位置及孤立 CR。二进制资源的长度与哈希覆盖全部原始字节。源内容指纹相同时沿用原 `Modified`，不因检出、复制或换行格式切换刷新时间。

后台对象缓存的完整输入校验记录保存在 `ResourceCache/Imported/<相对目录>/<源文件名>.import.status`，包含源文件及依赖的指纹和设置哈希，不复制资源内容、不进版本管理。其版本不符、内容指纹变化或对象缓存缺失时重新导入。路径、Blob 列表和设置按序数排序；伴生 JSON 相同时跳过写入。

主进程已加载对象的导入记录只保存在内存，与后台状态分开维护。后台重建 `.orbo` 不会把主进程对象标为已更新，也不会清空主进程记录。

- Refresh 没有主进程记录时重导；重导成功后只更新内存记录，不改写伴生文件。后续 Refresh 比较当前源文件、依赖与设置的内容指纹，内容相同即跳过。磁盘长度与时间戳只作为内存哈希缓存的读取提示；Refresh 会清空该缓存，重新校验实际内容。
- 这条路径**只服务 `Ctrl+R` 与 Project 面板右键菜单里的 `Refresh`**（两者走同一个入口）。同一菜单里的 `Reimport` / `Reimport All` 保持无条件强制重导，语义分明；顶部 Project 菜单只留新建与载入，不挂这三项。
- 判定结果会打进 Console：一条汇总（`Refresh: N of M loaded source(s) need reimporting.`）加逐源的原因（`no recorded runtime import` / `content changed` / `import settings changed`），最多列 50 条以免冲掉日志窗口。
- 命中键**不含导入器版本**，这是有意的：改了引擎的导入逻辑（例如 `Import_IMG` 的默认色空间推断）之后，未变资源不会因 Ctrl+R 而重导，需要靠递增 `CurrentVersion` 或显式 Reimport。Unity 把 importer ID 与 importer version 纳入判定，我们这一维暂缺。
- 命中键里的**目标平台是占位**（`EditorAssetCache.PlatformMatches` 恒为真，带 TODO）：多平台导入落地后再把平台写入导入记录并参与比对。
- 已知缺口：未被导入器列出的依赖不参与判定；新增 `#include` 文件要等后台重扫进 `Dependencies` 后才能参与主进程指纹。

指纹用 SHA-256（BCL 自带，`NuGet.config` 已清空包源），输入集合按相对路径序数排序后以长度前缀编码，避免拼接歧义；大文件流式分块读取，不整块进内存。

`Settings` 可以缺席：没有伴生文件、或伴生文件由更早版本写下时都按空处理，行为与"全部自动推断"一致，因此老项目升级后画面不变。

移除 SourceId、未知扩展字段保留、Engine 构建时间戳和 Generation。源路径及依赖路径不记录本机绝对路径；依赖校验比较内容指纹，不使用本机引擎安装路径或构建文件时间戳。更换路径后按新 Content 根解释相对路径。跨 Content 的依赖以相对路径表示，无法相对表示的路径拒绝提交。旧版本清单重新生成，Settings 独立于清单结构读取并保留。

**原生侧不解析 JSON。** 编辑器把 `Settings` 导出成 `源Key\t设置名\t值` 行表，通过两条通道交给原生：后台工作进程走命令行指向的设置文件，进程内 Reimport 走桥参数。原生用一个共用解析器按源文件 Key 逐行取用，因此整目录重导也能各自带上自己的设置。

资源身份仍使用既有 `sourceKey//Type/SubId`，没有引入 GUID 引用协议。清单损坏、版本不符、源文件/依赖变化、任一对象缓存缺失时重新导入。修改导入格式或规则时需递增 Version；普通引擎重新编译不再仅凭产物时间戳触发全量重新导入，也可显式 Reimport。

## 原生导入与后台调度

Font、Texture2D、Mesh 和模型 Material 的导入对象统一通过 `AssetPipeline::GetImportedObjectKey` 生成 `<sourceKey>//<实际原生类型>/<对象名>`。单对象使用 Main，模型多对象使用已有名字或编号。字体和独立图片的源文件路径不再作为运行时对象 Key 注册。

| 源文件 | 导入对象 Key |
| --- | --- |
| `Builtin/Fonts/Default.ttf` | `Builtin/Fonts/Default.ttf//Font/Main` |
| `Images/example.png` | `Images/example.png//Texture2D/Main` |
| `Meshes/example.obj` | `Meshes/example.obj//Mesh/Main` |

ImportSettings 仍按源文件 Key 查询；`.orbo` 按完整对象 Key 写入和计算文件名。编辑器自有资源文件的主对象继续使用自身的 Key。

新增 `AssetInspection`（Editor 原生辅助类）收集真实 AssetCollection，按 canonical Key 去重，只把当前源文件拥有的内部对象列为子项。外部依赖对象不冒充内部对象，但会写入本次缓存依赖图。重新导入直接取新集合，避免旧运行时对象残留导致清单出现已删除的子资源。

编辑器增加 `--inspect-asset <ContentRoot> <sourceKey> <outputDirectory>` 工作进程入口，在创建窗口、Application 和 GPU 之前执行。大文件完整解析发生在独立进程中，不在 UI 线程创建大网格或纹理，也不并发修改主进程 Object 注册表。工作进程同时生成对象 `.orbo`、字段摘要和依赖清单。

`EditorAssetCache` 串行调度一个导入进程。Project 扫描只登记源文件；已有有效缓存 `.resinfo` 直接读取，未索引或过期文件进入后台队列。独立进程数量不随目录中的文件数增长。项目切换和编辑器退出终止自身启动的工作进程。导入期间源文件变化时拒绝提交不一致的结果。

当前支持引擎已有 OBJ、glTF/GLB、图片、OrbShader、GLSL 源对和内部 `.orbo`。**本次没有实现 FBX 导入器**；以后接入时可复用清单/后台进程/缓存机制。后台导入仍承担完整解析的 CPU 和内存成本，不等同于零成本预览。

## Inspector 展示

Inspector 承担三种检视用途，由**选择来源**决定走哪一支：

| 用途 | 选择来源 | 数据来源 | 可编辑 |
| --- | --- | --- | --- |
| Ens 及其 Components | 场景视图、EnsView | 原生组件快照 | 是，走 PropertyDocument，带多选与 Undo/Redo |
| 引擎内部资源 | Project 选 `.orbmat` | 内存中的 Material 对象 | 是，写回 `.orbmat` 文本源 |
| | Project 选 `.orbo` / `.orbshader` / `.glsl` | 后台导入清单 | 只读 |
| 外部原始资源 | Project 选 `.png` / `.jpg` / `.obj` / `.gltf` / `.xml` 等 | 后台导入清单 | 导入设置可编辑，对象清单只读 |

**Ens 优先于资源**：两者同时有选择时清掉资源选择，回到组件检查。选择与焦点所有权的规则见 [Project 选择、展开与引用](#project-选择展开与引用)。

两处与直觉不符、实现上是刻意的：

- **`.world` 没有导入清单**。它是场景而不是导入资源：双击或 Open 由编辑器自己装载（`EditorWorldActions.RequestOpen`），Ens 与组件通过场景视图和 EnsView 编辑。选中它时 Inspector 给出的是**世界级设置**——启动场景标记，以及环境设置（天空盒、环境光）。后者挂在 World 上而不是任何 Ens 上，EnsView 够不着，因此与 RenderingPanel **共用同一份编辑实现**（`EditorEnvironmentSettings`），两处都能改。环境设置只有当前打开的世界能改：编辑器操作的是内存里的 World 对象，改不了磁盘上别的场景文件；启动场景标记则任何世界都能设，它记在 `.oeproj` 上。
- **`.orbmat` 不走资源清单那一条路**，而是独立的材质编辑器：它按绑定的 Shader 展开颜色槽、浮点槽、纹理槽，改完写回文本源再用 `ResourceManager::Reimport` 就地更新对象。其余内部资源与外部原始资源共用同一条只读清单路径。

资源卡片按完整 Key 分配独立 UI 身份，可折叠，显示类型、Key 与反射值。对于没有通用反射 getter 的已知资源容器，补充专用只读摘要：

- Mesh：顶点、UV、法线、切线、索引数量，以及子网格名称和区间。
- Texture2D：尺寸、通道、格式及像素字节数。
- Material：Shader 引用、纹理槽、颜色槽、浮点槽。
- Shader：反射字段、Pass 名称和槽数量。
- TextResource：正文的字节数与行数，正文本身留在源文件里看。

长文本截断到有限长度并标记，避免直接展示整块几何或像素数据。Inspector 使用导入快照，不为浏览清单而把这些对象加载到主进程。导入错误、源文件消失、空资源文件及已删除子资源都有显式状态；导入中或失败时不展示旧对象，禁止拖拽未完成的产物。

### 导入设置编辑

源文件 Inspector 分两节，用可折叠小标题（`TreeNode`，与 Project Settings 面板同一做法）：

```
brick.png                    ← 文件名
▼ Import Settings            ← 有设置时才有这一节
    Color Space  [Auto ▾]
    [Apply] [Revert]
▼ Objects                    ← 导入产出的内部对象清单，只读
    ▸ Texture2D
```

| 源类型 | 设置 | 取值 |
| --- | --- | --- |
| 图片（png/jpg/jpeg/tga/bmp） | Color Space | `Auto (by usage)` / `sRGB (color)` / `Linear (data)` |
| 模型（obj/gltf/glb） | Scale | 正浮点，默认 1 |
| | Up Axis | `Y-up (engine native)` / `Z-up -> Y-up` |
| | Import Materials | 复选框，默认勾选；关闭时只导入网格，同时跳过材质和纹理 |
| | Import Textures | 复选框，默认勾选；关闭时保留网格与材质参数，跳过纹理导入 |

**Import Textures** 共用于 OBJ、glTF 与 GLB。关闭时在 `Settings` 中保存 `"importTextures": "false"`，开启时删除该键，缺键默认开启。重新导入会清除材质原有的纹理引用和对应资源依赖，再按当前开关决定是否读取纹理；后台产物与清单按当前导入集合重建。glTF/GLB 内同一图片按颜色空间分别生成 Texture2D，首次使用保留原资源 Key，另一颜色空间追加 `_SRGB` 或 `_Linear` 后缀。

**Import Materials** 同样共用于 OBJ、glTF 与 GLB。关闭时保存 `"importMaterials": "false"`，开启时删除该键，缺键默认开启。关闭材质时禁用 Import Textures 控件并保留其草稿和保存值；重新开启材质后沿用原纹理设置。关闭后新的导入清单不生成模型材质或纹理，网格及其子网格区间继续保留。

**Scale 与 Up Axis** 在网格成形之后统一施加于该源产出的全部网格：缩放是统一倍率，因此不改变法线方向；Z-up 转换是纯旋转 `(x,y,z) → (x,z,-y)`，法线和切线同样变换。模型节点的局部矩阵按同一轴变换共轭转换，节点位移也乘以统一倍率，保持层级组合后的几何结果一致。引擎是 Y-up（XZ 为地面平面）。

**改动流程**：草稿累积 → Apply → 写回伴生文件的 `Settings` → 让缓存失效 → 进程内 Reimport 让运行时对象立刻与新设置一致 → 后台工作进程重新生成清单与产物。Inspector 里的值、运行时对象、磁盘产物三者同步更新。

数值控件拖动期间会连续返回值，因此设置走**草稿 + Apply** 而不是即时生效，避免每帧触发一次模型重新导入。

`Auto` / 空值 是**删除该键**而不是写入一个特殊值：缺键即"按文件原样 / 按语义推断"，这样新增设置项不需要迁移老文件。

设置按**源文件**生效。复合资源里的子贴图颜色空间仍按用途推断，一个 `.gltf` 内的不同贴图可分别使用 sRGB 或 Linear；Import Materials 与 Import Textures 控制是否导入。

播放中禁止编辑（与资源重命名、保存材质同一条件）。

设置写入不会误标记 World dirty，也不进入组件 Undo；Ens 和组件仍通过原有 PropertyDocument 编辑。对象清单部分仍然只读。

### 模型拖入 ScenePanel 或 EnsPanel

Project 面板的 `.obj`、`.gltf`、`.glb` 源文件可投放到 ScenePanel。导入完成后，从缓存中的 ModelNodes 创建一个以文件名命名的根 Ens；根位于鼠标投放位置。glTF/GLB 使用默认场景（没有默认场景时选第一个场景）的节点层级、本地变换和共享网格；没有场景时使用无父节点的节点，没有任何节点时按网格生成节点。OBJ 在根下创建一个网格节点，按 `usemtl` 对应子网格材质槽。

同一模型源文件也可投放到 EnsPanel：节点中部创建为该节点的子级，上下边缘按指示线插入同级，空白处放到 World 根。层级投放的模型根保留零局部位置；撤销和重做记录目标父级及同级插入位置。SceneView 绘制叠加层后恢复视口图像的 ImGui 条目状态，投放命中范围继续覆盖整个视口图像。

每个带网格的节点挂载 StaticMeshRenderer，按槽位引用本次导入的材质。材质为空的槽使用 `Builtin/Materials/default.orbmat`，关闭材质导入时也能显示白色网格；导入材质没有指定 Shader 时绑定内置 `Builtin/Shaders/blinn_phong.orbshader`。默认材质是引擎资源，不写入模型自己的材质清单。

拖拽复用缓存对象，不在 UI 里重新解析模型。导入未完成时提示等待完成后重试；创建失败时回滚全部新节点。创建成功后选择根 Ens，整个模型记录为一次撤销事务；重做按对象快照恢复原稳定身份。Play 期间禁用模型投放，不建立模型预制体链接。

## Project 选择、展开与引用

除代码、World 和纯文本文件外，所有原始文件均显示展开箭头，不受导入器支持情况或内部对象数量影响；展开后显示权威子资源列表，空文件或尚无导入器的格式显示无对象状态。网格中的可展开原始文件瓦片也显示展开箭头，点击后在网格下方显示该文件的资源区，复用同一子项绘制逻辑。源文件选择检查全部对象，子项选择检查单个对象。原始文件使用独立的 `EditorSourceIconCatalog` 映射和 `Resources/Icons/SourceFiles/` 图标目录，mtl、fbx、png、jpg、obj 等非代码文件统一使用纸箱 `Fallback.png`。Shader 代码（orbshader、glsl、vert、frag）、C#（cs）和 C/C++ 代码及头文件（cpp、cc、cxx、c、h、hpp、inl）不提供展开按钮，使用复制到 SourceFiles 的 ShaderFile、CSharpScript、CppScript 图标；导入对象按真实类型使用原有 Mesh、Material、Texture 等图标。World 使用透明玻璃球、土壤与树的专用图标；txt、json、xml、md、csv、ini、yaml 等纯文本和配置文件使用文本图标。World、代码和纯文本文件在列表与网格中都没有展开按钮。目录使用正面闭合的扁平文件夹图标。

每个图标均提供透明 PNG 的 32×32 与 256×256 两档：`Icons/<名称>.png` 为 32，`Icons/256/<名称>.png` 为 256，SourceFiles 分类在两档中保持一致。按实际显示尺寸乘以 framebuffer 像素密度选择档位，超过 32 像素使用 256，并生成 mipmap 改善中间缩放尺寸的采样。图标按需上传到 GPU。

资源检查选择独立于 Delete/Rename 所用的焦点所有权：从 Project 点击 Inspector 后目标不丢失；选择 Ens 后回到组件检查。文件操作仍针对父文件，不提供对子资源独立删除或重命名的伪操作。

沿用 kind=2 拖拽协议，原始文件 payload 是源文件 Key，子项 payload 为完整对象 Key。拖入 Inspector ObjectField 时先筛选类型匹配的内部对象：一个候选直接赋值，多个候选弹出选择器，零候选拒绝；不会一次性把全部对象赋给单个字段。ObjectField 从真实清单收集类型候选，保留声明类型校验、场景引用限制和整文件多候选选择器。选中资源后通过原生 `LoadCachedAsset` 读取指定 `.orbo` 及其缓存依赖，不重新解析整个源模型。原生桥限制读取范围，加载失败回滚本次新建对象。

这条缓存加载入口服务编辑器 ObjectField；普通 `Resources.Load` 和场景启动的既有资源加载契约没有整体替换。本次不顺带重写整个运行时资源系统。

## 源文件与缓存操作

- Content 中维护源文件与它们的**伴生 `.resinfo`**；后者承载导入设置，随资源一起进版本管理。
- 重命名/移动继续维护既有路径引用；伴生文件与源文件同址，随源文件一起移动，扫描清除旧位置的对象产物。
- Duplicate、外部 Import 复制源文件与伴生文件，但**不复制对象产物**——产物由新位置重新导入生成。
- 删除仍将源文件送入回收站；伴生文件与对象产物由扫描清理。
- 伴生文件与源文件同处 `Content/`，但**不参与 Project 面板的列表与操作**：`IsGeneratedPath` 把它们连同 `ResourceCache` 一起排除，文件监听、目录枚举、拖拽与引用重写都跳过。
- 项目 `.gitignore` 排除 ResourceCache 与 `*.resinfo.tmp`；**`*.resinfo` 必须纳入版本管理**。

## Project Settings 与 Layer 控件

统一面板采用 `ProjectSettingsPanel`，标题 **Project Settings**，通过既有面板发现机制注册。只加入 Layers 和 Physics。

Layers 提供 32 个共享名称，默认 Layer 0 为 Default。Renderer 下拉写入 `1u << index`，而不是下拉索引；Camera mask 提供 Nothing、Everything 和逐层勾选。原有零值或多位单层值显示 Custom/十六进制，直到用户明确选择才改变。Collider、HeightField、CharacterController 的相关字段使用同一套命名控件。提交仍走 PropertyDocument，保留多选和撤销。

Physics 按选中层显示碰撞矩阵的一行，改变一格同时更新对称列，包含自身碰撞。Apply 原子保存、Revert 放弃草稿、Restore Defaults 修改草稿；Play 期间禁用修改。配置为项目根 `GameSettings.ini` 的 `[Layers]` 分块，随 Player 镜像到包根：32 行，每行 `<序号>TAB<层名>TAB<八位十六进制允许掩码>`；文件在但缺这一段时按默认值补写。层名与碰撞矩阵是项目级设置，因此不放在会被 cook 的内容根里。

PhysX 有效 mask 为组件原 collisionMask 与矩阵允许行的交集；Collider、HeightField、CharacterController、Trigger 和 CCT 配对遵循矩阵。普通 Raycast/Sweep/Overlap 没有源层，仍按传入 layerMask 查询。旧多位层取所属行并集。配置参与形状 hash，变化后在物理同步时生效。缺少分块时按默认值补写并保持全允许矩阵；非法或非对称配置报错并使用默认值。

项目版本递增至 11，需更新 SDK、重建模块和重新打包；已有组件位值不迁移。

## TODO

- [x] 阅读约定并确认资源 Key、内部格式和 Layer 位值语义。
- [x] 编写落地设计，并按用户最终决策更新 metadata / resinfo 布局。
- [x] 以真实 AssetCollection 为权威清单，移除 C# 子资源猜测。
- [x] 独立导入进程、串行队列、依赖失效和原位置重新导入。
- [x] 原始资源全对象 Inspector、单子对象/内部文件 Inspector、复杂字段摘要。
- [x] Project 列表/网格子资源展开、选择、精确拖拽。
- [x] `.resinfo` 与对象缓存同目录，相对路径清单，不保留导入代次。
- [x] `.orbo` 导入缓存与按对象加载，Player 暂存目录分离。
- [x] Renderer 单层下拉、Camera 位掩码、项目 Layer 命名与碰撞矩阵。
- [x] 托管编辑器编译通过。
- [x] 最终原生 Editor 编译并收尾。

## 验证边界

Core Debug/x64、原生 Editor Debug/x64 和托管 Editor 编译通过。隔离检查使用真实导入工作进程，覆盖镜像目录、相对路径、无 `.result` 落盘、重导入清理、同目录资源隔离、损坏/缺失清单重建、失败不复用旧产物及删除源文件后的缓存清理。UTF-8 Shader 文件名通过管道检查；图片和模型的带前缀对象可原生读回，错误前缀被拒绝。现有 `Build/TestCookedAssetRoundTrip.ps1` 通过，覆盖 Player 产物和跨文件引用。没有执行编辑器界面交互回归。

定向检查另发现现有图片解码器不支持中文源文件名；该问题与缓存目录重构无关，本次未修改图片解码器。
