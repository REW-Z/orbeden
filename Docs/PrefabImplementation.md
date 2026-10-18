# Prefab 实施清单

本文记录已确认的行为和实施接口。临时编辑场景采用纯内存快照。当前阶段只编译和静态检查，不运行功能测试。

## 已确定的行为

- 采用增量扩展，保留现有场景、子树快照和复制流程。
- 源 `.world` 保存完整实例，另附编辑绑定信息。关联数据由 Editor 管理，不放进运行时 `Ens`、`Component` 或 `World`。
- 实例仅允许修改根节点的局部位置、旋转和缩放。更新时保留这三个字段、实例所在父级和同级位置，其余内容取自源 Prefab。
- 绑定实例的内部节点禁止改名、修改激活或静态状态、修改组件、增删节点和调整内部层级。整棵实例可以移动、复制、删除。
- EnsView 中所有绑定节点的文字显示浅绿色 `#A8D8A0`；未激活时保留色相并降低亮度。解绑后恢复主题默认状态。
- 不支持嵌套绑定。禁止将节点、模型或另一个实例插入绑定实例内部；禁止在 Prefab 编辑模式投放其他 Prefab。
- Unpack 清除整棵实例的绑定，保留内容、身份和场景引用，支持撤销与重做。
- 默认在打开场景和 World Cook 时更新实例。强制更新按钮按当前源 Prefab 扫描项目内全部 `.world`。
- 强制更新当前场景时使用内存快照，不擅自保存其未保存修改；更新后重载快照并清空撤销历史。其他引用场景逐文件安全写入，报告成功和失败，不承诺跨文件事务。
- 源文件缺失或损坏时，编辑器保留已有实例和绑定并显示错误；相关 World Cook 失败，不静默解绑或发布旧实例。
- 原有节点和组件维持稳定身份，源中新增对象获得新的实例身份，源中删除的对象被移除，指向被删除对象的场景引用清空。
- 通过项目内现有资源移动入口移动 Prefab 时同步绑定路径；在编辑器外移动导致来源缺失时按缺失来源处理。

## 编辑模式

- 双击 `.prefab` 进入独立编辑模式；入口复用现有场景未保存修改处理。
- 临时 World 仅存在于内存中，退出后销毁；不修改启动场景和最后编辑场景，不参与内容扫描和打包。
- 临时 World 载入源内容时保留源节点和组件身份。已存在的源根节点不可删除，不允许出现第二个顶层根节点；根节点之下可正常编辑内容。
- 在场景面板显示当前源文件及一行等宽的 Apply、Return、Sync 按钮。Apply 写回并更新原场景；Return 丢弃最近一次保存后的未保存修改，保留已提交内容并返回原场景；Sync 更新引用场景。
- 源文件写回失败时保留临时编辑现场，不丢弃内容。普通场景切换先完成退出流程；关闭窗口时写回失败会取消关闭并保留现场。
- 场景切换清空撤销历史；临时编辑期间不进入 Play 或执行 Player 构建。

## 文件格式

普通 Ens、Component、Field、RenderSettings 的格式保持不变。编辑关联放在 World 根下的独立区块：

```xml
<PrefabBindings version="1">
    <Instance source="Props/Door.prefab" root="world://ens/instance-root">
        <Object source="world://ens/source-root" instance="world://ens/instance-root" />
        <Object source="world://component/source-component" instance="world://component/instance-component" />
    </Instance>
</PrefabBindings>
```

示例身份只用于展示映射含义，实际身份沿用引擎已有格式。Object 映射包含实体和组件，标识字段不使用 `stableId`，也不复用场景的 Ens、Component、Field 标签。

没有绑定实例时不写空区块。无绑定的场景在 Cook 中直接按字节复制，保持 Editor 源文件与 Runtime 产物完全一致。有绑定时先更新普通实例，再移除关联区块，输出同一种 XML 格式。

复制和撤销使用的子树快照可以附带相同的关联区块；传给现有原生恢复入口前移除该区块，恢复成功后由 Editor 接收并映射关联。

## 新增托管文件和类型

目录统一为 `OrbedenEditor/Managed/Orbeden.Editor/`。

| 文件 | 类型 | 数据或职责 |
|---|---|---|
| `EditorPrefabBinding.cs` | `internal sealed class EditorPrefabBinding` | `string AssetKey`、`string RootKey`、`Dictionary<string, string> ObjectKeys`，分别记录来源、实例根和源对象到实例对象的身份映射 |
| `EditorPrefabDocuments.cs` | `internal static class EditorPrefabDocuments` | XML 关联读写、来源校验和离线更新，不持有活动场景 |
| `EditorPrefabSession.cs` | `internal static class EditorPrefabSession` | 当前场景关联、临时编辑模式、只读策略、解绑和更新操作 |
| `EditorPrefabSession.cs` | `internal enum EditorPrefabEditOperation : byte` | `RootTransform`、`Header`、`Structure`、`Move`、`Delete`、`Copy` |
| `EditorPrefabSession.cs` | `internal enum EditorWorldFileOperation : byte` | `PrepareOpen`、`CommitOpen`、`Save`、`Cook` |
| `EditorPrefabSession.cs` | `internal enum EditorWorldSnapshotOperation : byte` | `Capture`、`Restore`、`EnterTemporary`、`LeaveTemporary` |

`RootTransform` 只允许绑定根节点的局部位置、旋转和缩放。`Header` 覆盖名称、激活和静态状态。结构操作同时检查目标父级，不允许从其他入口绕过内部只读限制。内部同步和快照恢复走明确的事务路径。

## 新增托管函数

以下为完整的新增业务函数清单；短小且仅用一次的处理留在调用点，不另拆辅助函数。

### EditorPrefabDocuments

| 签名 | 返回与行为 |
|---|---|
| `ReadBindings(XElement root)` | 返回 `List<EditorPrefabBinding>`，读取并校验关联区块、重复身份和实例范围 |
| `WriteBindings(XElement root, IEnumerable<EditorPrefabBinding> bindings)` | 返回 `void`，替换关联区块，集合为空时删除区块 |
| `ReadSource(string contentRoot, string assetKey)` | 返回 `XElement`，校验内容根内的 Prefab 路径、文件和单根结构，读取源 Prefab 根元素 |
| `MapObjects(XElement source, XElement instance)` | 返回 `Dictionary<string, string>`，按对应的子树对象建立身份映射并校验类型与结构 |
| `Synchronize(XDocument world, string contentRoot, string? assetKey)` | 返回 `bool` 表示内容是否变化；更新全部关联或仅更新指定来源，保留根节点摆放和原有对象身份 |
| `WriteFile(string path, XDocument document)` | 返回 `void`，在同目录写入临时文件后替换目标，失败时抛出异常并保留原文件 |

### EditorPrefabSession

| 签名 | 返回与行为 |
|---|---|
| `ProcessWorldFile(EditorWorldFileOperation operation, string source, string target)` | 返回 `bool`；准备打开文件、提交关联、附加保存数据或生成 Cook 产物，按操作类型分派 |
| `FindBinding(Ens ens)` | 返回 `EditorPrefabBinding?`；查找对象所属的整棵绑定实例，无关联时返回空 |
| `CanModify(Ens ens, EditorPrefabEditOperation operation)` | 返回 `bool`；校验绑定只读规则和临时场景根节点约束 |
| `RegisterInstance(string assetKey, Ens root)` | 返回 `void`；为新实例记录来源和对象映射 |
| `CaptureBindings(string snapshot)` | 返回 `string`；为子树快照附加其包含的实例关联 |
| `PrepareSnapshot(string snapshot)` | 返回 `string`；移除快照中的关联区块，供原生恢复使用 |
| `RestoreBindings(string snapshot, Ens root, bool copy)` | 返回 `void`；恢复关联，复制模式下映射新身份 |
| `RemoveBindings(string snapshot)` | 返回 `void`；移除被成功删除子树包含的关联 |
| `Unpack(Ens ens)` | 返回 `bool`；解绑对象所属的整棵实例并记录撤销事务 |
| `BeginEdit(string assetKey)` | 返回 `bool`；准备源文件和临时 World，成功后进入编辑模式 |
| `EndEdit(bool save = true)` | 返回 `bool`；按参数决定是否写回源文件，再返回原场景，失败时保留编辑模式 |
| `RefreshInstances(string assetKey, bool allScenes)` | 返回 `bool`；更新当前场景，按参数决定是否更新其他引用场景，并报告失败 |
| `DrawControls(Ens ens)` | 返回 `void`；在 Inspector 显示来源、编辑和 Unpack 操作 |
| `DrawEditToolbar()` | 返回 `void`；在场景面板显示编辑模式及一行等宽的 Apply、Return、Sync 按钮 |
| `RemapAssetKeys(string oldKey, string newKey, bool prefix)` | 返回 `void`；同步当前关联中的资源路径 |
| `Clear()` | 返回 `void`；清理项目切换或关闭后的会话状态 |

### 内存场景补充接口

- `WorldSerializer.CaptureWorld(const World&)` 返回完整普通场景 XML。
- `Application.LoadWorldDocument(const WorldDocument&)` 返回成功标记，复用世界准备、物理重置和脚本会话切换流程。
- `EditorAssetsNative.CaptureWorldSnapshot()` 返回当前普通场景 XML。
- 快照 ABI 通过输出缓冲传递内存文本；捕获可以查询所需字节数。

### 现有桥接类型中的新增入口

| 所属类型与签名 | 返回与行为 |
|---|---|
| `EditorAssetsNative.RemapObjectIdentities(string xml, IReadOnlyDictionary<string, string> identities, bool clearExternal)` | 返回 `string`；调用现有原生文档引用重映射逻辑，统一处理对象引用、数组和事件引用 |
| `EditorAssetsNative.ChangeWorldSnapshot(EditorWorldSnapshotOperation operation, string xml)` | 返回 `bool`；捕获或恢复 World 快照、进入或退出临时 World |
| `EditorRuntime.ProcessWorldFile(byte operation, byte* source, int sourceLength, byte* target, int targetLength, byte* error, int errorCapacity)` | 返回 `byte`；原生调用的 UTF-8 文件处理桥，捕获异常并写回错误 |
| `EditorRuntime.CanModifyPrefab(EnsId ens, byte operation)` | 返回 `byte`；供原生编辑操作校验只读策略 |

## 原生侧接口

| 文件与新增接口 | 返回与行为 |
|---|---|
| `OrbedenCore/Src/Runtime/WorldSerializer.h/.cpp`：`RemapObjectIdentities(const std::string& xml, const List<std::pair<std::string, std::string>>& identities, bool clearExternal, std::string& error)` | 返回 `std::string`；只扩展通用文档重映射能力，不加入绑定管理；失败返回空字符串并填写错误 |
| `OrbedenEditor/Src/Editor/EditorProject.h/.cpp`：`SetWorldFileProcessor(EditorWorldFileProcessor processor)` | 返回 `void`；注册场景文件处理回调 |
| 同上：`CaptureWorldSnapshot()` | 返回 `std::string`；捕获当前普通场景 XML，不修改项目路径和脏标记 |
| 同上：`RestoreWorldSnapshot(const std::string& xml)` | 返回 `bool`；通过已有准备与提交流程替换当前场景，不改启动场景或最后编辑场景 |
| 同上：`OpenTemporaryWorld(const std::string& xml)` | 返回 `bool`；通过内存文档进入临时场景，保留原场景快照和身份 |
| 同上：`CloseTemporaryWorld()` | 返回 `bool`；成功恢复原场景后销毁临时快照 |
| 同上：`IsTemporaryWorld() const` | 返回 `bool`；供 Play、构建和界面识别临时模式 |
| `OrbedenEditor/Src/Editor/ManagedEditorBridge.h/.cpp`：`ProcessWorldFile(EditorWorldFileOperation operation, const std::string& source, const std::string& target, std::string& error)` | 返回 `bool`；调用托管文件处理入口 |
| 同上：`CanModifyPrefab(EnsId ens, uint8 operation)` | 返回 `bool`；调用托管只读策略 |

`EditorWorldFileOperation` 在原生侧使用相同枚举值；`EditorWorldFileProcessor` 是返回 `bool` 的函数回调，参数依次为操作类型、源路径、目标路径和错误字符串引用。

原生匿名命名空间增加两个 ABI 适配函数：`RemapManagedObjectIdentities` 接收 UTF-8 文档、身份映射、清理外部引用标记和输出缓冲，返回所需字节数，失败返回负值；`ChangeManagedWorldSnapshot` 接收操作类型和 UTF-8 XML；捕获操作返回文本字节数，其他操作返回成功标记。各输入字符串均配长度，各输出缓冲均配容量。

## 现有代码的接入修改

- `EditorProject` 的保存改为先写普通场景暂存文件，再经回调附加关联并提交；打开先准备更新后的文件，原生加载成功后再提交关联。失败不替换当前关联。
- `PlayerContentCooker.Cook` 增加 World 文件处理回调参数，取代场景的无条件复制；`EditorSystem` 传入 Cook 回调。
- `EditorSystem` 配置项目回调，处理临时模式下的保存、Play、构建、项目切换与关闭边界。
- `ManagedEditorBridge` 与 `EditorRuntime` 更新函数表和布局校验，保持两端一致。
- `EditorAssetsNative` 现有保存 Prefab、实例化、捕获和销毁子树入口接入关联处理。新建 Prefab 后原子树成为绑定实例；复制与撤销带回关联。
- `EditorWorldActions` 复用未保存场景确认流程进入 Prefab 模式；普通场景切换先完成退出。
- `ProjectPanel` 双击 `.prefab` 打开独立模式，关联路径重写接入现有资源移动流程。
- `InspectorPanel` 显示关联操作，绑定实例只开放根 Transform，关闭其他属性及组件结构编辑。
- `EnsPanel` 实施绑定颜色、Unpack 和层级操作限制；普通节点保持现有流程。
- `NativeEditorGUI.TreeNode` 增加可选参数 `bool prefabBound = false`，通过空闲选项位传递；原生 `EditorGuiTreeNode` 使用浅绿色绘制绑定标签。
- 场景手柄、组件自定义编辑器、属性写入和右键操作统一检查编辑策略；运行时脚本写入不受编辑器策略限制。
- 项目版本由 94 增为 95，更新构建与打包记录，新增使用文档；不手改生成绑定。

## 验证范围

编译 Windows x64 Debug 的 Core 和 Editor；静态检查 ABI 对齐、对象身份与引用重映射、普通场景复制分支、单根约束、无嵌套规则，以及新增代码、注释、文档的名称限制。不启动 Editor，不执行单元、集成、冒烟或回归功能测试，不将未运行的测试描述为通过。
