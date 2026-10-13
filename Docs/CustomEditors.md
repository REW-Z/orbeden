# Scene Gizmos 与 CustomEditor

SceneView 工具栏的 **Gizmos** 菜单控制总开关、方向光、碰撞体，以及是否显示全部碰撞体。默认显示所有活动方向光，碰撞体只显示选中对象。方向光采用金色太阳标记与箭头，箭头跟随实际 `direction` 字段；碰撞体采用绿色线框，选中时加亮，禁用组件压暗。辅助线绘制在场景叠加层，便于观察被模型遮挡的形状。

支持 BoxCollider、SphereCollider、CapsuleCollider、ConvexMeshCollider 和 TriangleMeshCollider。球与胶囊遵循 PhysX 的非均匀缩放约定，凸包使用实际烘焙后的凸包边界。网格边线缓存并随几何变化更新。变换手柄独立于 Gizmos 开关。

## 创建组件编辑器

把编辑器代码放在内容根内任意 `Editor` 文件夹，例如 `Content/Editor/MyComponentEditor.cs`。执行编辑器的 **Build Game C#**，系统会先构建游戏程序集，再构建旁边的 `<游戏程序集名>.Editor.dll` 并一起重载。`Editor` 文件夹中的 C# 文件不会进入游戏主程序集或 NativeAOT Player；这些源文件编译时定义 `ORBEDEN_EDITOR`。

也可以把脚本和它的编辑器写在同一个 `.cs` 文件中，文件放在内容根内的普通脚本目录，例如 `Content/Scripts/Test.cs`。Editor/PIE 的构建入口显式传入 `OrbedenEditorBuild=true`，游戏程序集因此定义 `ORBEDEN_EDITOR` 并引用 `Orbeden.Editor`，脚本和编辑器使用同一份脚本类型；Inspector 会直接从游戏程序集中发现自定义编辑器。Player 打包显式传入 `OrbedenEditorBuild=false`，NativeAOT 编译也始终排除该符号和编辑器引用，条件编译块不会进入 Player。手动为 Editor/PIE 构建游戏脚本时使用 `dotnet build <项目>.csproj -c Debug /p:OrbedenEditorBuild=true`；普通 `dotnet build` 不启用同文件的编辑器代码。`OrbedenBuildEditorExtensions=false` 仍只关闭独立 Editor 程序集构建。

VS Code / Visual Studio 的设计时项目加载（`DesignTimeBuild=true`）默认启用 `OrbedenEditorBuild`，使同文件的 `#if ORBEDEN_EDITOR` 代码参与智能提示，并解析 SDK 中的 `Orbeden.Editor.dll`。无需在游戏 `.csproj` 中添加无条件 Editor 引用。显式设置 `OrbedenEditorBuild=false` 或进入 NativeAOT 构建时不启用；更新 SDK 后，IDE 仍显示旧条件编译状态时，重新加载项目或窗口。

```csharp
using Orbeden;
#if ORBEDEN_EDITOR
using OrbedenEditor;
#endif

public class Test : Script
{
    /// <summary>由运行时关联宿主。</summary>
    public Test(Ens ens) : base(ens) { }
}

#if ORBEDEN_EDITOR
[CustomEditor(typeof(Test), true)]
public sealed class TestEditor : ComponentEditor
{
    /// <summary>绘制脚本字段。</summary>
    public override void OnDrawInspector() => DrawDefaultInspector();
}
#endif
```

同文件写法的编辑器 `using`、类型声明和所有编辑器 API 调用都放进 `#if ORBEDEN_EDITOR` 块。不要把包含 Runtime 脚本定义的文件放进 `Editor` 文件夹，该目录仍只编译到独立 Editor 程序集。

编辑器继承 `OrbedenEditor.ComponentEditor`，用 `[CustomEditor(typeof(...))]` 绑定组件。参数可以是 C# 用户脚本，也可以是 C++ 的生成包装类（内建类型如 `Orbeden.BoxCollider`，用户原生类型写作 `Native.MyComponent`，构建时提供 Native 别名）。第二个参数为 `true` 时同时匹配派生组件，更具体的注册优先。

| 回调 | 调用时机 |
| --- | --- |
| `OnDrawInspector()` | 组件卡片展开时；替换默认字段区域，标题、启用开关和组件菜单仍保留 |
| `OnSceneGui()` | SceneView 绘制时，对选中组件调用；可通过 EditorGUI 在视口左上角绘制控件 |
| `OnDrawGizmos()` | SceneView 绘制且 Gizmos 总开关打开时，对活动对象和选中对象调用；不要求 Inspector 可见 |

`Target` 提供 ObjectId、EnsId、Ens、TypeName 和 IsManaged。`Targets` 包含 Inspector 的多选目标；场景回调按单个组件派发。`IsSelected` 可用于只绘制选中状态的辅助线。编辑器实例会被复用，在目标销毁或程序集重载时释放。

字段使用 `Properties` / `FindProperty()` 读取，使用 `SetValue()` 暂存写入。回调完成后统一提交，支持多选、类型校验、失败回滚、WorldDirty 和撤销/重做。`DrawProperty(name)` 绘制指定字段的内置控件，`DrawDefaultInspector()` 插入默认 Inspector。**不要在每次绘制时调用 `Properties.Update()`，它会清掉尚未提交的修改。**

`Target` 是编辑目标描述，不是运行中的 C# Script 实例；编辑模式不为了画 Inspector 而构造或启动用户脚本。通过属性文档编辑宿主字段，才能在编辑模式、运行模式和 Missing Script 数据保存流程中使用一致的路径。直接给原生包装属性赋值不会自动产生 CustomEditor 撤销事务。

**界面文本只用 ASCII。** 编辑器用 ImGui 自带的默认字体，字形范围只有 Latin-1（`0x20`–`0xFF`），没有中日韩字形：标签、按钮、状态文本里出现中文，运行时会被逐字渲染成 `?`——不报错，只是文字全变问号。注释仍然按规范写中文，中英混排只发生在代码里，不进 UI 字符串。`×`（U+00D7）在 Latin-1 内，可以照常用的关闭按钮那样使用。

## 示例：为 C# 脚本自定义三个回调

普通脚本放在 `Content/Scripts/DetectionZone.cs`：

```csharp
using Orbeden;

public sealed class DetectionZone : Script
{
    public float radius = 3.0f;

    /// <summary>由运行时关联宿主。</summary>
    public DetectionZone(Ens ens) : base(ens) { }
}
```

编辑器放在 `Content/Editor/DetectionZoneEditor.cs`：

```csharp
using Orbeden;
using OrbedenEditor;

[CustomEditor(typeof(DetectionZone))]
public sealed class DetectionZoneEditor : ComponentEditor
{
    /// <summary>复用默认字段并提供重置操作。</summary>
    public override void OnDrawInspector()
    {
        DrawDefaultInspector();
        if (EditorGUI.Button("Reset Radius"))
            SetValue("radius", InteropValue.From(3.0f));
    }

    /// <summary>在选中对象的场景视图提供快捷操作。</summary>
    public override void OnSceneGui()
    {
        EditorGUI.Label("Detection Zone");
        if (EditorGUI.Button("Set Radius to 5"))
            SetValue("radius", InteropValue.From(5.0f));
    }

    /// <summary>绘制世界空间范围，不改变组件数据。</summary>
    public override void OnDrawGizmos()
    {
        PropertyValue? property = FindProperty("radius");
        if (property == null || !property.Value.TryGet(out float radius)) return;
        vector3 center = Target.Ens.Transform.GetWorldPosition();
        Gizmos.WireSphere(center, radius,
            IsSelected ? new color(0.4f, 1.0f, 0.6f) : new color(0.4f, 1.0f, 0.6f, 0.35f));
    }
}
```

可用绘制入口包括 `Gizmos.Line`、`Gizmos.Label`、`Gizmos.WireSphere`、`Gizmos.WireCube`，坐标均为世界空间，尺寸均为世界单位。自定义 GUI 回调各有独立 ID 空间；同一回调内重复绘制同名控件时可用 `EditorGUI.PushId()` / `PopId()` 区分。所有 Begin/End 或 Push/Pop 必须配对，建议用 `try/finally`。

组件 Inspector 不常驻显示默认行为、重复参数解释等次要说明；必要的操作说明放到悬停提示中。缺失依赖、配置冲突、编辑受限等需要用户关注的信息统一调用 `EditorGUI.Warning(text)`，使用主题黄色并自动换行。字段标签、分组标题和数值读数保持普通文本。

编辑器构造失败或回调抛出异常会输出 Console。失败回调停用到下次程序集重载，Inspector 回退到默认界面，其他组件编辑器继续工作。


## 动画曲线与颜色渐变

`GUI.AnimationCurve(label, id, ref ParticleCurve, minimumValue, maximumValue)` 与 `GUI.ColorGradient(label, id, ref ParticleGradient)` 是 Editor / Player 共用的 C# 控件；返回 true 时提交属性事务。
`id` 在当前 GUI 作用域内唯一，编辑对象可使用 ObjectId 与字段路径组合；时间归一化到 0..1。
`minimumValue` / `maximumValue` 是这个字段允许的取值区间：视图不越出区间，编辑也夹在里面，数值轴只出现 1 / 2 / 5 × 10^n 的刻度；**Ctrl + 滚轮**以光标所在的值缩放值轴，不按 Ctrl 时滚轮照常滚动面板。
点击关键帧选中并拖动，双击空白添加；右键关键帧开菜单（Linear / Cubic / Constant、Add Key After、Delete Key），右键空白开视图菜单（Fit View）；Delete 键删除选中帧，Esc 取消。两端关键帧是曲线的结构（时间 0 与 1），不可删除。
曲线支持数值、Linear/Cubic/Constant 插值和切线斜率；三次插值的关键帧带两条定长的切线手柄，切到三次时按相邻帧自动补切线；拖动与手柄调整只预览，松开提交。
渐变关键帧通过 Color / Alpha 选择器编辑 RGB 和透明度，棋盘背景显示透明度，加帧与删帧都在色标右键菜单里；`GUI.ColorField` 可独立使用。
粒子 Size Over Lifetime 是初始尺寸的倍率，Color Over Lifetime 是初始颜色的倍率；Start Color 为白色时可直接观察渐变颜色。
