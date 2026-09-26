# Scene Gizmos 与 CustomEditor

SceneView 工具栏的 **Gizmos** 菜单控制总开关、方向光、碰撞体，以及是否显示全部碰撞体。默认显示所有活动方向光，碰撞体只显示选中对象。方向光采用金色太阳标记与箭头，箭头跟随实际 `direction` 字段；碰撞体采用绿色线框，选中时加亮，禁用组件压暗。辅助线绘制在场景叠加层，便于观察被模型遮挡的形状。

支持 BoxCollider、SphereCollider、CapsuleCollider、ConvexMeshCollider 和 TriangleMeshCollider。球与胶囊遵循 PhysX 的非均匀缩放约定，凸包使用实际烘焙后的凸包边界。网格边线缓存并随几何变化更新。变换手柄独立于 Gizmos 开关。

## 创建组件编辑器

把编辑器代码放在内容根内任意 `Editor` 文件夹，例如 `Content/Editor/MyComponentEditor.cs`。执行编辑器的 **Build Game C#**，系统会先构建游戏程序集，再构建旁边的 `<游戏程序集名>.Editor.dll` 并一起重载。`Editor` 文件夹中的 C# 文件不会进入游戏主程序集或 NativeAOT Player；这些源文件编译时定义 `ORBEDEN_EDITOR`。

编辑器继承 `OrbedenEditor.ComponentEditor`，用 `[CustomEditor(typeof(...))]` 绑定组件。参数可以是 C# 用户脚本，也可以是 C++ 的生成包装类（内建类型如 `Orbeden.BoxCollider`，用户原生类型写作 `Native.MyComponent`，构建时提供 Native 别名）。第二个参数为 `true` 时同时匹配派生组件，更具体的注册优先。

| 回调 | 调用时机 |
| --- | --- |
| `OnDrawInspector()` | 组件卡片展开时；替换默认字段区域，标题、启用开关和组件菜单仍保留 |
| `OnSceneGui()` | SceneView 绘制时，对选中组件调用；可通过 EditorGUI 在视口左上角绘制控件 |
| `OnDrawGizmos()` | SceneView 绘制且 Gizmos 总开关打开时，对活动对象和选中对象调用；不要求 Inspector 可见 |

`Target` 提供 ObjectId、EnsId、Ens、TypeName 和 IsManaged。`Targets` 包含 Inspector 的多选目标；场景回调按单个组件派发。`IsSelected` 可用于只绘制选中状态的辅助线。编辑器实例会被复用，在目标销毁或程序集重载时释放。

字段使用 `Properties` / `FindProperty()` 读取，使用 `SetValue()` 暂存写入。回调完成后统一提交，支持多选、类型校验、失败回滚、WorldDirty 和撤销/重做。`DrawProperty(name)` 绘制指定字段的内置控件，`DrawDefaultInspector()` 插入默认 Inspector。**不要在每次绘制时调用 `Properties.Update()`，它会清掉尚未提交的修改。**

`Target` 是编辑目标描述，不是运行中的 C# Script 实例；编辑模式不为了画 Inspector 而构造或启动用户脚本。通过属性文档编辑宿主字段，才能在编辑模式、运行模式和 Missing Script 数据保存流程中使用一致的路径。直接给原生包装属性赋值不会自动产生 CustomEditor 撤销事务。

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

编辑器构造失败或回调抛出异常会输出 Console。失败回调停用到下次程序集重载，Inspector 回退到默认界面，其他组件编辑器继续工作。
