using System.Text.Json;

namespace OrbedenDocGen;

/// <summary>用户文档投影的读取模型：字段与 Tools/OrbedenMetaGen/DocProjection.cs 的写出结构逐项对应。</summary>
/// <remarks>
/// 只读一侧，所以不引 MetaGen 的工程：那会把文档生成器绑到绑定生成器的编译顺序上。
/// 结构变化由 SchemaVersion 拦下，读到不认识的版本直接拒绝而不是猜。
/// </remarks>
internal static class DocProjection
{
    /// <summary>本工具认识的投影结构版本。</summary>
    internal const int SupportedSchemaVersion = 1;

    /// <summary>读取并校验文档投影；文件缺失抛 <see cref="FileNotFoundException"/>，结构损坏抛 <see cref="InvalidDataException"/>。</summary>
    internal static DocDocument Load(string path)
    {
        if (!File.Exists(path)) throw new FileNotFoundException($"Projection JSON not found: {path}", path);
        DocDocument document;
        try
        {
            document = JsonSerializer.Deserialize<DocDocument>(File.ReadAllText(path))
                ?? throw new InvalidDataException($"Projection JSON is empty: {path}");
        }
        catch (JsonException exception)
        {
            throw new InvalidDataException($"Projection JSON is malformed: {exception.Message}", exception);
        }

        if (document.SchemaVersion != SupportedSchemaVersion)
            throw new InvalidDataException($"Projection schema version {document.SchemaVersion} is not supported (expected {SupportedSchemaVersion}).");
        return document;
    }
}

//以下结构与投影 JSON 一一对应，读取方按 SchemaVersion 拒绝不认识的版本。
internal sealed class DocDocument
{
    public int SchemaVersion { get; set; }
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
    //字段在托管侧是属性：get/set 表示可读可写方向
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
