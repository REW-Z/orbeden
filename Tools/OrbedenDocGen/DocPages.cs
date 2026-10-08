using System.Globalization;
using System.Reflection;
using System.Text;

namespace OrbedenDocGen;

/// <summary>成员条目的注释来源，决定来源统计落在哪一档。</summary>
internal enum DocSource
{
    None,
    /// <summary>编译器产出的 xml 注释，手写托管层走这条。</summary>
    Xml,
    /// <summary>C++ 头文件里的注释，生成包装走这条。</summary>
    Projection
}

/// <summary>一个成员在页面上的样子；算锚点用成员标识，渲染用标题，两者不互相牵连。</summary>
internal sealed class DocEntry
{
    /// <summary>算锚点用的成员标识，与渲染出的标题无关。</summary>
    public string AnchorName { get; set; } = "";
    public string Anchor { get; set; } = "";
    public string Section { get; set; } = "";
    /// <summary>页面上显示的标题；构造函数写类型名，运算符写 operator 符号。</summary>
    public string Title { get; set; } = "";
    public string Signature { get; set; } = "";
    public string Summary { get; set; } = "";
    public string Cpp { get; set; } = "";
    public bool Serialized { get; set; }
    public string Changed { get; set; } = "";
    public int Arity { get; set; }
    /// <summary>参数类型拼成的串，只在参数个数分不开时用于锚点。</summary>
    public string AnchorTypesFragment { get; set; } = "";
    public DocSource Source { get; set; } = DocSource.None;
}

/// <summary>一个类型页：分类决定目录，成员按段分组。</summary>
internal sealed class DocPage
{
    public string Category { get; set; } = "";
    public string Name { get; set; } = "";
    public string TypeFullName { get; set; } = "";
    public string Namespace { get; set; } = "";
    public string Kind { get; set; } = "";
    public string Declaration { get; set; } = "";
    public string Summary { get; set; } = "";
    public string? BaseTypeName { get; set; }
    public List<DocEntry> Entries { get; set; } = [];
    public DocSource Source { get; set; } = DocSource.None;
}

/// <summary>把投影、反射、xml 注释合成页面模型。</summary>
/// <remarks>
/// 成员清单以托管类型为准：投影只收绑定的调用入口，像 Object.IsValid、Ens.Name 这些手写成员
/// 不在里面，但它们同样是游戏侧要用的。投影只负责补 C++ 声明和头文件注释。
/// </remarks>
internal static class DocPages
{
    /// <summary>目录名。</summary>
    internal const string Components = "components";
    internal const string Values = "values";
    internal const string Managed = "managed";

    /// <summary>生成全部页面；同时报告投影里对不上托管类型的成员。</summary>
    internal static List<DocPage> Build(DocDocument projection, ManagedAssembly managed, XmlDocumentation xml, List<string> warnings)
    {
        Dictionary<string, DocType> projectionTypes = new(StringComparer.Ordinal);
        foreach (var type in projection.Types)
        {
            var reflected = managed.FindByNativeBinding(type.Name);
            if (reflected == null)
            {
                //绑定名对不上托管类型，说明投影与程序集不是同一次生成的
                warnings.Add($"projection type {type.Name} has no managed type carrying [NativeBinding(\"{type.Name}\")]");
                continue;
            }
            projectionTypes[reflected.FullName!] = type;
        }

        Dictionary<string, DocValue> projectionValues = new(StringComparer.Ordinal);
        foreach (var value in projection.Values)
            if (managed.FindByManagedName(value.ManagedName) is Type reflected) projectionValues[reflected.FullName!] = value;
            else warnings.Add($"projection value {value.Name} has no managed type {value.ManagedName}");

        List<DocPage> pages = [];
        HashSet<string> names = new(StringComparer.Ordinal);
        foreach (var type in managed.PublicTypes)
        {
            var fullName = type.FullName!;
            DocPage page;
            if (projectionTypes.TryGetValue(fullName, out var bound))
            {
                page = BuildProjectionPage(bound, managed, xml);
                page.Category = Components;
            }
            else if (projectionValues.TryGetValue(fullName, out var value))
            {
                page = BuildValuePage(type, value, xml);
                page.Category = Values;
            }
            else
            {
                page = BuildManagedPage(type, xml);
                page.Category = Managed;
            }

            page.Name = UniqueName(names, type.Name);
            page.TypeFullName = fullName;
            page.Namespace = type.Namespace ?? "";
            pages.Add(page);
        }

        //成员顺序全部按键排序，保证两次生成逐字节一致
        pages.Sort((left, right) => string.CompareOrdinal(left.Category + "/" + left.Name, right.Category + "/" + right.Name));
        foreach (var page in pages) AssignAnchors(page);
        return pages;
    }

    /// <summary>页面的相对路径，用于索引与基类型链接。</summary>
    internal static string RelativePath(DocPage page) => page.Category + "/" + page.Name + ".md";

    //同名类型只可能来自不同命名空间，加后缀保证互不覆盖
    private static string UniqueName(HashSet<string> used, string name)
    {
        if (used.Add(name)) return name;
        for (var index = 2; ; ++index)
        {
            var candidate = name + "-" + index.ToString(CultureInfo.InvariantCulture);
            if (used.Add(candidate)) return candidate;
        }
    }

    //锚点只由成员标识算出，不跟渲染出的标题走；重名才是重载，之后补参数个数
    private static void AssignAnchors(DocPage page)
    {
        Dictionary<string, List<DocEntry>> groups = new(StringComparer.Ordinal);
        foreach (var entry in page.Entries)
        {
            if (!groups.TryGetValue(entry.AnchorName, out var group)) groups[entry.AnchorName] = group = [];
            group.Add(entry);
        }

        foreach (var (name, group) in groups)
        {
            var key = name == "#ctor" ? "ctor" : name.ToLowerInvariant();
            if (group.Count == 1)
            {
                group[0].Anchor = "m-" + key;
                continue;
            }

            //参数个数分得开就用个数；分不开的档再补参数类型，否则锚点会随成员枚举顺序漂
            var distinct = group.Select(entry => entry.Arity).Distinct().Count() == group.Count;
            foreach (var entry in group)
            {
                var arity = entry.Arity.ToString(CultureInfo.InvariantCulture);
                entry.Anchor = "m-" + key + "-" + arity + (distinct ? "" : "-" + Sanitize(entry.AnchorTypesFragment));
            }
        }
    }

    //参数类型进锚点前只留字母数字，避免 * & < > 这些写法差异带来两套锚点
    private static string Sanitize(string text)
    {
        StringBuilder result = new(text.Length);
        foreach (var character in text)
            if (char.IsAsciiLetterOrDigit(character)) result.Append(char.ToLowerInvariant(character));
        return result.Length == 0 ? "x" : result.ToString();
    }

    //生成包装页：成员取托管类型的公开声明，投影补 C++ 声明与头文件注释
    private static DocPage BuildProjectionPage(DocType type, ManagedAssembly managed, XmlDocumentation xml)
    {
        var reflected = managed.FindByNativeBinding(type.Name)!;
        var typeSummary = xml.Get("T:" + DocIds.TypeName(reflected));
        DocPage page = new()
        {
            Kind = KindLabel(type),
            Declaration = CSharpSignature.TypeDeclaration(reflected),
            Summary = FirstNonEmpty(typeSummary, type.Doc),
            BaseTypeName = reflected.BaseType?.FullName,
            Source = typeSummary.Length != 0 ? DocSource.Xml : type.Doc.Length != 0 ? DocSource.Projection : DocSource.None
        };

        Dictionary<string, DocMember> declared = new(StringComparer.Ordinal);
        foreach (var member in type.Members) declared[member.Name] = member;

        HashSet<string> rendered = new(StringComparer.Ordinal);
        foreach (var member in PublicMembers(reflected))
        {
            var id = DocIds.MemberId(member) ?? "";
            var summary = xml.Get(id);
            declared.TryGetValue(MemberIdentity(member), out var declaredMember);
            if (declaredMember != null) rendered.Add(declaredMember.Name);
            page.Entries.Add(new DocEntry
            {
                AnchorName = MemberIdentity(member),
                Title = DisplayName(member, reflected),
                Signature = CSharpSignature.MemberDeclaration(member),
                Summary = FirstNonEmpty(summary, declaredMember?.Doc ?? ""),
                Source = summary.Length != 0 ? DocSource.Xml : declaredMember is { Doc.Length: > 0 } ? DocSource.Projection : DocSource.None,
                Arity = Arity(member),
                Section = Section(member),
                AnchorTypesFragment = member is MethodBase method ? string.Join("-", method.GetParameters().Select(parameter => CSharpSignature.Name(parameter.ParameterType))) : "",
                Serialized = declaredMember?.Serialize ?? false,
                Changed = declaredMember?.Changed ?? "",
                Cpp = declaredMember == null ? "" : CppDeclaration(declaredMember)
            });
        }

        //托管侧没有入口的投影成员仍然列出，宁可多一条也不要静默丢掉
        foreach (var member in type.Members.Where(member => !rendered.Contains(member.Name)))
            page.Entries.Add(new DocEntry
            {
                AnchorName = member.Name,
                Title = member.Name,
                Summary = member.Doc,
                Source = member.Doc.Length != 0 ? DocSource.Projection : DocSource.None,
                Arity = member.Arities.Count != 0 ? member.Arities[0] : member.Parameters.Count,
                Section = member.IsMethod ? "方法" : "属性",
                Cpp = CppDeclaration(member)
            });
        return page;
    }

    //值类型页：枚举走常量表，值结构走字段表，成员名与数值都以托管声明为准
    private static DocPage BuildValuePage(Type type, DocValue value, XmlDocumentation xml)
    {
        var typeName = DocIds.TypeName(type);
        var typeSummary = xml.Get("T:" + typeName);
        DocPage page = new()
        {
            Kind = type.IsEnum ? "枚举" : "值结构",
            Declaration = CSharpSignature.TypeDeclaration(type),
            Summary = FirstNonEmpty(typeSummary, value.Doc),
            Source = typeSummary.Length != 0 ? DocSource.Xml : value.Doc.Length != 0 ? DocSource.Projection : DocSource.None
        };

        Dictionary<string, DocField> declared = new(StringComparer.Ordinal);
        foreach (var field in value.Fields) declared[field.Name] = field;

        foreach (var member in PublicMembers(type))
        {
            //枚举成员的说明在投影里没有对应条目，值结构的字段才有
            declared.TryGetValue(MemberIdentity(member), out var declaredField);
            var id = DocIds.MemberId(member) ?? "";
            var summary = xml.Get(id);
            page.Entries.Add(new DocEntry
            {
                AnchorName = MemberIdentity(member),
                Title = DisplayName(member, type),
                Signature = member is FieldInfo info && IsEnumValue(member)
                    ? CSharpSignature.EnumValue(info)
                    : CSharpSignature.MemberDeclaration(member),
                Summary = FirstNonEmpty(summary, declaredField?.Doc ?? ""),
                Source = summary.Length != 0 ? DocSource.Xml : declaredField is { Doc.Length: > 0 } ? DocSource.Projection : DocSource.None,
                Arity = Arity(member),
                Section = Section(member),
                AnchorTypesFragment = member is MethodBase method ? string.Join("-", method.GetParameters().Select(parameter => CSharpSignature.Name(parameter.ParameterType))) : "",
                Cpp = declaredField == null ? "" : declaredField.Type + " " + declaredField.Name
            });
        }
        return page;
    }

    //手写托管层页：只有反射与 xml 注释两份输入
    private static DocPage BuildManagedPage(Type type, XmlDocumentation xml)
    {
        var typeName = DocIds.TypeName(type);
        var summary = xml.Get("T:" + typeName);
        DocPage page = new()
        {
            Kind = ManagedKindLabel(type),
            Declaration = CSharpSignature.TypeDeclaration(type),
            Summary = summary,
            BaseTypeName = type.IsEnum || type.IsValueType ? null : type.BaseType?.FullName,
            Source = summary.Length != 0 ? DocSource.Xml : DocSource.None
        };

        foreach (var member in PublicMembers(type))
        {
            var id = DocIds.MemberId(member) ?? "";
            var text = xml.Get(id);
            page.Entries.Add(new DocEntry
            {
                AnchorName = MemberIdentity(member),
                Title = DisplayName(member, type),
                Signature = member is FieldInfo info && IsEnumValue(member)
                    ? CSharpSignature.EnumValue(info)
                    : CSharpSignature.MemberDeclaration(member),
                Summary = text,
                Source = text.Length != 0 ? DocSource.Xml : DocSource.None,
                Arity = Arity(member),
                Section = Section(member),
                AnchorTypesFragment = member is MethodBase method ? string.Join("-", method.GetParameters().Select(parameter => CSharpSignature.Name(parameter.ParameterType))) : ""
            });
        }
        return page;
    }

    /// <summary>类型页要列出的公开成员：属性访问器由属性本身代表，枚举的 value__ 是编译器内部字段。</summary>
    internal static IEnumerable<MemberInfo> PublicMembers(Type type)
    {
        const BindingFlags flags = BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
        HashSet<string> accessors = [];
        foreach (var member in type.GetMembers(flags))
            if (member is PropertyInfo property)
            {
                if (property.GetMethod != null) accessors.Add(property.GetMethod.Name);
                if (property.SetMethod != null) accessors.Add(property.SetMethod.Name);
            }

        return type.GetMembers(flags)
            .Where(member => member switch
            {
                FieldInfo { IsSpecialName: true } => false,
                MethodInfo => !accessors.Contains(member.Name),
                ConstructorInfo or PropertyInfo or FieldInfo => true,
                _ => false
            })
            .OrderBy(member => SectionOrder(Section(member)))
            //枚举值按数值排，不按名字：序号本身是文档的一部分（有的枚举数值进批次键、进持久化载荷），
            //按名字排会把固定序号打乱，读者反查数值时对不上。非枚举值这一档恒为 null，不影响原有的名字序。
            .ThenBy(member => member is FieldInfo { IsLiteral: true } field ? EnumValueKey(field) : (decimal?)null)
            .ThenBy(MemberIdentity, StringComparer.Ordinal)
            .ThenBy(Arity);
    }

    //底层类型可以是 uint/ulong，decimal 能精确表示全部合法的枚举底层类型，比较也是全序
    private static decimal EnumValueKey(FieldInfo field) => Convert.ToDecimal(field.GetRawConstantValue(), CultureInfo.InvariantCulture);

    private static bool IsEnumValue(MemberInfo member) => member is FieldInfo { IsLiteral: true };

    private static string Section(MemberInfo member) => member switch
    {
        ConstructorInfo => "构造函数",
        PropertyInfo => "属性",
        FieldInfo { IsLiteral: true } => "枚举值",
        FieldInfo => "字段",
        MethodInfo { IsSpecialName: true } method when method.Name.StartsWith("op_", StringComparison.Ordinal) => "运算符",
        MethodInfo => "方法",
        _ => "其他"
    };

    private static int SectionOrder(string section) => section switch
    {
        "构造函数" => 0,
        "属性" => 1,
        "字段" => 2,
        "枚举值" => 3,
        "运算符" => 4,
        _ => 5
    };

    internal static int Arity(MemberInfo member) => member switch
    {
        MethodBase method => method.GetParameters().Length,
        PropertyInfo property => property.GetIndexParameters().Length,
        _ => 0
    };

    /// <summary>算锚点用的成员标识：构造函数是 #ctor，其余就是成员名。</summary>
    private static string MemberIdentity(MemberInfo member) => member is ConstructorInfo ? "#ctor" : member.Name;

    //页面上显示的标题：构造函数写类型名，运算符写 C# 符号，其余照原名
    private static string DisplayName(MemberInfo member, Type owner)
    {
        if (member is ConstructorInfo) return owner.Name;
        if (member is MethodInfo method && CSharpSignature.OperatorSymbol(method.Name) is string symbol)
            return symbol is "implicit" or "explicit" ? symbol + " operator " + CSharpSignature.Name(method.ReturnType) : "operator " + symbol;
        return member.Name;
    }

    //C++ 声明由投影的返回类型与参数表拼出，没进投影的成员不编造
    private static string CppDeclaration(DocMember member)
    {
        if (!member.IsMethod) return member.Type + " " + member.Name;
        StringBuilder result = new(member.Type + " " + member.Name + "(");
        result.Append(string.Join(", ", member.Parameters.Select(parameter =>
            parameter.Type + " " + parameter.Name + (parameter.Default.Length != 0 ? " = " + parameter.Default : ""))));
        return result.Append(')').ToString();
    }

    private static string KindLabel(DocType type)
    {
        if (type.IsComponent) return "组件";
        return type.Kind switch { "class" => "引擎对象", _ => type.Kind };
    }

    private static string ManagedKindLabel(Type type)
    {
        if (type.IsEnum) return "枚举";
        if (type.IsValueType) return "值类型";
        if (type.IsAbstract && type.IsSealed) return "静态类";
        if (typeof(Attribute).IsAssignableFrom(type)) return "特性";
        return "托管类";
    }

    private static string FirstNonEmpty(string first, string second) => first.Length != 0 ? first : second;
}
