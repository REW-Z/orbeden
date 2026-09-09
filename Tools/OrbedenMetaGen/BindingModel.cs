using System.Text.Json;

namespace OrbedenMetaGen;

internal sealed class BindingManifest
{
    public int Version { get; set; } = 1;
    public string ManagedNamespace { get; set; } = "Orbeden";
    public List<CppType> Types { get; set; } = [];
}

internal sealed class BindingModel
{
    internal List<CppType> LocalTypes { get; }
    internal List<CppType> ObjectTypes { get; }
    internal Dictionary<string, CppType> Types { get; } = new(StringComparer.Ordinal);
    internal HashSet<string> ImportedNames { get; } = new(StringComparer.Ordinal);
    internal string ManagedNamespace { get; }
    internal string ImportedNamespace { get; } = "Orbeden";
    internal static readonly HashSet<string> LifecycleNames = new(StringComparer.Ordinal)
    { "OnAttach", "OnDetach", "OnWorldActiveChanged", "OnStart", "OnUpdate", "OnFixedUpdate", "OnLateUpdate", "OnDrawGUI", "OnEnd" };

    /// <summary>合并模块类型清单并解析 Object 继承关系。</summary>
    internal BindingModel(List<CppType> localTypes, string managedNamespace, string? importPath)
    {
        LocalTypes = localTypes; ManagedNamespace = managedNamespace;
        if (importPath != null)
        {
            BindingManifest imported = JsonSerializer.Deserialize<BindingManifest>(File.ReadAllText(importPath)) ?? throw new InvalidDataException("Empty binding manifest");
            if (imported.Version != 1) throw new InvalidDataException("Unsupported binding manifest version");
            ImportedNamespace = imported.ManagedNamespace;
            foreach (var type in imported.Types) { Types.Add(type.QualifiedName, type); ImportedNames.Add(type.QualifiedName); }
        }
        foreach (var type in localTypes)
        {
            if (!Types.TryAdd(type.QualifiedName, type))
                throw new InvalidDataException($"{type.File}:{type.Line}: duplicate type {type.QualifiedName}");
        }
        ObjectTypes = localTypes.Where(type => type.IsObject).OrderBy(type => Depth(type)).ThenBy(type => type.QualifiedName, StringComparer.Ordinal).ToList();
        foreach (var type in ObjectTypes)
        {
            if (type.Name != "Object" && (Resolve(type.BaseName, type) is not CppType parent || !parent.IsObject))
                throw new InvalidDataException($"{type.File}:{type.Line}: unresolved Object base '{type.BaseName}'; supply the Core binding manifest");
        }
    }

    internal CppType? Resolve(string name, CppType context)
    {
        name = name.TrimStart(':');
        string scope = context.QualifiedName;
        while (scope.Contains("::", StringComparison.Ordinal))
        {
            scope = scope[..scope.LastIndexOf("::", StringComparison.Ordinal)];
            if (Types.TryGetValue(scope + "::" + name, out var nested)) return nested;
        }
        if (Types.TryGetValue(name, out var exact)) return exact;
        if (name == "Object" && Types.TryGetValue("Orbeden::Object", out var root)) return root;
        return null;
    }
    internal int Depth(CppType type)
    {
        HashSet<string> seen = []; int depth = 0;
        while (type.BaseName.Length != 0 && Resolve(type.BaseName, type) is CppType parent)
        {
            if (!seen.Add(type.QualifiedName)) throw new InvalidDataException($"Cyclic inheritance at {type.QualifiedName}");
            ++depth; type = parent;
        }
        return depth;
    }
    internal string ManagedName(CppType type)
    {
        string prefix = ImportedNames.Contains(type.QualifiedName) ? ImportedNamespace : ManagedNamespace;
        string name = type.QualifiedName.StartsWith("Orbeden::", StringComparison.Ordinal) ? type.QualifiedName[9..] : type.QualifiedName;
        return "global::" + prefix + "." + name.Replace("::", ".");
    }
    internal IEnumerable<CppMember> ExportedMembers(CppType type) => type.Members.Where(member => member.Access == "public"
        && !member.IgnoreBinding && !member.IsTemplate && !LifecycleNames.Contains(member.Name));
    internal void WriteManifest(string path)
    {
        var manifest = new BindingManifest { ManagedNamespace = ManagedNamespace, Types = LocalTypes.OrderBy(type => type.QualifiedName, StringComparer.Ordinal).ToList() };
        string text = JsonSerializer.Serialize(manifest, new JsonSerializerOptions { WriteIndented = true }) + "\n";
        if (!File.Exists(path) || File.ReadAllText(path) != text) File.WriteAllText(path, text);
    }
}
