namespace OrbedenCore.CSharp;

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
