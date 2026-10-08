using Orbeden;

namespace OrbedenEditor;

/// <summary>为原生组件包装类型或托管脚本注册编辑器。</summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = false)]
public sealed class CustomEditorAttribute(Type componentType, bool editorForChildClasses = false) : Attribute
{
    public Type ComponentType { get; } = componentType;
    public bool EditorForChildClasses { get; } = editorForChildClasses;
}
