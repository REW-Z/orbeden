using System.Security.Cryptography;
using System.Text;

namespace OrbedenMetaGen;

internal sealed record BindingCall(CppMember Member, string Operation, int ParameterCount, string Name, BindingValue Result, List<(CppParameter Parameter, BindingValue Value)> Parameters);

internal sealed class BindingGenerator(BindingModel model, BindingTypes types)
{
    private readonly Dictionary<CppType, List<BindingCall>> calls = [];
    private readonly Dictionary<CppType, ulong> signatures = [];
    private static string Symbol(CppType type) => type.QualifiedName.Replace("::", "_");

    /// <summary>生成共享 schema 的类型化入口与托管包装。</summary>
    internal void Generate(string sourceRoot, string outputDirectory)
    {
        foreach (CppType type in model.ObjectTypes)
        {
            List<BindingCall> entries = [];
            foreach (CppMember member in model.ExportedMembers(type).OrderBy(member => member.Name, StringComparer.Ordinal).ThenBy(member => string.Join(",", member.Parameters.Select(parameter => parameter.Type)), StringComparer.Ordinal))
            {
                if (member.FixedArray) throw new InvalidDataException($"{type.File}:{member.Line}: {type.QualifiedName}.{member.Name}: fixed C arrays are not supported; use List<> or exclude with ORBEDEN_BIND_IGNORE");
                BindingValue result = types.Resolve(member.Type, type);
                if (!member.IsMethod)
                {
                    if (member.Getter != "None") entries.Add(new(member, "get", 0, member.Name, result, []));
                    if (!member.Type.StartsWith("const ", StringComparison.Ordinal) && member.Setter != "None")
                        entries.Add(new(member, "set", 1, member.Name, types.Resolve("void", type), [(new("value", member.Type, ""), result)]));
                    continue;
                }
                int minimum = member.Parameters.FindIndex(parameter => parameter.DefaultValue.Length != 0);
                if (minimum < 0) minimum = member.Parameters.Count;
                for (int length = minimum; length <= member.Parameters.Count; ++length)
                {
                    List<(CppParameter, BindingValue)> parameters = [];
                    foreach (CppParameter parameter in member.Parameters.Take(length))
                    {
                        string valueType = parameter.Name == member.Buffer ? parameter.Type.TrimEnd('*') : parameter.Type;
                        BindingValue value = types.Resolve(valueType, type);
                        if (value.Kind == "recordptr") throw new InvalidDataException($"{type.File}:{member.Line}: borrowed value pointers are only supported as return snapshots");
                        if (parameter.Name == member.Buffer && value.IsEncoded)
                            throw new InvalidDataException($"{type.File}:{member.Line}: {type.QualifiedName}.{member.Name}: buffer '{parameter.Name}' must be a blittable scalar, enum or value struct, got '{parameter.Type}'");
                        parameters.Add((parameter, value));
                    }
                    entries.Add(new(member, "call", length, member.Name, result, parameters));
                }
            }
            foreach (IGrouping<string, BindingCall> conflict in entries.Where(call => call.Operation == "call")
                .GroupBy(call => call.Member.Name + "|" + string.Join(",", call.Parameters.Select(parameter => parameter.Value.Managed)), StringComparer.Ordinal)
                .Where(group => group.Select(call => call.Member).Distinct().Count() > 1))
            {
                CppMember overload = conflict.Skip(1).First().Member;
                throw new InvalidDataException($"{type.File}:{overload.Line}: {type.QualifiedName}.{overload.Name}: overloads with default arguments produce the same managed signature; remove or reorder the defaults");
            }
            calls.Add(type, entries);
            string schema = "Bindings-v1|" + type.QualifiedName + "|" + type.BaseName + "|" + string.Join("|", entries.Select(call => call.Operation + ":" + call.Name + ":" + DescribeValue(call.Result, []) + ":" + call.Member.IsStatic + ":" + call.Member.IsConst + ":" + call.Member.Changed + ":" + call.Member.Getter + ":" + call.Member.Setter + ":" + call.Member.Buffer + ":" + call.Member.Count + ":" + string.Join(",", call.Parameters.Select(parameter => parameter.Parameter.Type + "=" + parameter.Parameter.DefaultValue + ":" + DescribeValue(parameter.Value, [])))));
            signatures.Add(type, BitConverter.ToUInt64(SHA256.HashData(Encoding.UTF8.GetBytes(schema))));
        }
        string cpp = GenerateCpp(sourceRoot);
        string cs = GenerateCSharp();
        Directory.CreateDirectory(outputDirectory);
        WriteChanged(Path.Combine(outputDirectory, "Bindings.Generated.cpp"), cpp);
        WriteChanged(Path.Combine(outputDirectory, "Bindings.Generated.cs"), cs);
        model.WriteManifest(Path.Combine(outputDirectory, "Bindings.Manifest.json"));
    }

    /// <summary>把递归值布局和枚举定义纳入签名，拒绝混用不同生成版本。</summary>
    private string DescribeValue(BindingValue value, HashSet<string> visiting)
    {
        string schema = value.Kind + "(" + value.Cpp + "," + value.WireCpp + "," + value.WireManaged + ")";
        if (!visiting.Add(value.Cpp)) return schema;
        if (value.Element != null) schema += "[" + DescribeValue(value.Element, visiting) + "]";
        if (value.Declaration is CppType declaration)
        {
            if (value.Kind == "enum")
                schema += declaration.EnumBase + string.Join(";", declaration.EnumValues.Select(item => item.Key + "=" + item.Value));
            if (value.Kind == "record")
                schema += string.Join(";", model.ExportedMembers(declaration).Where(member => !member.IsMethod && !member.IsStatic)
                    .Select(member => member.Name + ":" + DescribeValue(types.Resolve(member.Type, declaration), visiting)));
        }
        visiting.Remove(value.Cpp);
        return schema;
    }
    private string GenerateCpp(string sourceRoot)
    {
        StringBuilder output = new("// <auto-generated />\n#include \"Runtime/Native/NativeBindings.h\"\n");
        foreach (string file in model.ObjectTypes.Concat(types.Values.Values.Where(value => value.Declaration != null).Select(value => value.Declaration!)).Where(type => !model.IsImported(type)).Select(type => type.File).Distinct().Order(StringComparer.Ordinal))
            output.AppendLine($"#include \"{Path.GetRelativePath(sourceRoot, file).Replace('\\', '/')}\"");
        output.AppendLine("namespace\n{");
        foreach (BindingValue value in types.Values.Values.Where(value => value.Kind != "scalar" || value.Cpp != "void"))
        {
            output.AppendLine($"void Write_{value.Key}(NativeBindingWriter& writer, {value.Cpp} const& value);");
            if (value.Kind != "recordptr") output.AppendLine($"{value.Cpp} Read_{value.Key}(NativeBindingReader& reader);");
        }
        foreach (BindingValue value in types.Values.Values.Where(value => value.Kind != "scalar" || value.Cpp != "void")) EmitCppCodec(output, value);
        foreach ((CppType type, List<BindingCall> entries) in calls)
        for (int index = 0; index < entries.Count; ++index) EmitCppCall(output, type, entries[index], index);
        output.AppendLine("}\n");
        string module = model.ManagedNamespace.Replace('.', '_');
        output.AppendLine($"void RegisterBindings_{module}()\n{{");
        foreach ((CppType type, List<BindingCall> entries) in calls)
        {
            output.AppendLine($"    static void* functions_{Symbol(type)}[] = {{ {string.Join(", ", Enumerable.Range(0, entries.Count).Select(index => $"reinterpret_cast<void*>(&Call_{Symbol(type)}_{index})"))}{(entries.Count == 0 ? "nullptr" : "")} }};");
            output.AppendLine($"    NativeBindings::Register({type.QualifiedName}::StaticType(), {signatures[type]}ULL, {{ functions_{Symbol(type)}, {entries.Count} }});");
        }
        output.AppendLine("}");
        return output.ToString();
    }

    private void EmitCppCodec(StringBuilder output, BindingValue value)
    {
        output.AppendLine($"void Write_{value.Key}(NativeBindingWriter& writer, {value.Cpp} const& value)\n{{");
        switch (value.Kind)
        {
            case "string": output.AppendLine($"    writer.Text(value{(value.Cpp == "StringId" ? ".GetPath()" : "")});"); break;
            case "array": output.AppendLine($"    writer.Scalar(static_cast<int32>(value.size()));\n    for (const auto& item : value) Write_{value.Element!.Key}(writer, item);"); break;
            case "record":
                foreach (CppMember field in model.ExportedMembers(value.Declaration!).Where(member => !member.IsMethod && !member.IsStatic))
                    output.AppendLine($"    Write_{types.Resolve(field.Type, value.Declaration!).Key}(writer, value.{field.Name});");
                break;
            case "recordptr": output.AppendLine($"    writer.Scalar(static_cast<uint8>(value != nullptr));\n    if (value) Write_{value.Element!.Key}(writer, *value);"); break;
            case "object": output.AppendLine("    writer.Scalar(NativeBindings::Id(value));"); break;
            case "ref": output.AppendLine("    writer.Scalar(NativeBindings::Id(value.Get()));"); break;
            default: output.AppendLine($"    writer.Scalar(static_cast<{value.WireCpp}>(value));"); break;
        }
        output.AppendLine("}");
        if (value.Kind == "recordptr") return;
        output.AppendLine($"{value.Cpp} Read_{value.Key}(NativeBindingReader& reader)\n{{");
        switch (value.Kind)
        {
            case "string": output.AppendLine($"    return {value.Cpp}(reader.Text());"); break;
            case "array": output.AppendLine($"    int32 count = reader.Count();\n    {value.Cpp} value; value.reserve(count);\n    for (int32 index = 0; index < count; ++index) value.push_back(Read_{value.Element!.Key}(reader));\n    return value;"); break;
            case "record":
                output.AppendLine($"    {value.Cpp} value{{}};");
                foreach (CppMember field in model.ExportedMembers(value.Declaration!).Where(member => !member.IsMethod && !member.IsStatic))
                    output.AppendLine($"    value.{field.Name} = Read_{types.Resolve(field.Type, value.Declaration!).Key}(reader);");
                output.AppendLine("    return value;"); break;
            case "object": output.AppendLine($"    return NativeBindings::Optional<{value.Declaration!.QualifiedName}>(reader.Scalar<int32>());"); break;
            case "ref": output.AppendLine($"    return {value.Cpp}(NativeBindings::Optional<{value.Declaration!.QualifiedName}>(reader.Scalar<int32>()));"); break;
            default: output.AppendLine($"    return static_cast<{value.Cpp}>(reader.Scalar<{value.WireCpp}>());"); break;
        }
        output.AppendLine("}");
    }

    private void EmitCppCall(StringBuilder output, CppType type, BindingCall call, int index)
    {
        List<string> signature = ["int32 objectId"];
        foreach (var parameter in call.Parameters)
            signature.Add(parameter.Parameter.Name == call.Member.Buffer ? $"const {parameter.Value.Cpp}* {parameter.Parameter.Name}" : $"{parameter.Value.WireCpp} {parameter.Parameter.Name}");
        if (call.Result.Cpp != "void") signature.Add($"{(call.Result.IsEncoded ? "NativeBindingBuffer" : call.Result.WireCpp)}* result");
        output.AppendLine($"NativeBindingStatus ORBEDEN_NATIVE_CALL Call_{Symbol(type)}_{index}({string.Join(", ", signature)})\n{{\n    try\n    {{");
        if (!call.Member.IsStatic) output.AppendLine($"        auto* instance = NativeBindings::Require<{type.QualifiedName}>(objectId);");
        if (call.Result.Cpp != "void") output.AppendLine("        if (!result) return NativeBindingStatus::InvalidArgument;");
        List<string> arguments = [];
        foreach (var parameter in call.Parameters)
        {
            string name = parameter.Parameter.Name;
            BindingValue value = parameter.Value;
            if (name == call.Member.Buffer)
            {
                output.AppendLine($"        if ({call.Member.Count} < 0 || ({call.Member.Count} != 0 && !{name})) return NativeBindingStatus::InvalidArgument;");
                arguments.Add(name); continue;
            }
            if (value.IsEncoded)
            { output.AppendLine($"        NativeBindingReader reader_{name}({name});\n        auto decoded_{name} = Read_{value.Key}(reader_{name});\n        reader_{name}.Complete();"); arguments.Add("decoded_" + name); }
            else arguments.Add(value.Kind switch
            {
                "object" => $"NativeBindings::Optional<{value.Declaration!.QualifiedName}>({name})",
                "ref" => $"{value.Cpp}(NativeBindings::Optional<{value.Declaration!.QualifiedName}>({name}))",
                "bool" or "enum" => $"static_cast<{value.Cpp}>({name})", _ => name
            });
        }
        string target = call.Member.IsStatic ? type.QualifiedName + "::" : "instance->";
        string expression = call.Operation switch
        {
            "get" => target + (call.Member.Getter is "" or "Direct" ? call.Name : call.Member.Getter + "()"),
            "set" => call.Member.Setter.Length == 0 ? $"{target}{call.Name} = {arguments[0]}" : $"{target}{call.Member.Setter}({arguments[0]})",
            _ => $"{target}{call.Name}({string.Join(", ", arguments)})"
        };
        if (call.Operation == "set" && call.Member.Setter.Length != 0)
        {
            CppMember? setter = type.Members.FirstOrDefault(member => member.IsMethod && member.Name == call.Member.Setter);
            if (setter?.Buffer.Length > 0)
                expression = $"{target}{call.Member.Setter}({arguments[0]}.data(), static_cast<int32>({arguments[0]}.size()))";
            if (setter?.Type == "bool") expression = $"if (!({expression})) return NativeBindingStatus::InvalidArgument";
        }
        if (call.Result.Cpp == "void")
        {
            output.AppendLine($"        {expression};");
            if (call.Operation == "set" && call.Member.Changed.Length != 0)
                output.AppendLine($"        {target}{call.Member.Changed}();");
        }
        else if (call.Result.IsEncoded) output.AppendLine($"        NativeBindingWriter writer; Write_{call.Result.Key}(writer, {expression}); *result = writer.Finish();");
        else output.AppendLine($"        *result = {call.Result.Kind switch { "object" => $"NativeBindings::Id({expression})", "ref" => $"NativeBindings::Id(({expression}).Get())", _ => $"static_cast<{call.Result.WireCpp}>({expression})" }};");
        output.AppendLine("        return NativeBindingStatus::Ok;\n    }\n    catch (const NativeBindingError& error) { return error.status; }\n    catch (...) { return NativeBindingStatus::InvocationFailed; }\n}");
    }

    private string GenerateCSharp()
    {
        StringBuilder output = new("// <auto-generated />\n#nullable enable\nusing System;\nusing System.Runtime.CompilerServices;\nusing Orbeden;\n");
        output.AppendLine($"namespace {model.ManagedNamespace}\n{{\ninternal static unsafe class GeneratedBindingCodecs\n{{");
        foreach (BindingValue value in types.Values.Values.Where(value => value.Kind != "scalar" || value.Cpp != "void")) EmitManagedCodec(output, value);
        output.AppendLine("}\ninternal static class GeneratedBindingRegistration\n{\n    [ModuleInitializer]\n    internal static void Register()\n    {");
        foreach (CppType type in model.ObjectTypes)
        {
            bool component = IsComponent(type);
            string factory = type.IsAbstract || type.Name is "Object" or "Component" or "Script" ? "null" : component ? $"(ens, pointer) => new {model.ManagedName(type)}(ens!, pointer)" : $"(ens, pointer) => new {model.ManagedName(type)}(pointer)";
            output.AppendLine($"        NativeBindingRuntime.Register(typeof({model.ManagedName(type)}), \"{type.Name}\", {signatures[type]}UL, {factory});");
        }
        output.AppendLine("    }\n}\n}");
        foreach (BindingValue value in types.Values.Values.Where(value => value.Declaration != null && value.Kind is "record" or "enum").DistinctBy(value => value.Cpp))
        {
            if (model.IsImported(value.Declaration!)) continue;
            string managed = model.ManagedName(value.Declaration)[8..];
            string scope = managed[..managed.LastIndexOf('.')]; string name = managed[(managed.LastIndexOf('.') + 1)..];
            output.AppendLine($"namespace {scope}\n{{");
            if (value.Kind == "enum")
            {
                output.AppendLine($"public enum {name} : {value.WireManaged}\n{{");
                foreach (var item in value.Declaration.EnumValues) output.AppendLine($"    {item.Key}{(item.Value.Length == 0 ? "" : " = " + item.Value.Replace("::", "."))},");
            }
            else
            {
                output.AppendLine($"public struct {name}\n{{");
                foreach (var member in model.ExportedMembers(value.Declaration).Where(member => !member.IsMethod && !member.IsStatic))
                    output.AppendLine($"    public {types.Resolve(member.Type, value.Declaration).Managed} @{member.Name};");
            }
            output.AppendLine("}\n}");
        }
        foreach (CppType type in model.ObjectTypes) EmitManagedClass(output, type);
        return output.ToString();
    }

    private void EmitManagedCodec(StringBuilder output, BindingValue value)
    {
        output.AppendLine($"    internal static void Write_{value.Key}(NativeBindingWriter writer, {value.Managed} value)\n    {{");
        switch (value.Kind)
        {
            case "string": output.AppendLine("        writer.Text(value);"); break;
            case "array": output.AppendLine($"        ArgumentNullException.ThrowIfNull(value); writer.Scalar(value.Length);\n        foreach (var item in value) Write_{value.Element!.Key}(writer, item);"); break;
            case "record":
                foreach (var field in model.ExportedMembers(value.Declaration!).Where(member => !member.IsMethod && !member.IsStatic)) output.AppendLine($"        Write_{types.Resolve(field.Type, value.Declaration!).Key}(writer, value.@{field.Name});");
                break;
            case "recordptr": output.AppendLine($"        writer.Scalar((byte)(value.HasValue ? 1 : 0));\n        if (value.HasValue) Write_{value.Element!.Key}(writer, value.Value);"); break;
            case "ref": case "object": output.AppendLine("        writer.Scalar(NativeBindingRuntime.GetObjectId(value));"); break;
            case "bool": output.AppendLine("        writer.Scalar((byte)(value ? 1 : 0));"); break;
            default: output.AppendLine($"        writer.Scalar(({value.WireManaged})value);"); break;
        }
        output.AppendLine("    }");
        output.AppendLine($"    internal static {value.Managed} Read_{value.Key}(ref NativeBindingReader reader)\n    {{");
        switch (value.Kind)
        {
            case "string": output.AppendLine("        return reader.Text();"); break;
            case "array": output.AppendLine($"        int length = reader.Count(); var value = new {value.Element!.Managed.TrimEnd('?')}[length];\n        for (int index = 0; index < length; ++index) value[index] = Read_{value.Element.Key}(ref reader);\n        return value;"); break;
            case "record":
                output.AppendLine($"        {value.Managed} value = new();");
                foreach (var field in model.ExportedMembers(value.Declaration!).Where(member => !member.IsMethod && !member.IsStatic)) output.AppendLine($"        value.@{field.Name} = Read_{types.Resolve(field.Type, value.Declaration!).Key}(ref reader);");
                output.AppendLine("        return value;"); break;
            case "recordptr": output.AppendLine($"        return reader.Scalar<byte>() != 0 ? Read_{value.Element!.Key}(ref reader) : null;"); break;
            case "ref": case "object": output.AppendLine($"        return NativeBindingRuntime.Wrap<{value.Managed.TrimEnd('?')}>(reader.Scalar<int>());"); break;
            case "bool": output.AppendLine("        return reader.Scalar<byte>() != 0;"); break;
            default: output.AppendLine($"        return ({value.Managed})reader.Scalar<{value.WireManaged}>();"); break;
        }
        output.AppendLine("    }");
    }

    private void EmitManagedClass(StringBuilder output, CppType type)
    {
        string name = model.ManagedName(type)[8..]; string scope = name[..name.LastIndexOf('.')]; name = name[(name.LastIndexOf('.') + 1)..];
        CppType? parent = model.Resolve(type.BaseName, type);
        bool foundation = type.Name is "Object" or "Component" or "Script";
        if (type.IsUnique) output.AppendLine($"namespace {scope} {{ [UniqueComponent] public partial class {name} {{ }} }}");
        output.AppendLine($"namespace {scope}\n{{\n[NativeBinding(\"{type.Name}\")]\npublic {(type.IsAbstract ? "abstract " : type.IsFinal ? "sealed " : "")}unsafe partial class {name}{(foundation || parent == null ? "" : " : " + model.ManagedName(parent))}\n{{");
        if (!foundation)
            output.AppendLine(IsComponent(type) ? $"    {(type.IsFinal ? "internal" : "protected internal")} {name}(Ens ens, IntPtr pointer) : base(ens, pointer) {{ }}" : $"    {(type.IsFinal ? "internal" : "protected internal")} {name}(IntPtr pointer) : base(pointer) {{ }}");
        List<BindingCall> entries = calls[type];
        foreach (var group in entries.Select((call, index) => (call, index)).GroupBy(item => item.call.Member))
        {
            if (!group.Key.IsMethod)
            {
                BindingValue value = types.Resolve(group.Key.Type, type);
                output.AppendLine($"    public {(HidesMember(type, group.Key.Name) ? "new " : "")}{(group.Key.IsStatic ? "static " : "")}{value.Managed} @{group.Key.Name}\n    {{");
                foreach (var item in group) { output.AppendLine($"        {item.call.Operation}\n        {{"); EmitManagedCall(output, type, item.call, item.index); output.AppendLine("        }"); }
                output.AppendLine("    }");
            }
            else foreach (var item in group)
            {
                var parameters = item.call.Parameters.Where(parameter => parameter.Parameter.Name != group.Key.Count).Select(parameter => $"{(parameter.Parameter.Name == group.Key.Buffer ? "ReadOnlySpan<" + parameter.Value.Managed + ">" : parameter.Value.Managed)} @{parameter.Parameter.Name}");
                output.AppendLine($"    public {(HidesMember(type, group.Key.Name) ? "new " : "")}{(group.Key.IsStatic ? "static " : "")}{item.call.Result.Managed} @{group.Key.Name}({string.Join(", ", parameters)})\n    {{");
                EmitManagedCall(output, type, item.call, item.index); output.AppendLine("    }");
            }
        }
        output.AppendLine("}\n}");
    }

    private void EmitManagedCall(StringBuilder output, CppType type, BindingCall call, int index)
    {
        string codecs = "global::" + model.ManagedNamespace + ".GeneratedBindingCodecs";
        List<string> wireTypes = ["int"]; List<string> arguments = [call.Member.IsStatic ? "0" : "InstanceId"];
        int fixedBlocks = 0;
        foreach (var parameter in call.Parameters)
        {
            string name = parameter.Parameter.Name; BindingValue value = parameter.Value;
            if (name == call.Member.Count) { wireTypes.Add(value.WireManaged); arguments.Add("@" + call.Member.Buffer + ".Length"); continue; }
            if (name == call.Member.Buffer)
            { output.AppendLine($"        fixed ({value.WireManaged}* pointer_{name} = @{name})\n        {{"); ++fixedBlocks; wireTypes.Add(value.WireManaged + "*"); arguments.Add("pointer_" + name); continue; }
            wireTypes.Add(value.WireManaged);
            if (value.IsEncoded)
            {
                output.AppendLine($"        var writer_{name} = new NativeBindingWriter();\n        {codecs}.Write_{value.Key}(writer_{name}, @{name});\n        byte[] bytes_{name} = writer_{name}.ToArray();\n        fixed (byte* pointer_{name} = bytes_{name})\n        {{"); ++fixedBlocks;
                arguments.Add($"new NativeBindingSlice(pointer_{name}, bytes_{name}.Length)");
            }
            else arguments.Add(value.Kind switch { "ref" or "object" => "NativeBindingRuntime.GetObjectId(@" + name + ")", "bool" => $"(byte)(@{name} ? 1 : 0)", "enum" => $"({value.WireManaged})@{name}", _ => "@" + name });
        }
        bool returns = call.Result.Cpp != "void";
        if (returns)
        {
            string wire = call.Result.IsEncoded ? "NativeBindingBuffer" : call.Result.WireManaged;
            output.AppendLine($"        {wire} result = default;"); wireTypes.Add(wire + "*"); arguments.Add("&result");
        }
        wireTypes.Add("NativeBindingStatus");
        output.AppendLine($"        var callback = (delegate* unmanaged[Cdecl]<{string.Join(", ", wireTypes)}>)NativeBindingRuntime.GetFunction(typeof({model.ManagedName(type)}), {index}{(call.Member.IsStatic ? "" : ", this")});");
        output.AppendLine($"        NativeBindingRuntime.Check(callback({string.Join(", ", arguments)}));");
        if (returns)
        {
            if (call.Result.IsEncoded)
                output.AppendLine($"        try\n        {{\n            var reader = new NativeBindingReader(result.Span);\n            var decoded = {codecs}.Read_{call.Result.Key}(ref reader); reader.Complete(); return decoded;\n        }}\n        finally {{ NativeBindingRuntime.Release(result); }}");
            else output.AppendLine($"        return {call.Result.Kind switch { "bool" => "result != 0", "ref" or "object" => $"NativeBindingRuntime.Wrap<{call.Result.Managed.TrimEnd('?')}>(result)", _ => $"({call.Result.Managed})result" }};");
        }
        for (int count = 0; count < fixedBlocks; ++count) output.AppendLine("        }");
    }

    private bool HidesMember(CppType type, string name)
    {
        for (CppType? parent = model.Resolve(type.BaseName, type); parent != null; parent = model.Resolve(parent.BaseName, parent))
            if (model.ExportedMembers(parent).Any(member => member.Name == name)) return true;
        return false;
    }

    private bool IsComponent(CppType type)
    {
        for (CppType? current = type; current != null; current = model.Resolve(current.BaseName, current))
            if (current.Name == "Component") return true;
        return false;
    }
    private static void WriteChanged(string path, string text)
    {
        if (!File.Exists(path) || File.ReadAllText(path) != text) File.WriteAllText(path, text);
    }
}
