using System.Text.Json;

namespace OrbedenMetaGen;

/// <summary>用户文档投影：只保留游戏侧可见的类型与成员，附带头文件里的注释文本。</summary>
/// <remarks>
/// 不复用 Bindings.Manifest.json：那份清单为 ABI 校验故意收录全部私有成员，是跨模块导入契约，
/// 把文档输入接上去会把契约与文档耦在一起。本投影从绑定生成器已算好的调用表写出，
/// 与 Bindings.Generated.cs 同源，因此不会出现“文档里有、托管侧其实调不到”的成员。
/// </remarks>
internal static class DocProjection
{
    private const int SchemaVersion = 1;

    /// <summary>写出文档投影；内容未变时不重写文件。</summary>
    internal static void Write(string path, BindingModel model, BindingTypes types, Dictionary<CppType, List<BindingCall>> calls, Func<CppType, bool> isComponent)
    {
        DocDocument document = new()
        {
            SchemaVersion = SchemaVersion,
            Namespace = model.ManagedNamespace,
            Types = [.. model.ObjectTypes.Select(type => ProjectType(type, model, calls, isComponent))],
            Values = [.. types.Values.Values
                .Where(value => value.Declaration != null && value.Kind is "record" or "enum")
                .DistinctBy(value => value.Cpp)
                .Where(value => !model.IsImported(value.Declaration!))
                .Select(value => ProjectValue(value.Declaration!, value.Kind, model))
                .OrderBy(value => value.Name, StringComparer.Ordinal)]
        };
        string text = JsonSerializer.Serialize(document, new JsonSerializerOptions { WriteIndented = true }) + "\n";
        BindingGenerator.WriteChanged(path, text);
    }

    //按调用表把类型投影成文档条目：成员只收托管侧真的生成了入口的那些。
    private static DocType ProjectType(CppType type, BindingModel model, Dictionary<CppType, List<BindingCall>> calls, Func<CppType, bool> isComponent)
    {
        DocType result = new()
        {
            Name = type.Name, QualifiedName = type.QualifiedName, ManagedName = StripGlobal(model.ManagedName(type)),
            BaseName = type.BaseName, Kind = type.Kind, Doc = type.Doc,
            IsAbstract = type.IsAbstract, IsUnique = type.IsUnique, IsFinal = type.IsFinal,
            IsComponent = isComponent(type)
        };
        foreach (var group in calls[type].GroupBy(call => call.Member))
        {
            CppMember member = group.Key;
            DocMember entry = new()
            {
                Name = member.Name, Doc = member.Doc, IsMethod = member.IsMethod, IsStatic = member.IsStatic,
                Type = member.Type, Serialize = member.Serialize, Changed = member.Changed,
                Parameters = [.. member.Parameters.Select(parameter => new DocParameter { Name = parameter.Name, Type = parameter.Type, Default = parameter.DefaultValue })]
            };
            if (member.IsMethod) entry.Arities = [.. group.Select(call => call.ParameterCount).Distinct().Order()];
            else entry.Property = string.Join("", group.Select(call => call.Operation).Distinct().Order().Select(operation => operation == "get" ? "get" : "set"));
            result.Members.Add(entry);
        }
        return result;
    }

    //枚举与值结构：托管侧会重新声明成 public enum / public struct，所以它们也是游戏侧可见的类型。
    private static DocValue ProjectValue(CppType declaration, string kind, BindingModel model)
    {
        DocValue result = new()
        {
            Name = declaration.Name, Kind = kind, EnumBase = declaration.EnumBase, Doc = declaration.Doc,
            ManagedName = StripGlobal(model.ManagedName(declaration))
        };
        if (kind == "enum")
            result.Values = [.. declaration.EnumValues.Select(item => new DocEnumValue { Name = item.Key, Value = item.Value })];
        else
            result.Fields = [.. model.ExportedMembers(declaration).Where(member => !member.IsMethod && !member.IsStatic)
                .Select(member => new DocField { Name = member.Name, Type = member.Type, Doc = member.Doc })];
        return result;
    }

    private static string StripGlobal(string managedName) => managedName.StartsWith("global::", StringComparison.Ordinal) ? managedName["global::".Length..] : managedName;
}

//文档投影的落盘结构。字段随文档需要增删，读取方按 SchemaVersion 拒绝不认识的版本。
internal sealed class DocDocument
{
    public int SchemaVersion { get; set; } = 1;
    public string Namespace { get; set; } = "";
    public List<DocType> Types { get; set; } = [];
    public List<DocValue> Values { get; set; } = [];
}

internal sealed class DocType
{
    public string Name { get; set; } = "";
    public string QualifiedName { get; set; } = "";
    public string ManagedName { get; set; } = "";
    public string BaseName { get; set; } = "";
    public string Kind { get; set; } = "";
    public bool IsAbstract { get; set; }
    public bool IsUnique { get; set; }
    public bool IsFinal { get; set; }
    public bool IsComponent { get; set; }
    public string Doc { get; set; } = "";
    public List<DocMember> Members { get; set; } = [];
}

internal sealed class DocMember
{
    public string Name { get; set; } = "";
    public string Doc { get; set; } = "";
    public bool IsMethod { get; set; }
    public bool IsStatic { get; set; }
    //字段在托管侧是属性：空串表示不生成入口，get/set 表示可读可写方向
    public string Property { get; set; } = "";
    public string Type { get; set; } = "";
    public bool Serialize { get; set; }
    public string Changed { get; set; } = "";
    public List<DocParameter> Parameters { get; set; } = [];
    //方法：带默认参数的 C++ 声明在托管侧展开成一档档重载，这里记下每档的参数个数
    public List<int> Arities { get; set; } = [];
}

internal sealed class DocParameter
{
    public string Name { get; set; } = "";
    public string Type { get; set; } = "";
    public string Default { get; set; } = "";
}

internal sealed class DocValue
{
    public string Name { get; set; } = "";
    public string Kind { get; set; } = "";
    public string ManagedName { get; set; } = "";
    public string EnumBase { get; set; } = "";
    public string Doc { get; set; } = "";
    public List<DocEnumValue> Values { get; set; } = [];
    public List<DocField> Fields { get; set; } = [];
}

internal sealed class DocEnumValue
{
    public string Name { get; set; } = "";
    public string Value { get; set; } = "";
}

internal sealed class DocField
{
    public string Name { get; set; } = "";
    public string Type { get; set; } = "";
    public string Doc { get; set; } = "";
}
