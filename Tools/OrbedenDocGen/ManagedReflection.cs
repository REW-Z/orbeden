using System.Reflection;

namespace OrbedenDocGen;

/// <summary>托管程序集的反射视图：原生绑定名对照表、可成页的公开类型、以及全部成员的文档 ID 索引。</summary>
/// <remarks>
/// 属性一律只用 GetCustomAttributesData 读元数据，绝不调 GetCustomAttributes/IsDefined：
/// 生成物里有 [ModuleInitializer]，构造特性实例会把模块初始化跑起来，等于执行生成的代码。
/// </remarks>
internal sealed class ManagedAssembly
{
    private readonly Assembly assembly;
    private readonly Dictionary<string, Type> byNativeBinding = new(StringComparer.Ordinal);
    private readonly Dictionary<string, MemberInfo> byDocumentationId = new(StringComparer.Ordinal);

    private ManagedAssembly(Assembly assembly) => this.assembly = assembly;

    /// <summary>公开的顶层类型，按全名排序；编译器生成的嵌套类型（如定长缓冲区）不算。</summary>
    internal List<Type> PublicTypes { get; } = [];

    /// <summary>文档 ID 到成员，覆盖全部可见性，用作 xml 注释条目的对照表。</summary>
    internal IReadOnlyDictionary<string, MemberInfo> MembersByDocumentationId => byDocumentationId;

    /// <summary>加载程序集；失败时抛 <see cref="BadImageFormatException"/> 或 <see cref="FileNotFoundException"/>。</summary>
    internal static ManagedAssembly Load(string path)
    {
        if (!File.Exists(path)) throw new FileNotFoundException($"Managed assembly not found: {path}", path);
        var assembly = Assembly.LoadFrom(path);
        ManagedAssembly result = new(assembly);
        result.Build();
        return result;
    }

    /// <summary>按生成包装上的 [NativeBinding("X")] 取托管类型。</summary>
    internal Type? FindByNativeBinding(string name) => byNativeBinding.TryGetValue(name, out var type) ? type : null;

    /// <summary>按托管全名取公开类型。</summary>
    internal Type? FindByManagedName(string name) => PublicTypes.FirstOrDefault(type => type.FullName == name);

    /// <summary>按文档 ID 取成员。</summary>
    internal MemberInfo? FindByDocumentationId(string id) => byDocumentationId.TryGetValue(id, out var member) ? member : null;

    private void Build()
    {
        foreach (var type in EnumerateTypes())
        {
            var native = NativeBindingName(type);
            if (native.Length != 0 && !byNativeBinding.ContainsKey(native)) byNativeBinding[native] = type;
            if (IsPageWorthy(type)) PublicTypes.Add(type);

            //类型自身的条目也要进表，否则 xml 里的 T: 条目全部对不上
            var typeId = "T:" + DocIds.TypeName(type);
            if (!byDocumentationId.ContainsKey(typeId)) byDocumentationId[typeId] = type;

            foreach (var member in type.GetMembers(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly))
            {
                var id = DocIds.MemberId(member);
                if (id != null && !byDocumentationId.ContainsKey(id)) byDocumentationId[id] = member;
            }
        }
        PublicTypes.Sort((left, right) => string.CompareOrdinal(left.FullName, right.FullName));
    }

    //反射类型可能加载不全，取到多少算多少
    private IEnumerable<Type> EnumerateTypes()
    {
        Type[] types;
        try
        {
            types = assembly.GetTypes();
        }
        catch (ReflectionTypeLoadException exception)
        {
            types = [.. exception.Types.Where(type => type != null).Cast<Type>()];
        }
        return types;
    }

    //顶层、公开、非编译器生成；嵌套的 <Payload>e__FixedBuffer 之类没有独立页面的意义
    private static bool IsPageWorthy(Type type) => type.IsPublic && !type.IsNested && !IsCompilerGenerated(type);

    private static bool IsCompilerGenerated(Type type) => type.Name.Contains('<') || type.Name.Contains('>');

    /// <summary>读取类型上的原生绑定名；没有这个特性就返回空串。</summary>
    internal static string NativeBindingName(Type type)
    {
        foreach (var data in type.GetCustomAttributesData())
            if (data.AttributeType.FullName == "Orbeden.NativeBindingAttribute" && data.ConstructorArguments.Count > 0)
                return data.ConstructorArguments[0].Value as string ?? "";
        return "";
    }
}
