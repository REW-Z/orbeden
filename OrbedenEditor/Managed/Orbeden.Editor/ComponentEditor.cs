using Orbeden;

namespace OrbedenEditor;

/// <summary>统一的原生组件与托管脚本编辑目标；字段读写使用 Properties。</summary>
public readonly record struct ComponentEditorTarget(int ObjectId, EnsId EnsId, string TypeName, bool IsManaged)
{
    public Ens Ens => Orbeden.Ens.FromId(EnsId);
}

/// <summary>组件自定义编辑器，实例在目标和程序集有效期间复用。</summary>
public abstract class ComponentEditor
{
    public ComponentEditorTarget Target => Targets[0];
    public IReadOnlyList<ComponentEditorTarget> Targets { get; internal set; } = [];
    public PropertyDocument Properties { get; internal set; } = null!;
    public bool IsSelected { get; internal set; }
    internal Action? DefaultInspector;

    /// <summary>绘制 Inspector；默认显示内置字段。</summary>
    public virtual void OnDrawInspector() => DrawDefaultInspector();

    /// <summary>绘制选中组件的 SceneView 控件或辅助线。</summary>
    public virtual void OnSceneGui() { }

    /// <summary>绘制组件 Gizmos，由 SceneView 总开关控制。</summary>
    public virtual void OnDrawGizmos() { }

    /// <summary>在自定义界面中插入默认 Inspector。</summary>
    public void DrawDefaultInspector() => DefaultInspector?.Invoke();

    /// <summary>使用内置控件绘制一个字段，保留引用选择、集合编辑和撤销。</summary>
    public void DrawProperty(string name) => InspectorPanel.DrawCustomProperty(Properties, name);

    /// <summary>按字段名读取当前多选公共属性。</summary>
    public PropertyValue? FindProperty(string name) => Properties.FindProperty(name);

    /// <summary>暂存字段修改，回调结束时统一提交并记录撤销。</summary>
    public void SetValue(string name, InteropValue value)
    {
        PropertyValue property = FindProperty(name) ?? throw new ArgumentException($"Property not found: {name}", nameof(name));
        property.SetValue(value);
    }
}
