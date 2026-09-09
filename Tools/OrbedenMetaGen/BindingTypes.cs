using System.Text.RegularExpressions;

namespace OrbedenMetaGen;

internal sealed record BindingValue(string Kind, string Cpp, string Managed, string WireCpp, string WireManaged, CppType? Declaration = null, BindingValue? Element = null)
{
    internal bool IsEncoded => Kind is "string" or "array" or "record" or "recordptr";
    internal string Key => Regex.Replace(Cpp, @"\W", "_");
}

internal sealed class BindingTypes(BindingModel model)
{
    internal Dictionary<string, BindingValue> Values { get; } = new(StringComparer.Ordinal);
    private static readonly Dictionary<string, (string Cpp, string Cs)> Scalars = new(StringComparer.Ordinal)
    {
        ["void"] = ("void", "void"), ["bool"] = ("uint8", "byte"),
        ["int8"] = ("int8", "sbyte"), ["uint8"] = ("uint8", "byte"), ["int16"] = ("int16", "short"), ["uint16"] = ("uint16", "ushort"),
        ["int"] = ("int32", "int"), ["int32"] = ("int32", "int"), ["uint32"] = ("uint32", "uint"), ["int64"] = ("int64", "long"), ["uint64"] = ("uint64", "ulong"),
        ["float"] = ("float32", "float"), ["float32"] = ("float32", "float"), ["double"] = ("float64", "double"), ["float64"] = ("float64", "double")
    };
    private static readonly HashSet<string> Builtins = ["vector2", "vector3", "color", "quaternion", "EnsId"];

    /// <summary>解析一个明确支持的 ABI 类型，不支持时给出可定位的错误。</summary>
    internal BindingValue Resolve(string text, CppType owner)
    {
        string type = text.Trim();
        if (type.StartsWith("const ", StringComparison.Ordinal)) type = type[6..];
        if (type.EndsWith('&')) type = type[..^1];
        if (Scalars.TryGetValue(type, out var scalar))
            return Remember(new(type == "bool" ? "bool" : "scalar", type, type == "bool" ? "bool" : scalar.Cs, scalar.Cpp, scalar.Cs));
        if (Builtins.Contains(type)) return Remember(new("builtin", type, "global::Orbeden." + type, type, "global::Orbeden." + type));
        if (type is "std::string" or "StringId") return Remember(new("string", type, "string", "NativeBindingSlice", "NativeBindingSlice"));
        foreach (string prefix in new[] { "List<", "std::vector<" })
        {
            if (!type.StartsWith(prefix, StringComparison.Ordinal) || !type.EndsWith('>')) continue;
            BindingValue element = Resolve(type[prefix.Length..^1], owner);
            return Remember(new("array", type, element.Managed + "[]", "NativeBindingSlice", "NativeBindingSlice", Element: element));
        }
        bool reference = type.StartsWith("Ref<", StringComparison.Ordinal) && type.EndsWith('>');
        bool pointer = type.EndsWith('*');
        string targetName = reference ? type[4..^1] : pointer ? type[..^1] : type;
        CppType? declaration = model.Resolve(targetName, owner);
        if (declaration == null) throw new InvalidDataException($"unsupported type '{text}'");
        if (declaration.IsObject)
        {
            if (!reference && !pointer) throw new InvalidDataException($"Object values must be pointers or Ref<T>: '{text}'");
            return Remember(new(reference ? "ref" : "object", reference ? "Ref<" + declaration.QualifiedName + ">" : declaration.QualifiedName + "*", model.ManagedName(declaration) + "?", "int32", "int", declaration));
        }
        if (declaration.Kind == "enum")
        {
            BindingValue underlying = Resolve(declaration.EnumBase, declaration);
            return Remember(new("enum", declaration.QualifiedName, model.ManagedName(declaration), underlying.WireCpp, underlying.WireManaged, declaration));
        }
        if (declaration.Kind != "struct") throw new InvalidDataException($"unsupported non-Object class/pointer '{text}'");
        if (pointer)
        {
            BindingValue element = Resolve(declaration.QualifiedName, owner);
            return Remember(new("recordptr", "const " + declaration.QualifiedName + "*", element.Managed + "?", "NativeBindingSlice", "NativeBindingSlice", declaration, element));
        }
        string key = declaration.QualifiedName;
        if (Values.TryGetValue(key, out var existing)) return existing;
        BindingValue record = Remember(new("record", key, model.ManagedName(declaration), "NativeBindingSlice", "NativeBindingSlice", declaration));
        foreach (CppMember field in model.ExportedMembers(declaration).Where(member => !member.IsMethod && !member.IsStatic))
        {
            if (field.FixedArray)
                throw new InvalidDataException($"{declaration.File}:{field.Line}: {declaration.QualifiedName}.{field.Name}: fixed C arrays are not supported; use List<> or exclude with ORBEDEN_BIND_IGNORE");
            Resolve(field.Type, declaration);
        }
        return record;
    }
    /// <summary>递归检查缓冲元素；普通值结构通过字段编码传输，不依赖两侧内存布局。</summary>
    internal bool IsBufferElement(BindingValue value, HashSet<string>? visiting = null)
    {
        if (value.Kind is "bool" or "builtin" or "enum") return true;
        if (value.Kind == "scalar") return value.Cpp != "void";
        if (value.Kind != "record" || value.Declaration is not CppType declaration || declaration.BaseName.Length != 0) return false;
        visiting ??= [];
        if (!visiting.Add(value.Cpp)) return false;
        bool supported = declaration.Members.Any(member => !member.IsMethod && !member.IsStatic)
            && !declaration.Members.Any(member => member.IsVirtual)
            && declaration.Members.Where(member => !member.IsMethod && !member.IsStatic).All(member =>
                member.Access == "public" && !member.IgnoreBinding && !member.FixedArray
                && IsBufferElement(Resolve(member.Type, declaration), visiting));
        visiting.Remove(value.Cpp);
        return supported;
    }

    private BindingValue Remember(BindingValue value) { Values.TryAdd(value.Cpp, value); return value; }
}
