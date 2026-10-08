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
    internal Dictionary<string, string> ImportedNamespaces { get; } = new(StringComparer.Ordinal);
    internal string ManagedNamespace { get; }
    internal static readonly HashSet<string> LifecycleNames = new(StringComparer.Ordinal)
    { "OnAttach", "OnDetach", "OnWorldActiveChanged", "OnStart", "OnUpdate", "OnFixedUpdate", "OnLateUpdate", "OnDrawGUI", "OnEnd" };

    /// <summary>合并多个模块清单并解析 Object 继承关系。</summary>
    internal BindingModel(List<CppType> localTypes, string managedNamespace, List<string>? importPaths)
    {
        LocalTypes = localTypes; ManagedNamespace = managedNamespace;
        foreach (string importPath in importPaths ?? [])
        {
            BindingManifest imported = JsonSerializer.Deserialize<BindingManifest>(File.ReadAllText(importPath)) ?? throw new InvalidDataException("Empty binding manifest");
            if (imported.Version != 1) throw new InvalidDataException("Unsupported binding manifest version");
            foreach (var type in imported.Types)
            {
                if (!Types.TryAdd(type.QualifiedName, type))
                    throw new InvalidDataException($"duplicate type {type.QualifiedName} in imported manifests");
                ImportedNamespaces[type.QualifiedName] = imported.ManagedNamespace;
            }
        }
        foreach (var type in localTypes)
        {
            if (!Types.TryAdd(type.QualifiedName, type))
                throw new InvalidDataException($"{type.File}:{type.Line}: duplicate type {type.QualifiedName}");
        }
        foreach (CppType type in localTypes.Where(type => !type.IsObject && type.Kind == "class" && type.BaseName.Length != 0))
        {
            if (Resolve(type.BaseName, type) is CppType parent && parent.IsObject)
                throw new InvalidDataException($"{type.File}:{type.Line}: {type.QualifiedName} derives from Object type '{parent.QualifiedName}' but does not declare OBJECT_TYPE_DECLARE");
        }
        foreach (CppType type in localTypes.Where(type => type.IsObject))
        {
            CppType? colliding = Types.Values.FirstOrDefault(other => other.IsObject && other.Name == type.Name && other.QualifiedName != type.QualifiedName);
            if (colliding != null)
                throw new InvalidDataException($"{type.File}:{type.Line}: {type.QualifiedName}: native type name '{type.Name}' collides with '{colliding.QualifiedName}'; the runtime resolves wrappers by short type name, so rename one of them");
        }
        foreach (IGrouping<string, CppType> collision in localTypes.Where(type => type.IsObject)
            .GroupBy(ManagedName, StringComparer.Ordinal).Where(group => group.Count() > 1))
        {
            CppType second = collision.Skip(1).First();
            throw new InvalidDataException($"{second.File}:{second.Line}: {second.QualifiedName}: managed class name '{collision.Key}' collides with '{collision.First().QualifiedName}'; rename the type or its namespace");
        }
        ObjectTypes = localTypes.Where(type => type.IsObject).OrderBy(type => Depth(type)).ThenBy(type => type.QualifiedName, StringComparer.Ordinal).ToList();
        foreach (var type in ObjectTypes)
        {
            if (type.Name != "Object" && (Resolve(type.BaseName, type) is not CppType parent || !parent.IsObject))
                throw new InvalidDataException($"{type.File}:{type.Line}: unresolved Object base '{type.BaseName}'; supply the binding manifest of the module that declares it");
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
        string prefix = ImportedNamespaces.TryGetValue(type.QualifiedName, out string? imported) ? imported : ManagedNamespace;
        //C++ 根命名空间与模块短名一致时并入托管根命名空间，保留其余子命名空间。
        string name = type.QualifiedName;
        string root = prefix[(prefix.LastIndexOf('.') + 1)..];
        if (name.StartsWith(root + "::", StringComparison.Ordinal)) name = name[(root.Length + 2)..];
        return "global::" + prefix + "." + name.Replace("::", ".");
    }
    internal bool IsImported(CppType type) => ImportedNamespaces.ContainsKey(type.QualifiedName);
    internal IEnumerable<CppMember> ExportedMembers(CppType type) => type.Members.Where(member => member.Access == "public"
        && !member.IgnoreBinding && !member.IsTemplate && !LifecycleNames.Contains(member.Name));
    internal void WriteManifest(string path)
    {
        var manifest = new BindingManifest { ManagedNamespace = ManagedNamespace, Types = LocalTypes.OrderBy(type => type.QualifiedName, StringComparer.Ordinal).ToList() };
        string text = JsonSerializer.Serialize(manifest, new JsonSerializerOptions { WriteIndented = true }) + "\n";
        if (!File.Exists(path) || File.ReadAllText(path) != text) File.WriteAllText(path, text);
    }
}
