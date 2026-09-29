using System.Reflection;
using System.Text;

namespace OrbedenDocGen;

/// <summary>按 ECMA-334 附录 D 的规则拼文档注释 ID，用来把反射成员对到编译器产出的 xml 条目上。</summary>
/// <remarks>
/// 编译器写出的参数类型是全限定名（int 写成 System.Int32、IntPtr 写成 System.IntPtr），
/// 泛型实例写成 List{System.Int32}，方法泛型形参写成 ``0，所以不能直接拿 Type.FullName 用。
/// </remarks>
internal static class DocIds
{
    /// <summary>类型的文档 ID 主体，嵌套类型用点号连接。</summary>
    internal static string TypeName(Type type)
    {
        List<Type> chain = [];
        for (Type? current = type; current != null; current = current.DeclaringType) chain.Add(current);
        chain.Reverse();

        StringBuilder result = new();
        for (int index = 0; index < chain.Count; ++index)
        {
            if (index != 0) result.Append('.');
            //泛型类型定义的 Name 自带 `n 记号，正是文档 ID 需要的写法
            result.Append(chain[index].Name);
        }
        var ns = type.Namespace;
        return ns is null || ns.Length == 0 ? result.ToString() : ns + "." + result;
    }

    /// <summary>成员（含类型与签名）的完整文档 ID；不认识的成员返回 null。</summary>
    internal static string? MemberId(MemberInfo member)
    {
        return member switch
        {
            Type type => "T:" + TypeName(type),
            MethodBase method => "M:" + MethodId(method),
            PropertyInfo property => "P:" + TypeName(property.DeclaringType!) + "." + property.Name + IndexerSuffix(property.GetIndexParameters()),
            FieldInfo field => "F:" + TypeName(field.DeclaringType!) + "." + field.Name,
            EventInfo info => "E:" + TypeName(info.DeclaringType!) + "." + info.Name,
            _ => null
        };
    }

    //方法 ID：泛型方法带 ``n 后缀，无参时连括号一起省掉，转换运算符要补返回类型
    private static string MethodId(MethodBase method)
    {
        var name = method.Name switch { ".ctor" => "#ctor", ".cctor" => "#cctor", _ => method.Name };
        StringBuilder result = new(TypeName(method.DeclaringType!) + "." + name);
        if (method.IsGenericMethodDefinition) result.Append("``").Append(method.GetGenericArguments().Length);
        ParameterInfo[] parameters = method.GetParameters();
        if (parameters.Length != 0) result.Append('(').Append(string.Join(",", parameters.Select(parameter => TypeReference(parameter.ParameterType)))).Append(')');
        if (method is MethodInfo info && method.Name is "op_Implicit" or "op_Explicit") result.Append('~').Append(TypeReference(info.ReturnType));
        return result.ToString();
    }

    private static string IndexerSuffix(ParameterInfo[] parameters)
    {
        return parameters.Length == 0 ? "" : "(" + string.Join(",", parameters.Select(parameter => TypeReference(parameter.ParameterType))) + ")";
    }

    //参数位置上的一个类型引用：byref/指针/数组/泛型形参都是不同的写法
    private static string TypeReference(Type type)
    {
        if (type.IsByRef) return TypeReference(type.GetElementType()!) + "@";
        if (type.IsPointer) return TypeReference(type.GetElementType()!) + "*";
        if (type.IsArray)
        {
            var element = TypeReference(type.GetElementType()!);
            return type.GetArrayRank() == 1
                ? element + "[]"
                : element + "[" + string.Join(",", Enumerable.Repeat("0:", type.GetArrayRank())) + "]";
        }

        if (type.IsGenericParameter) return (type.DeclaringMethod != null ? "``" : "`") + type.GenericParameterPosition;
        //泛型实例写成 List{System.Int32}：名字部分去掉 `n 记号，参数逐个展开
        if (type.IsGenericType && !type.IsGenericTypeDefinition)
            return StripArity(TypeName(type.GetGenericTypeDefinition())) + "{" + string.Join(",", type.GetGenericArguments().Select(TypeReference)) + "}";
        return TypeName(type);
    }

    //去掉 `n 泛型参数记号，逐个标识符处理，嵌套类型的外层记号也一并去掉
    private static string StripArity(string text)
    {
        StringBuilder result = new();
        for (int index = 0; index < text.Length; ++index)
        {
            if (text[index] != '`')
            {
                result.Append(text[index]);
                continue;
            }

            ++index;
            while (index < text.Length && char.IsAsciiDigit(text[index])) ++index;
            --index;
        }
        return result.ToString();
    }
}
