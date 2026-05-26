using System.Text;
using System.Text.RegularExpressions;

//解析命令行参数
if (args.Length < 2)
{
    Console.Error.WriteLine("Usage: OrbedenMetaGen <sourceRoot> <outputDir>");
    return 1;
}

var sourceRoot = Path.GetFullPath(args[0]);
var outputDir = Path.GetFullPath(args[1]);

if (!Directory.Exists(sourceRoot))
{
    Console.Error.WriteLine($"Source root does not exist: {sourceRoot}");
    return 1;
}

Directory.CreateDirectory(outputDir);

//扫描所有 Runtime 头文件
var classes = new List<ClassInfo>();
foreach (var file in Directory.EnumerateFiles(sourceRoot, "*.h", SearchOption.AllDirectories).OrderBy(path => path))
{
    var text = File.ReadAllText(file);
    classes.AddRange(ParseClasses(text, file));
}

if (classes.Count == 0)
{
    Console.Error.WriteLine("No OBJECT_TYPE_DECLARE classes were found.");
    return 1;
}

//校验字段持久化类型
var errors = new List<string>();
foreach (var classInfo in classes)
{
    foreach (var field in classInfo.Fields)
    {
        field.Persistent = IsPersistentField(classInfo.Name, field.Name);
        field.Kind = GetFieldKind(field.Type);

        if (field.Persistent && field.Kind is null)
        {
            errors.Add($"{classInfo.Name}.{field.Name}: unsupported field type '{field.Type}'");
        }
    }
}

if (errors.Count > 0)
{
    foreach (var error in errors)
    {
        Console.Error.WriteLine(error);
    }

    return 1;
}

//生成 C++ 反射注册代码
var generatedPath = Path.Combine(outputDir, "Reflection.Generated.cpp");
File.WriteAllText(generatedPath, GenerateCpp(classes), new UTF8Encoding(false));
Console.WriteLine($"Generated {generatedPath}");
return 0;

//解析包含 OBJECT_TYPE_DECLARE 的 C++ 类
static IEnumerable<ClassInfo> ParseClasses(string text, string file)
{
    var results = new List<ClassInfo>();
    var classRegex = new Regex(@"\bclass\s+(?<name>\w+)(?:\s*:\s*(?:(?:public|private|protected)\s+)?(?<base>\w+))?\s*\{", RegexOptions.Multiline);

    foreach (Match match in classRegex.Matches(text))
    {
        var openBrace = text.IndexOf('{', match.Index);
        var closeBrace = FindMatchingBrace(text, openBrace);
        if (closeBrace < 0) continue;

        var body = text.Substring(openBrace + 1, closeBrace - openBrace - 1);
        var className = match.Groups["name"].Value;
        if (!body.Contains($"OBJECT_TYPE_DECLARE({className})", StringComparison.Ordinal)) continue;

        results.Add(new ClassInfo
        {
            Name = className,
            BaseName = match.Groups["base"].Success ? match.Groups["base"].Value : "",
            File = file,
            Fields = ParseFields(body),
            Methods = ParseMethods(body, className),
        });
    }

    return results;
}

//查找类体结束花括号
static int FindMatchingBrace(string text, int openBrace)
{
    var depth = 0;
    for (var index = openBrace; index < text.Length; ++index)
    {
        if (text[index] == '{') depth++;
        if (text[index] == '}')
        {
            depth--;
            if (depth == 0) return index;
        }
    }

    return -1;
}

//解析类中的字段声明
static List<FieldInfo> ParseFields(string body)
{
    var fields = new List<FieldInfo>();
    var access = "private";
    var statement = new StringBuilder();
    var braceDepth = 0;
    var angleDepth = 0;

    foreach (var rawLine in body.Replace("\r\n", "\n").Split('\n'))
    {
        var line = StripLineComment(rawLine).Trim();
        if (line.Length == 0) continue;

        if (line is "public:" or "private:" or "protected:")
        {
            access = line[..^1];
            continue;
        }

        if (line.StartsWith("OBJECT_TYPE_DECLARE", StringComparison.Ordinal)) continue;
        if (line.StartsWith("friend ", StringComparison.Ordinal)) continue;
        if (line.StartsWith("using ", StringComparison.Ordinal)) continue;
        if (line.StartsWith("typedef ", StringComparison.Ordinal)) continue;

        foreach (var ch in line)
        {
            if (ch == '<') angleDepth++;
            if (ch == '>') angleDepth = Math.Max(0, angleDepth - 1);
            if (ch == '{' && angleDepth == 0) braceDepth++;
            if (ch == '}' && angleDepth == 0) braceDepth = Math.Max(0, braceDepth - 1);
        }

        if (statement.Length > 0) statement.Append(' ');
        statement.Append(line);

        if (!line.EndsWith(';')) continue;

        var text = statement.ToString().Trim();
        statement.Clear();
        braceDepth = 0;
        angleDepth = 0;

        var field = ParseFieldStatement(text, access);
        if (field is not null)
        {
            fields.Add(field);
        }
    }

    return fields;
}

//解析类中的可反射方法
static List<MethodInfo> ParseMethods(string body, string className)
{
    var methods = new List<MethodInfo>();
    var cleanBody = string.Join('\n', body.Replace("\r\n", "\n").Split('\n').Select(StripLineComment));
    var methodRegex = new Regex(@"(?<ret>[A-Za-z_][\w:<>\s*&]*?)\s+(?<name>[A-Za-z_]\w*)\s*\((?<params>[^()]*)\)\s*(?<const>const)?\s*(?:override\s*)?(?:;|\{)", RegexOptions.Multiline);

    var index = 0;
    foreach (Match match in methodRegex.Matches(cleanBody))
    {
        var returnType = NormalizeType(match.Groups["ret"].Value);
        var name = match.Groups["name"].Value;

        if (name == className) continue;
        if (returnType.Contains("static ", StringComparison.Ordinal)) continue;
        if (returnType.Contains("template", StringComparison.Ordinal)) continue;
        if (returnType.EndsWith("operator", StringComparison.Ordinal)) continue;
        if (name is "if" or "while" or "for" or "switch" or "return") continue;

        returnType = NormalizeValueType(returnType);
        var returnKind = GetValueKind(returnType);
        if (returnKind is null) continue;

        var parameters = ParseParameters(match.Groups["params"].Value);
        if (parameters is null) continue;

        methods.Add(new MethodInfo
        {
            Name = name,
            ReturnType = returnType,
            ReturnKind = returnKind,
            Parameters = parameters,
            InvokerIndex = index++,
        });
    }

    return methods;
}

//解析方法参数列表
static List<ParameterInfo>? ParseParameters(string text)
{
    var parameters = new List<ParameterInfo>();
    var trimmed = text.Trim();
    if (trimmed.Length == 0 || trimmed == "void") return parameters;

    var parts = SplitParameters(trimmed);
    for (var index = 0; index < parts.Count; ++index)
    {
        var content = parts[index].Trim();
        var equals = content.IndexOf('=');
        if (equals >= 0)
        {
            content = content[..equals].Trim();
        }

        var match = Regex.Match(content, @"^(?<type>.+?)\s+(?<name>\w+)$");
        if (!match.Success) return null;

        var rawType = NormalizeType(match.Groups["type"].Value);
        if (rawType.Contains('*')) return null;
        if (rawType.Contains('&') && !rawType.StartsWith("const ", StringComparison.Ordinal)) return null;

        var valueType = NormalizeValueType(rawType);
        var kind = GetValueKind(valueType);
        if (kind is null || kind.CppName == "Reflection::ValueKind::Empty") return null;

        parameters.Add(new ParameterInfo
        {
            Name = match.Groups["name"].Value,
            Type = valueType,
            Kind = kind,
        });
    }

    return parameters;
}

//按模板尖括号层级拆分参数
static List<string> SplitParameters(string text)
{
    var result = new List<string>();
    var start = 0;
    var angleDepth = 0;
    for (var index = 0; index < text.Length; ++index)
    {
        if (text[index] == '<') angleDepth++;
        if (text[index] == '>') angleDepth = Math.Max(0, angleDepth - 1);
        if (text[index] == ',' && angleDepth == 0)
        {
            result.Add(text[start..index]);
            start = index + 1;
        }
    }

    result.Add(text[start..]);
    return result;
}

//去掉行注释，避免影响简单语法扫描
static string StripLineComment(string line)
{
    var index = line.IndexOf("//", StringComparison.Ordinal);
    return index < 0 ? line : line[..index];
}

//解析单条字段语句
static FieldInfo? ParseFieldStatement(string statement, string access)
{
    if (!statement.EndsWith(';')) return null;
    if (statement.Contains('(')) return null;
    if (statement.StartsWith("static ", StringComparison.Ordinal)) return null;

    var content = statement[..^1].Trim();
    var equals = content.IndexOf('=');
    if (equals >= 0)
    {
        content = content[..equals].Trim();
    }

    content = Regex.Replace(content, @"\s+", " ");
    var match = Regex.Match(content, @"^(?<type>.+?)\s+(?<name>\w+)$");
    if (!match.Success) return null;

    return new FieldInfo
    {
        Name = match.Groups["name"].Value,
        Type = NormalizeType(match.Groups["type"].Value),
        Access = access,
    };
}

//规范化 C++ 类型空白
static string NormalizeType(string type)
{
    return Regex.Replace(type.Trim(), @"\s*([*&])\s*", "$1");
}

//规范化反射值类型，去掉 const/ref/virtual 等修饰
static string NormalizeValueType(string type)
{
    var result = NormalizeType(type);
    if (result.StartsWith("virtual ", StringComparison.Ordinal))
    {
        result = result["virtual ".Length..];
    }

    if (result.StartsWith("const ", StringComparison.Ordinal))
    {
        result = result["const ".Length..];
    }

    if (result.EndsWith('&'))
    {
        result = result[..^1];
    }

    return result.Trim();
}

//判断字段是否进入持久化
static bool IsPersistentField(string className, string fieldName)
{
    if (className == "Object" && (fieldName == "instanceId" || fieldName == "ownerWorld")) return false;
    if (className == "Component" && fieldName == "owner") return false;
    if (className == "EnsComponent")
    {
        return fieldName is "name" or "localPosition" or "localRotation" or "localScale";
    }

    return true;
}

//映射字段类型到 C++ FieldKind
static FieldKindInfo? GetFieldKind(string type)
{
    return NormalizeValueType(type) switch
    {
        "bool" => new FieldKindInfo("Reflection::FieldKind::Bool"),
        "int32" => new FieldKindInfo("Reflection::FieldKind::Int32"),
        "uint32" => new FieldKindInfo("Reflection::FieldKind::UInt32"),
        "TypeId" => new FieldKindInfo("Reflection::FieldKind::UInt32"),
        "uint64" => new FieldKindInfo("Reflection::FieldKind::UInt64"),
        "float32" => new FieldKindInfo("Reflection::FieldKind::Float32"),
        "std::string" => new FieldKindInfo("Reflection::FieldKind::String"),
        "StringId" => new FieldKindInfo("Reflection::FieldKind::StringId"),
        "vector3" => new FieldKindInfo("Reflection::FieldKind::Vector3"),
        "quaternion" => new FieldKindInfo("Reflection::FieldKind::Quaternion"),
        "EnsId" => new FieldKindInfo("Reflection::FieldKind::EnsId"),
        _ => null,
    };
}

//映射方法参数和返回值到 C++ ValueKind
static ValueKindInfo? GetValueKind(string type)
{
    return NormalizeValueType(type) switch
    {
        "void" => new ValueKindInfo("Reflection::ValueKind::Empty"),
        "bool" => new ValueKindInfo("Reflection::ValueKind::Bool"),
        "int32" => new ValueKindInfo("Reflection::ValueKind::Int32"),
        "uint32" => new ValueKindInfo("Reflection::ValueKind::UInt32"),
        "TypeId" => new ValueKindInfo("Reflection::ValueKind::UInt32"),
        "uint64" => new ValueKindInfo("Reflection::ValueKind::UInt64"),
        "float32" => new ValueKindInfo("Reflection::ValueKind::Float32"),
        "std::string" => new ValueKindInfo("Reflection::ValueKind::String"),
        "StringId" => new ValueKindInfo("Reflection::ValueKind::StringId"),
        "vector3" => new ValueKindInfo("Reflection::ValueKind::Vector3"),
        "quaternion" => new ValueKindInfo("Reflection::ValueKind::Quaternion"),
        "EnsId" => new ValueKindInfo("Reflection::ValueKind::EnsId"),
        _ => null,
    };
}

//生成 C++ 反射注册源码
static string GenerateCpp(List<ClassInfo> classes)
{
    var output = new StringBuilder();
    output.AppendLine("// <auto-generated>");
    output.AppendLine("// Generated by Tools/OrbedenMetaGen. Do not edit by hand.");
    output.AppendLine("// </auto-generated>");
    output.AppendLine();
    output.AppendLine("#include \"Runtime/Reflection.h\"");
    output.AppendLine("#include \"Runtime/Ens.h\"");
    output.AppendLine("#include \"Runtime/EnsId.h\"");
    output.AppendLine("#include \"Runtime/Object.h\"");
    output.AppendLine();
    output.AppendLine("class ReflectionGeneratedAccess");
    output.AppendLine("{");
    output.AppendLine("public:");

    foreach (var classInfo in classes)
    {
        //生成字段 getter/setter 和方法 invoker
        foreach (var field in classInfo.Fields.Where(field => field.Persistent))
        {
            output.AppendLine($"    //读取 {classInfo.Name}.{field.Name} 字段");
            output.AppendLine($"    static std::string Get_{classInfo.Name}_{field.Name}(Object* object)");
            output.AppendLine("    {");
            output.AppendLine($"        {classInfo.Name}* instance = static_cast<{classInfo.Name}*>(object);");
            output.AppendLine($"        return Reflection::ToXmlValue(instance->{field.Name});");
            output.AppendLine("    }");
            output.AppendLine();
            output.AppendLine($"    //写入 {classInfo.Name}.{field.Name} 字段");
            output.AppendLine($"    static bool Set_{classInfo.Name}_{field.Name}(Object* object, const std::string& value)");
            output.AppendLine("    {");
            output.AppendLine($"        {classInfo.Name}* instance = static_cast<{classInfo.Name}*>(object);");
            output.AppendLine($"        return Reflection::SetFromXmlValue(instance->{field.Name}, value);");
            output.AppendLine("    }");
            output.AppendLine();
        }

        foreach (var method in classInfo.Methods)
        {
            var invokerName = $"Invoke_{classInfo.Name}_{method.Name}_{method.InvokerIndex}";
            output.AppendLine($"    //调用 {classInfo.Name}.{method.Name} 方法");
            output.AppendLine($"    static Reflection::Value {invokerName}(Object* object, const List<Reflection::Value>& args, bool& success)");
            output.AppendLine("    {");
            output.AppendLine("        success = false;");
            output.AppendLine($"        {classInfo.Name}* instance = static_cast<{classInfo.Name}*>(object);");
            output.AppendLine($"        if (!instance || args.size() != {method.Parameters.Count}) return Reflection::Value();");
            output.AppendLine();

            for (var index = 0; index < method.Parameters.Count; ++index)
            {
                var parameter = method.Parameters[index];
                output.AppendLine($"        {parameter.Type} arg{index}{{}};");
                output.AppendLine($"        if (!args[{index}].TryGet(arg{index})) return Reflection::Value();");
            }

            var argList = string.Join(", ", Enumerable.Range(0, method.Parameters.Count).Select(index => $"arg{index}"));
            if (method.ReturnType == "void")
            {
                output.AppendLine($"        instance->{method.Name}({argList});");
                output.AppendLine("        success = true;");
                output.AppendLine("        return Reflection::Value();");
            }
            else
            {
                output.AppendLine($"        auto result = instance->{method.Name}({argList});");
                output.AppendLine("        success = true;");
                output.AppendLine("        return Reflection::Value(result);");
            }

            output.AppendLine("    }");
            output.AppendLine();
        }
    }

    output.AppendLine("};");
    output.AppendLine();
    output.AppendLine("namespace Reflection");
    output.AppendLine("{");
    output.AppendLine("    //注册生成的反射元数据");
    output.AppendLine("    void RegisterGeneratedReflection()");
    output.AppendLine("    {");
    output.AppendLine("        static bool registered = false;");
    output.AppendLine("        if (registered) return;");
    output.AppendLine("        registered = true;");
    output.AppendLine();

    foreach (var classInfo in classes)
    {
        //注册字段元数据
        output.AppendLine($"        RegisterTypeFields({classInfo.Name}::StaticType(),");
        output.AppendLine("            {");

        foreach (var field in classInfo.Fields)
        {
            var kind = field.Kind?.CppName ?? "Reflection::FieldKind::Unsupported";
            var persistent = field.Persistent ? "true" : "false";
            var getter = field.Persistent ? $"ReflectionGeneratedAccess::Get_{classInfo.Name}_{field.Name}" : "nullptr";
            var setter = field.Persistent ? $"ReflectionGeneratedAccess::Set_{classInfo.Name}_{field.Name}" : "nullptr";
            output.AppendLine($"                FieldInfo(\"{field.Name}\", \"{field.Type}\", {kind}, {persistent}, {getter}, {setter}),");
        }

        output.AppendLine("            });");
        output.AppendLine();

        //注册方法元数据
        output.AppendLine($"        RegisterTypeMethods({classInfo.Name}::StaticType(),");
        output.AppendLine("            {");

        foreach (var method in classInfo.Methods)
        {
            var parameters = method.Parameters.Count == 0
                ? "List<ParameterInfo>()"
                : "List<ParameterInfo>{ " + string.Join(", ", method.Parameters.Select(parameter => $"ParameterInfo(\"{parameter.Name}\", \"{parameter.Type}\", {parameter.Kind.CppName})")) + " }";

            output.AppendLine($"                MethodInfo(\"{method.Name}\", \"{method.ReturnType}\", {method.ReturnKind!.CppName}, {parameters}, ReflectionGeneratedAccess::Invoke_{classInfo.Name}_{method.Name}_{method.InvokerIndex}),");
        }

        output.AppendLine("            });");
        output.AppendLine();
    }

    output.AppendLine("    }");
    output.AppendLine("}");
    return output.ToString();
}

sealed class ClassInfo
{
    public string Name { get; set; } = "";
    public string BaseName { get; set; } = "";
    public string File { get; set; } = "";
    public List<FieldInfo> Fields { get; set; } = [];
    public List<MethodInfo> Methods { get; set; } = [];
}

sealed class FieldInfo
{
    public string Name { get; set; } = "";
    public string Type { get; set; } = "";
    public string Access { get; set; } = "";
    public bool Persistent { get; set; }
    public FieldKindInfo? Kind { get; set; }
}

sealed class FieldKindInfo(string cppName)
{
    public string CppName { get; } = cppName;
}

sealed class MethodInfo
{
    public string Name { get; set; } = "";
    public string ReturnType { get; set; } = "";
    public ValueKindInfo? ReturnKind { get; set; }
    public List<ParameterInfo> Parameters { get; set; } = [];
    public int InvokerIndex { get; set; }
}

sealed class ParameterInfo
{
    public string Name { get; set; } = "";
    public string Type { get; set; } = "";
    public ValueKindInfo Kind { get; set; } = new("Reflection::ValueKind::Empty");
}

sealed class ValueKindInfo(string cppName)
{
    public string CppName { get; } = cppName;
}
