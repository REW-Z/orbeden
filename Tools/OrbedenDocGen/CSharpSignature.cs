using System.Reflection;
using System.Text;

namespace OrbedenDocGen;

/// <summary>把反射成员渲染成游戏脚本里能直接照抄的 C# 签名。</summary>
/// <remarks>
/// 类型一律用短名：文档只覆盖 Orbeden 命名空间下的类型，写全限定名反而挡住可读性。
/// 可空引用注解不渲染——NullabilityInfoContext 会去实例化特性，与只用元数据读取的约束冲突。
/// </remarks>
internal static class CSharpSignature
{
    private static readonly Dictionary<string, string> Aliases = new(StringComparer.Ordinal)
    {
        ["System.Boolean"] = "bool",
        ["System.Byte"] = "byte",
        ["System.SByte"] = "sbyte",
        ["System.Int16"] = "short",
        ["System.UInt16"] = "ushort",
        ["System.Int32"] = "int",
        ["System.UInt32"] = "uint",
        ["System.Int64"] = "long",
        ["System.UInt64"] = "ulong",
        ["System.Single"] = "float",
        ["System.Double"] = "double",
        ["System.Decimal"] = "decimal",
        ["System.Char"] = "char",
        ["System.String"] = "string",
        ["System.Object"] = "object",
        ["System.Void"] = "void"
    };

    /// <summary>类型的 C# 写法。</summary>
    internal static string Name(Type type)
    {
        if (type.IsByRef) return Name(type.GetElementType()!);
        if (type.IsPointer) return Name(type.GetElementType()!) + "*";
        if (type.IsArray) return Name(type.GetElementType()!) + "[" + new string(',', type.GetArrayRank() - 1) + "]";
        if (type.IsGenericParameter) return type.Name;
        if (type.IsGenericType && !type.IsGenericTypeDefinition)
            return StripArity(Name(type.GetGenericTypeDefinition())) + "<" + string.Join(", ", type.GetGenericArguments().Select(Name)) + ">";
        if (type.IsNested && type.DeclaringType != null) return Name(type.DeclaringType) + "." + type.Name;

        var full = type.FullName ?? type.Name;
        return Aliases.TryGetValue(full, out var alias) ? alias : StripArity(type.Name);
    }

    /// <summary>类型的 C# 声明行，例如 public sealed class Transform : Component。</summary>
    internal static string TypeDeclaration(Type type)
    {
        if (type.IsEnum) return $"public enum {type.Name} : {Name(Enum.GetUnderlyingType(type))}";
        StringBuilder result = new("public");
        if (type.IsAbstract && type.IsSealed) result.Append(" static");
        else if (type.IsAbstract) result.Append(" abstract");
        else if (type.IsSealed && !type.IsValueType) result.Append(" sealed");
        result.Append(type.IsValueType ? " struct" : type.IsInterface ? " interface" : " class").Append(' ').Append(type.Name);
        if (type.BaseType != null && type.BaseType != typeof(object) && !type.IsValueType && type.BaseType != typeof(ValueType) && type.BaseType != typeof(Enum))
            result.Append(" : ").Append(Name(type.BaseType));
        return result.ToString();
    }

    /// <summary>成员的 C# 签名；构造函数、方法、属性、字段各一种写法。取不到签名时返回空串。</summary>
    internal static string MemberDeclaration(MemberInfo member)
    {
        return member switch
        {
            ConstructorInfo constructor => $"{Access(constructor)} {constructor.DeclaringType!.Name}" + ParameterList(constructor.GetParameters()),
            MethodInfo method => MethodDeclaration(method),
            PropertyInfo property => PropertyDeclaration(property),
            FieldInfo field => FieldDeclaration(field),
            _ => ""
        };
    }

    private static string MethodDeclaration(MethodInfo method)
    {
        StringBuilder result = new(Access(method));
        if (method.IsStatic) result.Append(" static");
        if (method.IsAbstract) result.Append(" abstract");
        //运算符在 C# 里写不出方法名，按用户实际写的符号渲染：转换运算符不重复写返回类型
        var symbol = OperatorSymbol(method.Name);
        if (symbol is "implicit" or "explicit") result.Append(' ').Append(symbol).Append(" operator ").Append(Name(method.ReturnType));
        else if (symbol != null) result.Append(' ').Append(Name(method.ReturnType)).Append(" operator ").Append(symbol);
        else result.Append(' ').Append(Name(method.ReturnType)).Append(' ').Append(method.Name);
        if (method.IsGenericMethodDefinition)
            result.Append('<').Append(string.Join(", ", method.GetGenericArguments().Select(argument => argument.Name))).Append('>');
        result.Append(ParameterList(method.GetParameters()));

        //泛型约束写在签名末尾，游戏侧要照着约束选类型
        List<string> constraints = [];
        if (method.IsGenericMethodDefinition)
            foreach (var argument in method.GetGenericArguments())
                if (Constraint(argument) is string text) constraints.Add($"where {argument.Name} : {text}");
        foreach (var text in constraints) result.Append(' ').Append(text);
        return result.ToString();
    }

    private static string PropertyDeclaration(PropertyInfo property)
    {
        var accessors = (property.GetMethod != null, property.SetMethod != null) switch
        {
            (true, true) => "{ get; set; }",
            (true, false) => "{ get; }",
            (false, true) => "{ set; }",
            _ => "{ }"
        };
        StringBuilder result = new(Access(property.GetMethod ?? property.SetMethod!));
        if ((property.GetMethod ?? property.SetMethod)!.IsStatic) result.Append(" static");
        result.Append(' ').Append(Name(property.PropertyType)).Append(' ').Append(property.Name).Append(' ').Append(accessors);
        var index = property.GetIndexParameters();
        if (index.Length != 0) result.Append("  // 索引器：").Append(ParameterList(index));
        return result.ToString();
    }

    private static string FieldDeclaration(FieldInfo field)
    {
        StringBuilder result = new(Access(field));
        if (field.IsLiteral) result.Append(" const");
        else if (field.IsStatic) result.Append(" static");
        if (field.IsInitOnly) result.Append(" readonly");
        result.Append(' ').Append(Name(field.FieldType)).Append(' ').Append(field.Name);
        if (field.IsLiteral && field.GetRawConstantValue() is { } value) result.Append(" = ").Append(Literal(value));
        return result.ToString();
    }

    /// <summary>枚举成员的值，按底层类型渲染；C++ 侧的 1u&lt;&lt;0 这种表达式对 C# 读者没有意义。</summary>
    internal static string EnumValue(FieldInfo field)
    {
        var value = field.GetRawConstantValue()!;
        return Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture) ?? "0";
    }

    private static string Access(MethodBase method)
    {
        if (method.IsPublic) return "public";
        if (method.IsFamily) return "protected";
        if (method.IsFamilyOrAssembly) return "protected internal";
        return "internal";
    }

    private static string Access(FieldInfo field)
    {
        if (field.IsPublic) return "public";
        if (field.IsFamily) return "protected";
        if (field.IsFamilyOrAssembly) return "protected internal";
        return "internal";
    }

    private static string ParameterList(ParameterInfo[] parameters)
    {
        if (parameters.Length == 0) return "()";
        return "(" + string.Join(", ", parameters.Select(Parameter)) + ")";
    }

    private static string Parameter(ParameterInfo parameter)
    {
        var prefix = parameter.IsOut ? "out " : parameter.ParameterType.IsByRef ? "ref " : "";
        var text = prefix + Name(parameter.ParameterType) + " " + parameter.Name;
        //默认值在游戏侧很关键：不传就走默认，写上重载才数得清。
        //取 RawDefaultValue 而不是 DefaultValue：前者只读元数据常量，不去实例化 DefaultParameterValueAttribute。
        if (parameter.HasDefaultValue) text += " = " + Literal(parameter.RawDefaultValue);
        return text;
    }

    private static string Literal(object? value)
    {
        return value switch
        {
            null => "null",
            string text => "\"" + text + "\"",
            bool flag => flag ? "true" : "false",
            char character => "'" + character + "'",
            float number => FloatLiteral(number, "float"),
            double number => FloatLiteral(number, "double"),
            _ => Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture) ?? "default"
        };
    }

    //极值不写成 -3.4028235E+38 这种科学计数法，游戏侧照着抄很难读。
    //装箱的 float 取出来会先拓宽成 double，极值必须按目标类型比：拿 double.MaxValue 比 float.MaxValue 永远不等。
    private static string FloatLiteral(double value, string type)
    {
        var isSingle = type == "float";
        var maximum = isSingle ? float.MaxValue : double.MaxValue;
        var minimum = isSingle ? float.MinValue : double.MinValue;
        var epsilon = isSingle ? float.Epsilon : double.Epsilon;
        var suffix = isSingle ? "f" : "";
        return value switch
        {
            _ when value == maximum => type + ".MaxValue",
            _ when value == minimum => type + ".MinValue",
            _ when value == epsilon => type + ".Epsilon",
            double.PositiveInfinity => type + ".PositiveInfinity",
            double.NegativeInfinity => type + ".NegativeInfinity",
            _ when double.IsNaN(value) => type + ".NaN",
            _ => value.ToString("R", System.Globalization.CultureInfo.InvariantCulture) + suffix
        };
    }

    /// <summary>运算符方法名到 C# 符号的对照，页面上按用户写法显示。</summary>
    internal static string? OperatorSymbol(string name) => Operators.TryGetValue(name, out var symbol) ? symbol : null;

    private static readonly Dictionary<string, string> Operators = new(StringComparer.Ordinal)
    {
        ["op_Addition"] = "+", ["op_Subtraction"] = "-", ["op_Multiply"] = "*", ["op_Division"] = "/",
        ["op_Modulus"] = "%", ["op_Equality"] = "==", ["op_Inequality"] = "!=", ["op_LessThan"] = "<",
        ["op_GreaterThan"] = ">", ["op_LessThanOrEqual"] = "<=", ["op_GreaterThanOrEqual"] = ">=",
        ["op_UnaryNegation"] = "-", ["op_UnaryPlus"] = "+", ["op_LogicalNot"] = "!", ["op_BitwiseAnd"] = "&",
        ["op_BitwiseOr"] = "|", ["op_ExclusiveOr"] = "^", ["op_LeftShift"] = "<<", ["op_RightShift"] = ">>",
        ["op_Increment"] = "++", ["op_Decrement"] = "--", ["op_True"] = "true", ["op_False"] = "false",
        ["op_Implicit"] = "implicit", ["op_Explicit"] = "explicit"
    };

    //泛型形参的约束；struct 约束已经把 new() 含进去了，不重复写
    private static string? Constraint(Type argument)
    {
        List<string> parts = [];
        var attributes = argument.GenericParameterAttributes;
        var valueType = attributes.HasFlag(GenericParameterAttributes.NotNullableValueTypeConstraint);
        if (valueType) parts.Add("struct");
        else if (attributes.HasFlag(GenericParameterAttributes.ReferenceTypeConstraint)) parts.Add("class");
        foreach (var constraint in argument.GetGenericParameterConstraints())
            if (constraint != typeof(ValueType)) parts.Add(Name(constraint));
        if (!valueType && attributes.HasFlag(GenericParameterAttributes.DefaultConstructorConstraint)) parts.Add("new()");
        return parts.Count == 0 ? null : string.Join(", ", parts);
    }

    private static string StripArity(string name)
    {
        var tick = name.IndexOf('`');
        return tick < 0 ? name : name[..tick];
    }
}
