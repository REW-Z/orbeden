using OrbedenMetaGen;
using System.Text;
using System.Text.RegularExpressions;

//规范化路径盘符大小写，保证不同 shell 下生成文本一致。
static string CanonicalPath(string path)
{
    string full = Path.GetFullPath(path);
    return full.Length >= 2 && full[1] == ':' ? char.ToUpperInvariant(full[0]) + full[1..] : full;
}

//解析命令行参数
if (args.Length < 2)
{
    Console.Error.WriteLine("Usage: OrbedenMetaGen <sourceRoot> <outputDir>");
    return 1;
}

var sourceRoot = CanonicalPath(args[0]);
var outputDir = CanonicalPath(args[1]);
var gameModule = args.Skip(2).Any(value => value == "--game-module");

if (!Directory.Exists(sourceRoot))
{
    Console.Error.WriteLine($"Source root does not exist: {sourceRoot}");
    return 1;
}

Directory.CreateDirectory(outputDir);
//同一输出目录的独立 C# / Native 构建串行生成，避免读取半套生成物。
using var generationLock = new GenerationLock(outputDir);

//扫描所有 Runtime 头文件
var classes = new List<ClassInfo>();
var declarations = new List<CppType>();
foreach (var file in Directory.EnumerateFiles(sourceRoot, "*.h", SearchOption.AllDirectories).Where(path => !Path.GetRelativePath(sourceRoot, path).Split(Path.DirectorySeparatorChar).Any(part => part is "ThirdParty" or "Build" or "Generated" or "obj" or "bin")).OrderBy(path => path, StringComparer.Ordinal))
{
    var text = File.ReadAllText(file);
    var parsed = CppDeclarations.Parse(text, file);
    declarations.AddRange(parsed);
    classes.AddRange(ParseClasses(parsed, file));
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
    if (classInfo.Name is "Script" or "Object" or "Component")
    {
        classInfo.Fields.Clear();
        classInfo.Methods.Clear();
    }
    foreach (var field in classInfo.Fields)
    {
        field.Persistent = IsPersistentField(classInfo.Name, field.Name)
            && (!gameModule || field.Access == "public" || field.ExplicitPersistent);
        field.ObjectRefTypeName = GetObjectRefTypeName(field.Type);
        field.Kind = field.FixedArray ? null : GetFieldKind(field.Type);

        if (field.Persistent && field.Kind is null)
        {
            string reason = field.FixedArray ? "fixed C arrays are not supported" : $"field type '{field.Type}' is unsupported";
            if (field.ExplicitPersistent)
            {
                errors.Add($"{classInfo.File}:{field.Line}: {classInfo.Name}.{field.Name}: explicitly serialized field: {reason}");
            }
            else
            {
                Console.Error.WriteLine($"warning: {classInfo.File}:{field.Line}: {classInfo.Name}.{field.Name}: public {reason} and will be ignored");
                field.Persistent = false;
            }
        }
    }
}

//反射与 Binding 共用导入后的类型模型，识别跨模块脚本基类。
BindingModel? bindings = null;
if (args.Contains("--bindings", StringComparer.Ordinal))
{
    string? Option(string key)
    {
        int index = Array.IndexOf(args, key);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }
    List<string> Imports()
    {
        List<string> paths = [];
        for (int index = 0; index + 1 < args.Length; ++index)
            if (args[index] == "--import") paths.Add(args[index + 1]);
        return paths;
    }
    try
    {
        bindings = new BindingModel(declarations, Option("--namespace") ?? (gameModule ? "Game.Native" : "Orbeden"), Imports());
    }
    catch (InvalidDataException exception) { Console.Error.WriteLine(exception.Message); return 1; }
}

//识别脚本继承关系并校验生命周期函数不能声明为 virtual。
var classByName = classes.ToDictionary(value => value.CppName, StringComparer.Ordinal);
bool IsScriptClass(ClassInfo value)
{
    if (bindings != null)
    {
        CppType type = bindings.Types[value.CppName];
        while (bindings.Resolve(type.BaseName, type) is CppType parent)
        {
            if (parent.QualifiedName is "Script" or "Orbeden::Script") return true;
            type = parent;
        }
        return false;
    }
    if (value.BaseName is "Script" or "Orbeden::Script") return true;
    string scope = value.CppName.Contains("::") ? value.CppName[..value.CppName.LastIndexOf("::", StringComparison.Ordinal)] + "::" : "";
    return (classByName.TryGetValue(scope + value.BaseName, out ClassInfo? baseClass) || classByName.TryGetValue(value.BaseName, out baseClass)) && IsScriptClass(baseClass);
}
foreach (ClassInfo classInfo in classes)
{
    classInfo.IsScript = IsScriptClass(classInfo);
    if (!classInfo.IsScript) continue;

    foreach (ScriptCallbackInfo callback in classInfo.ScriptCallbacks.Where(value => value.IsVirtual))
    {
        errors.Add($"{classInfo.File}:{callback.Line}: {classInfo.Name}.{callback.Name}: C++ script callbacks must not use virtual or override");
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

string? bindingModule = null;
//生成 Binding 类型清单时严格校验全部公开签名。
if (bindings != null)
{
    try
    {
        bindingModule = bindings.ManagedNamespace.Replace('.', '_');
        BindingTypes bindingTypes = new(bindings);
        foreach (CppType type in bindings.ObjectTypes)
        foreach (CppMember member in bindings.ExportedMembers(type))
        {
            try
            {
                bindingTypes.Resolve(member.Type, type);
                foreach (CppParameter parameter in member.Parameters)
                    bindingTypes.Resolve(parameter.Name == member.Buffer ? parameter.Type.TrimEnd('*') : parameter.Type, type);
            }
            catch (InvalidDataException exception) { errors.Add($"{type.File}:{member.Line}: {type.QualifiedName}.{member.Name}: {exception.Message}"); }
        }
        if (errors.Count != 0)
        {
            foreach (string error in errors) Console.Error.WriteLine(error);
            return 1;
        }
        new BindingGenerator(bindings, bindingTypes).Generate(sourceRoot, outputDir);
    }
    catch (InvalidDataException exception) { Console.Error.WriteLine(exception.Message); return 1; }
}
//生成 C++ 反射注册代码
var generatedPath = Path.Combine(outputDir, "Reflection.Generated.cpp");
string reflectionText = GenerateCpp(classes, sourceRoot, gameModule, bindingModule);
if (!File.Exists(generatedPath) || File.ReadAllText(generatedPath) != reflectionText)
    File.WriteAllText(generatedPath, reflectionText, new UTF8Encoding(false));
Console.WriteLine($"Generated {generatedPath}");
return 0;

//从公共词法模型投影反射支持的成员，不改变 Binding 的完整声明数据。
static IEnumerable<ClassInfo> ParseClasses(List<CppType> declarations, string file)
{
    foreach (CppType declaration in declarations.Where(type => type.IsObject))
    {
        ClassInfo result = new() { Name = declaration.Name, CppName = declaration.QualifiedName, BaseName = declaration.BaseName, File = file };
        foreach (CppMember member in declaration.Members)
        {
            if (!member.IsMethod)
            {
                if (!member.IsStatic)
                    result.Fields.Add(new FieldInfo { Name = member.Name, Type = NormalizeType(member.Type), Access = member.Access, ExplicitPersistent = member.Serialize, Changed = member.Changed, Line = member.Line, FixedArray = member.FixedArray });
                continue;
            }
            bool timed = member.Name is "OnUpdate" or "OnFixedUpdate" or "OnLateUpdate";
            bool lifecycle = timed || member.Name is "OnStart" or "OnEnd" or "OnDrawGUI";
            if (lifecycle)
            {
                bool signature = member.Type == "void" && !member.IsStatic && member.Parameters.Count == (timed ? 1 : 0)
                    && (!timed || member.Parameters[0].Type is "float32" or "float");
                if (signature) result.ScriptCallbacks.Add(new ScriptCallbackInfo { Name = member.Name, IsVirtual = member.IsVirtual, Line = member.Line });
                continue;
            }
            if (member.Access != "public" || member.IsStatic || member.IsTemplate) continue;
            ValueKindInfo? returnKind = GetValueKind(NormalizeValueType(member.Type));
            if (returnKind == null) continue;
            List<ParameterInfo> parameters = [];
            foreach (CppParameter parameter in member.Parameters)
            {
                string valueType = NormalizeValueType(parameter.Type);
                ValueKindInfo? kind = GetValueKind(valueType);
                if (kind == null || parameter.Type.Contains('*') || parameter.Type.Contains('&') && !parameter.Type.StartsWith("const ", StringComparison.Ordinal)) break;
                parameters.Add(new ParameterInfo { Name = parameter.Name, Type = valueType, Kind = kind });
            }
            if (parameters.Count == member.Parameters.Count)
                result.Methods.Add(new MethodInfo { Name = member.Name, ReturnType = NormalizeValueType(member.Type), ReturnKind = returnKind, Parameters = parameters, InvokerIndex = result.Methods.Count });
        }
        yield return result;
    }
}
//规范化 C++ 类型空白
static string NormalizeType(string type)
{
    var result = Regex.Replace(type.Trim(), @"\s*([*&])\s*", "$1");
    return Regex.Replace(result, @"\s*([<>,])\s*", "$1");
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
    if (className == "Script") return false;
    if (className is "Object") return false;
    if (className == "Component" && fieldName == "owner") return false;
    if (className == "Camera" && fieldName == "renderTargetId") return false;
    if (className == "StaticMeshRenderer" && fieldName == "renderState") return false;
    if (className == "Transform")
    {
        return fieldName is "localPosition" or "localRotation" or "localScale";
    }

    if (className == "Texture2D")
    {
        return fieldName is "name" or "width" or "height" or "channels" or "format";
    }

    if (className == "Skybox")
    {
        return fieldName is "right" or "left" or "top" or "bottom" or "front" or "back";
    }

    if (className == "Mesh")
    {
        return fieldName is "name";
    }

    if (className == "Material")
    {
        return fieldName is "name" or "shader";
    }

    if (className == "Shader")
    {
        return fieldName is "name" or "vertexPath" or "fragmentPath" or "vertexSource" or "fragmentSource";
    }

    return true;
}

//映射字段类型到 C++ FieldKind
static FieldKindInfo? GetFieldKind(string type)
{
    if (GetObjectRefTypeName(type) is not null)
    {
        return new FieldKindInfo("Reflection::FieldKind::ObjectRef");
    }

    return NormalizeValueType(type) switch
    {
        "bool" => new FieldKindInfo("Reflection::FieldKind::Bool"),
        "int32" => new FieldKindInfo("Reflection::FieldKind::Int32"),
        "uint32" => new FieldKindInfo("Reflection::FieldKind::UInt32"),
        "TypeRuntimeId" => new FieldKindInfo("Reflection::FieldKind::UInt32"),
        "uint64" => new FieldKindInfo("Reflection::FieldKind::UInt64"),
        "float32" => new FieldKindInfo("Reflection::FieldKind::Float32"),
        "std::string" => new FieldKindInfo("Reflection::FieldKind::String"),
        "StringId" => new FieldKindInfo("Reflection::FieldKind::StringId"),
        "vector3" => new FieldKindInfo("Reflection::FieldKind::Vector3"),
        "color" => new FieldKindInfo("Reflection::FieldKind::Color"),
        "quaternion" => new FieldKindInfo("Reflection::FieldKind::Quaternion"),
        "EnsId" => new FieldKindInfo("Reflection::FieldKind::EnsId"),
        "ClearMode" => new FieldKindInfo("Reflection::FieldKind::UInt32"),
        "DrawQueue" => new FieldKindInfo("Reflection::FieldKind::UInt32"),
        _ => null,
    };
}

//读取 Ref<T> 的目标类型名
static string? GetObjectRefTypeName(string type)
{
    var normalized = NormalizeValueType(type);
    var match = Regex.Match(normalized, @"^Ref<(?<target>[A-Za-z_]\w*(?:::\w+)*)>$");
    return match.Success ? match.Groups["target"].Value : null;
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
        "TypeRuntimeId" => new ValueKindInfo("Reflection::ValueKind::UInt32"),
        "uint64" => new ValueKindInfo("Reflection::ValueKind::UInt64"),
        "float32" => new ValueKindInfo("Reflection::ValueKind::Float32"),
        "std::string" => new ValueKindInfo("Reflection::ValueKind::String"),
        "StringId" => new ValueKindInfo("Reflection::ValueKind::StringId"),
        "vector3" => new ValueKindInfo("Reflection::ValueKind::Vector3"),
        "color" => new ValueKindInfo("Reflection::ValueKind::Color"),
        "quaternion" => new ValueKindInfo("Reflection::ValueKind::Quaternion"),
        "EnsId" => new ValueKindInfo("Reflection::ValueKind::EnsId"),
        _ => null,
    };
}

//生成 C++ 反射注册源码
static string GenerateCpp(List<ClassInfo> classes, string sourceRoot, bool gameModule, string? bindingModule)
{
    var output = new StringBuilder();
    output.AppendLine("// <auto-generated>");
    output.AppendLine("// Generated by Tools/OrbedenMetaGen. Do not edit by hand.");
    output.AppendLine("// </auto-generated>");
    output.AppendLine();
    output.AppendLine("#include \"Runtime/Reflection.h\"");
    output.AppendLine("#include \"Runtime/Object/Script.h\"");
    foreach (var file in classes.Select(value => Path.GetRelativePath(sourceRoot, value.File).Replace('\\', '/')).Distinct(StringComparer.Ordinal))
    {
        output.AppendLine($"#include \"{file}\"");
    }
    output.AppendLine();
    output.AppendLine("class ReflectionGeneratedAccess");
    output.AppendLine("{");
    output.AppendLine("public:");

    foreach (var classInfo in classes)
    {
        foreach (var callback in classInfo.ScriptCallbacks)
        {
            var timed = callback.Name is "OnUpdate" or "OnFixedUpdate" or "OnLateUpdate";
            output.AppendLine($"    //直接调用 {classInfo.Name}.{callback.Name}，不经过虚函数表");
            output.AppendLine(timed
                ? $"    static void Script_{classInfo.Symbol}_{callback.Name}(Script* script, float32 deltaTime)"
                : $"    static void Script_{classInfo.Symbol}_{callback.Name}(Script* script)");
            output.AppendLine("    {");
            output.AppendLine($"        {classInfo.CppName}* instance = static_cast<{classInfo.CppName}*>(script);");
            output.AppendLine(timed
                ? $"        instance->{classInfo.CppName}::{callback.Name}(deltaTime);"
                : $"        instance->{classInfo.CppName}::{callback.Name}();");
            output.AppendLine("    }");
            output.AppendLine();
        }

        //生成字段 getter/setter 和方法 invoker
        foreach (var field in classInfo.Fields.Where(field => field.Persistent))
        {
            var setterBacked = classInfo.Name == "Transform"
                || (field.Name == "enabled" && classInfo.Name is "Camera" or "DirectionalLight" or "StaticMeshRenderer");
            var marksDirty = classInfo.Name == "Material" && field.Name == "shader";
            var regenerateOnSet = field.Changed.Length != 0;
            var getterExpression = setterBacked
                ? $"instance->Get{char.ToUpperInvariant(field.Name[0])}{field.Name[1..]}()"
                : $"instance->{field.Name}";
            output.AppendLine($"    //读取 {classInfo.Name}.{field.Name} 字段");
            output.AppendLine($"    static std::string Get_{classInfo.Symbol}_{field.Name}(Object* object)");
            output.AppendLine("    {");
            output.AppendLine($"        {classInfo.CppName}* instance = static_cast<{classInfo.CppName}*>(object);");
            output.AppendLine($"        return Reflection::ToXmlValue({getterExpression});");
            output.AppendLine("    }");
            output.AppendLine();
            output.AppendLine($"    //直接读取 {classInfo.Name}.{field.Name} 字段");
            output.AppendLine($"    static Reflection::Value GetValue_{classInfo.Symbol}_{field.Name}(Object* object)");
            output.AppendLine("    {");
            output.AppendLine($"        {classInfo.CppName}* instance = static_cast<{classInfo.CppName}*>(object);");
            output.AppendLine($"        return instance ? Reflection::ToValue({getterExpression}) : Reflection::Value();");
            output.AppendLine("    }");
            output.AppendLine();
            output.AppendLine($"    //写入 {classInfo.Name}.{field.Name} 字段");
            output.AppendLine($"    static bool Set_{classInfo.Symbol}_{field.Name}(Object* object, const std::string& value)");
            output.AppendLine("    {");
            output.AppendLine($"        {classInfo.CppName}* instance = static_cast<{classInfo.CppName}*>(object);");
            if (setterBacked)
            {
                var setterName = $"Set{char.ToUpperInvariant(field.Name[0])}{field.Name[1..]}";
                output.AppendLine($"        {field.Type} parsedValue{{}};");
                output.AppendLine("        if (!Reflection::SetFromXmlValue(parsedValue, value)) return false;");
                output.AppendLine($"        instance->{setterName}(parsedValue);");
                output.AppendLine("        return true;");
            }
            else if (marksDirty)
            {
                output.AppendLine($"        if (!Reflection::SetFromXmlValue(instance->{field.Name}, value)) return false;");
                output.AppendLine("        instance->MarkDirty();");
                output.AppendLine("        return true;");
            }
            else if (regenerateOnSet)
            {
                output.AppendLine($"        if (!Reflection::SetFromXmlValue(instance->{field.Name}, value)) return false;");
                output.AppendLine($"        instance->{field.Changed}();");
                output.AppendLine("        return true;");
            }
            else
            {
                output.AppendLine($"        return Reflection::SetFromXmlValue(instance->{field.Name}, value);");
            }
            output.AppendLine("    }");
            output.AppendLine();
            output.AppendLine($"    //直接写入 {classInfo.Name}.{field.Name} 字段");
            output.AppendLine($"    static bool SetValue_{classInfo.Symbol}_{field.Name}(Object* object, const Reflection::Value& value)");
            output.AppendLine("    {");
            output.AppendLine($"        {classInfo.CppName}* instance = static_cast<{classInfo.CppName}*>(object);");
            output.AppendLine("        if (!instance) return false;");
            if (setterBacked)
            {
                var setterName = $"Set{char.ToUpperInvariant(field.Name[0])}{field.Name[1..]}";
                output.AppendLine($"        {field.Type} parsedValue{{}};");
                output.AppendLine("        if (!Reflection::SetFromValue(parsedValue, value)) return false;");
                output.AppendLine($"        instance->{setterName}(parsedValue);");
                output.AppendLine("        return true;");
            }
            else if (marksDirty)
            {
                output.AppendLine($"        if (!Reflection::SetFromValue(instance->{field.Name}, value)) return false;");
                output.AppendLine("        instance->MarkDirty();");
                output.AppendLine("        return true;");
            }
            else if (regenerateOnSet)
            {
                output.AppendLine($"        if (!Reflection::SetFromValue(instance->{field.Name}, value)) return false;");
                output.AppendLine($"        instance->{field.Changed}();");
                output.AppendLine("        return true;");
            }
            else
            {
                output.AppendLine($"        return Reflection::SetFromValue(instance->{field.Name}, value);");
            }
            output.AppendLine("    }");
            output.AppendLine();
        }

        foreach (var method in classInfo.Methods)
        {
            var invokerName = $"Invoke_{classInfo.Symbol}_{method.Name}_{method.InvokerIndex}";
            output.AppendLine($"    //调用 {classInfo.Name}.{method.Name} 方法");
            output.AppendLine($"    static Reflection::Value {invokerName}(Object* object, std::span<const Reflection::Value> args, bool& success)");
            output.AppendLine("    {");
            output.AppendLine("        success = false;");
            output.AppendLine($"        {classInfo.CppName}* instance = static_cast<{classInfo.CppName}*>(object);");
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
    if (gameModule)
    {
        if (bindingModule != null) output.AppendLine($"void RegisterBindings_{bindingModule}();");
        output.AppendLine("#if defined(_WIN32)");
        output.AppendLine("#define ORBEDEN_GAME_EXPORT __declspec(dllexport)");
        output.AppendLine("#else");
        output.AppendLine("#define ORBEDEN_GAME_EXPORT __attribute__((visibility(\"default\")))");
        output.AppendLine("#endif");
        output.AppendLine("extern \"C\" ORBEDEN_GAME_EXPORT void OrbedenGameNative_RegisterReflection()");
    }
    else
    {
        output.AppendLine("namespace Reflection");
        output.AppendLine("{");
        output.AppendLine("    //注册生成的反射元数据");
        output.AppendLine("    void RegisterGeneratedReflection()");
    }
    output.AppendLine("    {");
    if (gameModule) output.AppendLine("        using namespace Reflection;");
    if (gameModule && bindingModule != null) output.AppendLine($"        RegisterBindings_{bindingModule}();");
    output.AppendLine("        static bool registered = false;");
    output.AppendLine("        if (registered) return;");
    output.AppendLine("        registered = true;");
    output.AppendLine();

    foreach (var classInfo in classes)
    {
        //注册字段元数据
        if (classInfo.Name == "Script")
        {
            output.AppendLine("        Script::RegisterReflection();");
            continue;
        }
        output.AppendLine($"        RegisterTypeFields({classInfo.CppName}::StaticType(),");
        output.AppendLine("            {");

        foreach (var field in classInfo.Fields)
        {
            var kind = field.Kind?.CppName ?? "Reflection::FieldKind::Unsupported";
            var persistent = field.Persistent ? "true" : "false";
            var getter = field.Persistent ? $"ReflectionGeneratedAccess::Get_{classInfo.Symbol}_{field.Name}" : "nullptr";
            var setter = field.Persistent ? $"ReflectionGeneratedAccess::Set_{classInfo.Symbol}_{field.Name}" : "nullptr";
            var objectRefTypeName = field.ObjectRefTypeName is null ? "nullptr" : $"\"{field.ObjectRefTypeName}\"";
            var valueGetter = field.Persistent ? $"ReflectionGeneratedAccess::GetValue_{classInfo.Symbol}_{field.Name}" : "nullptr";
            var valueSetter = field.Persistent ? $"ReflectionGeneratedAccess::SetValue_{classInfo.Symbol}_{field.Name}" : "nullptr";
            output.AppendLine($"                FieldInfo(\"{field.Name}\", \"{field.Type}\", {kind}, {persistent}, {getter}, {setter}, {objectRefTypeName}, {valueGetter}, {valueSetter}),");
        }

        output.AppendLine("            });");
        output.AppendLine();

        if (classInfo.IsScript)
        {
            string Callback(string name)
            {
                return classInfo.ScriptCallbacks.Any(value => value.Name == name)
                    ? $"ReflectionGeneratedAccess::Script_{classInfo.Symbol}_{name}"
                    : "nullptr";
            }

            output.AppendLine($"        RegisterScriptCallbacks({classInfo.CppName}::StaticType(),");
            output.AppendLine("            {");
            output.AppendLine($"                {Callback("OnStart")},");
            output.AppendLine($"                {Callback("OnUpdate")},");
            output.AppendLine($"                {Callback("OnFixedUpdate")},");
            output.AppendLine($"                {Callback("OnLateUpdate")},");
            output.AppendLine($"                {Callback("OnDrawGUI")},");
            output.AppendLine($"                {Callback("OnEnd")},");
            output.AppendLine("            });");
            output.AppendLine();
        }

        //注册方法元数据
        output.AppendLine($"        RegisterTypeMethods({classInfo.CppName}::StaticType(),");
        output.AppendLine("            {");

        foreach (var method in classInfo.Methods)
        {
            var parameters = method.Parameters.Count == 0
                ? "List<ParameterInfo>()"
                : "List<ParameterInfo>{ " + string.Join(", ", method.Parameters.Select(parameter => $"ParameterInfo(\"{parameter.Name}\", \"{parameter.Type}\", {parameter.Kind.CppName})")) + " }";

            output.AppendLine($"                MethodInfo(\"{method.Name}\", \"{method.ReturnType}\", {method.ReturnKind!.CppName}, {parameters}, ReflectionGeneratedAccess::Invoke_{classInfo.Symbol}_{method.Name}_{method.InvokerIndex}),");
        }

        output.AppendLine("            });");
        output.AppendLine();
    }

    output.AppendLine("    }");
    if (!gameModule) output.AppendLine("}");
    return output.ToString();
}

sealed class ClassInfo
{
    public string CppName { get; set; } = "";
    public string Symbol => CppName.Replace("::", "_");
    public string Name { get; set; } = "";
    public string BaseName { get; set; } = "";
    public string File { get; set; } = "";
    public List<FieldInfo> Fields { get; set; } = [];
    public List<MethodInfo> Methods { get; set; } = [];
    public List<ScriptCallbackInfo> ScriptCallbacks { get; set; } = [];
    public bool IsScript { get; set; }
}

sealed class FieldInfo
{
    public string Name { get; set; } = "";
    public string Type { get; set; } = "";
    public string Access { get; set; } = "";
    public string Changed { get; set; } = "";
    public int Line { get; set; }
    public bool ExplicitPersistent { get; set; }
    public bool Persistent { get; set; }
    public bool FixedArray { get; set; }
    public FieldKindInfo? Kind { get; set; }
    public string? ObjectRefTypeName { get; set; }
}

sealed class ScriptCallbackInfo
{
    public string Name { get; set; } = "";
    public bool IsVirtual { get; set; }
    public int Line { get; set; }
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
