namespace Orbeden;

/// <summary>标记同一 Ens 只能挂载一个实例的组件。</summary>
[AttributeUsage(AttributeTargets.Class, Inherited = true)]
public sealed class UniqueComponentAttribute : Attribute
{
}

/// <summary>声明组件所依赖的其他组件。</summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = true)]
public sealed class DependsOnComponentAttribute : Attribute
{
    /// <summary>创建依赖声明。</summary>
    public DependsOnComponentAttribute(params Type[] componentTypes)
    {
        ComponentTypes = componentTypes ?? [];
    }

    /// <summary>依赖的组件类型。</summary>
    public Type[] ComponentTypes { get; }
}

/// <summary>标记私有字段需要显示在 Inspector 中。</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class SerializeFieldAttribute : Attribute
{
}

/// <summary>标记字段或属性不显示在 Inspector 中。</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class HideInInspectorAttribute : Attribute
{
}
