# 资源 Inspector、复合资源展开与项目 Layer 设置

## 本次目标和最终决策

主线是资源 Inspector 和原始复合资源展开；Project Settings 只补充本次字段编辑所需的 Layer 名称和物理碰撞矩阵。

- 选择原始文件：Inspector 展示该次导入产生的全部内部资源对象。
- 选择 Project 子资源：Inspector 只展示该对象；子项可拖入类型匹配的 ObjectField。
- 选择内部 `.orbo`：展示其中的单个对象。
- 现有 Ens、组件、多选属性事务和 Undo/Redo 保持原流程。
- **metadata 与子资源清单合并为源文件旁的 `.resinfo`**；它随源文件维护，纳入版本管理。`.orbo` 是可重建缓存。
- 扩展名只用于识别导入器。资源数量、Key、类型和字段来自原生 AssetPipeline 实际导入结果，不在 C# 重复解析 OBJ/glTF 猜测。
- 资源检查只读。当前没有独立资源编辑/导入覆盖保存协议，不提供重新导入后丢失的假保存。

## 文件布局与职责

```text
Project/
  Content/
    Models/robot.gltf
    Models/robot.gltf.resinfo
    ProjectSettings.layers
  ResourceCache/
    Imported/<源文件Key的FNV-1a哈希>/<生成代次>/
      <对象Key哈希>.orbo
      inspection.result
    Player/
      <对象Key哈希>.orbo
      cooked.index
      ...world
```

`inspection.result` 是工作进程的临时交换结果，不是资源元数据。生成代次隔离新旧产物，成功后才原子替换伴随 `.resinfo`，失败不破坏上次成功结果。历史生成代次目前保留，整个 ResourceCache 可清理重建；清理时不删除 Content 中的 `.resinfo`。Player cook 只清理 `ResourceCache/Player`，不清理 Imported。

### `.resinfo` 的数据分区

采用 UTF-8 JSON，当前 Version=1。

| 字段 | 职责 | 重新导入时 |
| --- | --- | --- |
| SourceId | 稳定的源文件元数据身份 | 保留；Duplicate 创建新身份 |
| ImportSettings | 用户导入设置的扩展容器 | 原样保留；本次不新增具体导入选项或设置编辑器 |
| 未知顶层字段 | 后续元数据扩展 | 使用 JsonExtensionData 保留 |
| Source / Engine | 本次源路径与编辑器/Core 构建标记 | 更新 |
| Generation | 本次成功导入对应的缓存代次 | 更新 |
| Dependencies | 实际导入依赖的路径、长度、修改时间 | 更新 |
| Data.Objects | 完整 Key、原生类型、BlobName、反射摘要 | 更新 |
| Data.Messages | 导入器警告 | 更新 |

SourceId 当前用于元数据身份，**没有替换引擎已有的路径 Key 引用协议**。资源运行时身份继续使用 `sourceKey//Type/SubId`；glTF 的索引等仍由已有导入器决定。外部项目复制后，生成路径/构建标记失效会触发重建，SourceId 和用户元数据仍保留。

字段缺损、未知版本或无法解析的元数据明确报错，不静默覆盖。源文件或依赖发生变化、对象产物缺失、引擎构建变化时重新导入；时间戳和长度用于失效判断，不声称进行了全文件内容哈希验证。

## 原生导入与后台调度

新增 `AssetInspection`（Editor 原生辅助类）收集真实 AssetCollection，按 canonical Key 去重，只把当前源文件拥有的内部对象列为子项。外部依赖对象不冒充内部对象，但会写入本次缓存依赖图。重新导入直接取新集合，避免旧运行时对象残留导致清单出现已删除的子资源。

编辑器增加 `--inspect-asset <ContentRoot> <sourceKey> <outputDirectory>` 工作进程入口，在创建窗口、Application 和 GPU 之前执行。大文件完整解析发生在独立进程中，不在 UI 线程创建大网格或纹理，也不并发修改主进程 Object 注册表。工作进程同时生成对象 `.orbo`、字段摘要和依赖清单。

`EditorAssetCache` 串行调度一个导入进程。Project 扫描只登记源文件；已有有效 `.resinfo` 直接读取，未索引或过期文件进入后台队列。独立进程数量不随目录中的文件数增长。项目切换和编辑器退出终止自身启动的工作进程。导入期间源文件变化时拒绝提交不一致的结果。

当前支持引擎已有 OBJ、glTF/GLB、图片、OrbShader、GLSL 源对和内部 `.orbo`。**本次没有实现 FBX 导入器**；以后接入时可复用清单/后台进程/缓存机制。后台导入仍承担完整解析的 CPU 和内存成本，不等同于零成本预览。

## Inspector 展示

资源卡片按完整 Key 分配独立 UI 身份，可折叠，显示类型、Key 与反射值。对于没有通用反射 getter 的已知资源容器，补充专用只读摘要：

- Mesh：顶点、UV、法线、切线、索引数量，以及子网格名称和区间。
- Texture2D：尺寸、通道、格式及像素字节数。
- Material：Shader 引用、纹理槽、颜色槽、浮点槽。
- Shader：反射字段、Pass 名称和槽数量。

长文本截断到有限长度并标记，避免直接展示整块几何或像素数据。Inspector 使用导入快照，不为浏览清单而把这些对象加载到主进程。导入错误、源文件消失、空资源文件及已删除子资源都有显式状态；导入失败时旧清单标记过期，过期条目禁止拖拽。

资源修改不会误标记 World dirty，也不会进入组件 Undo；Ens 和组件仍通过原有 PropertyDocument 编辑。

## Project 选择、展开与引用

列表中的支持文件显示展开箭头；展开后显示权威子资源列表。网格保持现有瓦片布局，选中文件后在网格下方显示其可展开资源区，复用同一子项绘制逻辑。源文件选择检查全部对象，子项选择检查单个对象。

资源检查选择独立于 Delete/Rename 所用的焦点所有权：从 Project 点击 Inspector 后目标不丢失；选择 Ens 后回到组件检查。文件操作仍针对父文件，不提供对子资源独立删除或重命名的伪操作。

沿用 kind=2 拖拽协议，子项 payload 为完整 Key。ObjectField 从真实清单收集类型候选，保留声明类型校验、场景引用限制和整文件多候选选择器。选中资源后通过原生 `LoadCachedAsset` 读取指定 `.orbo` 及其缓存依赖，不重新解析整个源模型。原生桥限制读取范围，加载失败回滚本次新建对象。

这条缓存加载入口服务编辑器 ObjectField；普通 `Resources.Load` 和场景启动的既有资源加载契约没有整体替换。本次不顺带重写整个运行时资源系统。

## 元数据文件操作

- Project 隐藏 `.resinfo` 及其写入临时文件。
- 文件重命名/移动联动伴随文件；伴随文件移动失败时恢复源文件。目录移动天然包含元数据。
- Duplicate 复制设置与扩展字段，生成新 SourceId，清除原副本的生成清单；递归复制目录同样处理。
- Import 外部源文件时携带已有 `.resinfo` 并保留其身份，生成数据重建。
- 删除源文件时将源文件与伴随文件作为同一组送入回收站。
- 通过系统文件管理器操作时，应把源文件和 `.resinfo` 一起维护。

## Project Settings 与 Layer 控件

统一面板采用 `ProjectSettingsPanel`，标题 **Project Settings**，通过既有面板发现机制注册。只加入 Layers 和 Physics。

Layers 提供 32 个共享名称，默认 Layer 0 为 Default。Renderer 下拉写入 `1u << index`，而不是下拉索引；Camera mask 提供 Nothing、Everything 和逐层勾选。原有零值或多位单层值显示 Custom/十六进制，直到用户明确选择才改变。Collider、HeightField、CharacterController 的相关字段使用同一套命名控件。提交仍走 PropertyDocument，保留多选和撤销。

Physics 按选中层显示碰撞矩阵的一行，改变一格同时更新对称列，包含自身碰撞。Apply 原子保存、Revert 放弃草稿、Restore Defaults 修改草稿；Play 期间禁用修改。配置为 `Content/ProjectSettings.layers`，随 Player 复制：UTF-8 无 BOM，首行 OrbedenLayers1，随后 32 行为八位十六进制允许掩码、TAB、层名。

PhysX 有效 mask 为组件原 collisionMask 与矩阵允许行的交集；Collider、HeightField、CharacterController、Trigger 和 CCT 配对遵循矩阵。普通 Raycast/Sweep/Overlap 没有源层，仍按传入 layerMask 查询。旧多位层取所属行并集。配置参与形状 hash，变化后在物理同步时生效。无配置保持旧项目的全允许矩阵；非法或非对称配置报错并使用默认值。

项目版本递增至 10，需更新 SDK、重建模块和重新打包；已有组件位值不迁移。

## TODO

- [x] 阅读约定并确认资源 Key、内部格式和 Layer 位值语义。
- [x] 编写落地设计，并按用户最终决策更新 metadata / resinfo 布局。
- [x] 以真实 AssetCollection 为权威清单，移除 C# 子资源猜测。
- [x] 独立导入进程、串行队列、依赖失效和失败保留旧清单。
- [x] 原始资源全对象 Inspector、单子对象/内部文件 Inspector、复杂字段摘要。
- [x] Project 列表/网格子资源展开、选择、精确拖拽。
- [x] `.resinfo` 伴随源文件，保留元数据，文件操作联动。
- [x] `.orbo` 导入缓存与按对象加载，Player 暂存目录分离。
- [x] Renderer 单层下拉、Camera 位掩码、项目 Layer 命名与碰撞矩阵。
- [x] 托管编辑器编译通过。
- [x] 最终原生 Editor 编译并收尾。

## 验证边界

遵照用户最后要求，收尾只检查代码与编译，不新增或继续运行测试，不执行界面交互回归。Core Debug 已编译通过；PhysX 第三方库存在缺少 PDB 的链接警告。最终原生 Editor（含托管发布）Debug/x64 编译成功，退出码 0。
