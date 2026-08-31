using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Diagnostics;

namespace Orbeden.ScriptGenerator;

/// <summary>生成游戏脚本模块的固定 ABI 与类型工厂。</summary>
[Generator]
public sealed class GameModuleGenerator : IIncrementalGenerator
{
    private static readonly DiagnosticDescriptor MissingConstructor = new(
        "ORB001",
        "脚本构造函数不可用",
        "脚本 '{0}' 必须声明 public {1}(Orbeden.Ens) 构造函数。",
        "Orbeden",
        DiagnosticSeverity.Error,
        true);

    /// <summary>注册编译期生成逻辑。</summary>
    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        IncrementalValueProvider<(Compilation Compilation, string RootNamespace)> input = context.CompilationProvider
            .Combine(context.AnalyzerConfigOptionsProvider.Select(static (options, _) => GetRootNamespace(options.GlobalOptions)));
        context.RegisterSourceOutput(input, static (productionContext, value) => GenerateGameModule(productionContext, value.Compilation, value.RootNamespace));
    }

    //读取脚本工程根命名空间
    private static string GetRootNamespace(AnalyzerConfigOptions options)
    {
        return options.TryGetValue("build_property.RootNamespace", out string? value) ? value : string.Empty;
    }

    //生成模块入口与脚本工厂
    private static void GenerateGameModule(SourceProductionContext context, Compilation compilation, string rootNamespace)
    {
        INamedTypeSymbol? scriptBehaviour = compilation.GetTypeByMetadataName("Orbeden.ScriptBehaviour");
        INamedTypeSymbol? ens = compilation.GetTypeByMetadataName("Orbeden.Ens");
        INamedTypeSymbol? stringType = compilation.GetSpecialType(SpecialType.System_String);
        INamedTypeSymbol? dictionaryType = compilation.GetTypeByMetadataName("System.Collections.Generic.IReadOnlyDictionary`2");
        if (scriptBehaviour == null || ens == null || stringType == null || dictionaryType == null) return;
        INamedTypeSymbol serializedValues = dictionaryType.Construct(stringType, stringType);

        List<INamedTypeSymbol> scriptTypes = [];
        CollectScriptTypes(compilation.Assembly.GlobalNamespace, scriptBehaviour, scriptTypes);
        scriptTypes.Sort(static (left, right) => string.Compare(left.ToDisplayString(), right.ToDisplayString(), StringComparison.Ordinal));

        List<ScriptTypeInfo> validTypes = [];
        for (int index = 0; index < scriptTypes.Count; index++)
        {
            INamedTypeSymbol scriptType = scriptTypes[index];
            IMethodSymbol? constructor = FindScriptConstructor(scriptType, ens);
            if (constructor == null)
            {
                context.ReportDiagnostic(Diagnostic.Create(MissingConstructor, scriptType.Locations.FirstOrDefault(), scriptType.ToDisplayString(), scriptType.Name));
                continue;
            }

            validTypes.Add(new ScriptTypeInfo(scriptType, validTypes.Count, HasSerializedValueApplier(scriptType, serializedValues)));
        }

        string moduleNamespace = string.IsNullOrWhiteSpace(rootNamespace) ? compilation.AssemblyName ?? "Game" : rootNamespace;
        context.AddSource("Orbeden.GameModule.g.cs", BuildSource(moduleNamespace, validTypes));
    }

    //收集所有非抽象脚本类型
    private static void CollectScriptTypes(INamespaceSymbol currentNamespace, INamedTypeSymbol scriptBehaviour, List<INamedTypeSymbol> results)
    {
        foreach (INamedTypeSymbol type in currentNamespace.GetTypeMembers())
        {
            CollectScriptType(type, scriptBehaviour, results);
        }

        foreach (INamespaceSymbol childNamespace in currentNamespace.GetNamespaceMembers())
        {
            CollectScriptTypes(childNamespace, scriptBehaviour, results);
        }
    }

    //收集类型及其嵌套脚本类型
    private static void CollectScriptType(INamedTypeSymbol type, INamedTypeSymbol scriptBehaviour, List<INamedTypeSymbol> results)
    {
        if (type.TypeKind == TypeKind.Class && !type.IsAbstract && InheritsFrom(type, scriptBehaviour))
        {
            results.Add(type);
        }

        foreach (INamedTypeSymbol nestedType in type.GetTypeMembers())
        {
            CollectScriptType(nestedType, scriptBehaviour, results);
        }
    }

    //判断类型是否继承脚本基类
    private static bool InheritsFrom(INamedTypeSymbol type, INamedTypeSymbol targetType)
    {
        for (INamedTypeSymbol? current = type.BaseType; current != null; current = current.BaseType)
        {
            if (SymbolEqualityComparer.Default.Equals(current, targetType)) return true;
        }

        return false;
    }

    //查找可由工厂调用的脚本构造函数
    private static IMethodSymbol? FindScriptConstructor(INamedTypeSymbol type, INamedTypeSymbol ens)
    {
        foreach (IMethodSymbol constructor in type.InstanceConstructors)
        {
            if (constructor.DeclaredAccessibility != Accessibility.Public || constructor.Parameters.Length != 1) continue;
            if (SymbolEqualityComparer.Default.Equals(constructor.Parameters[0].Type, ens)) return constructor;
        }

        return null;
    }

    //判断脚本是否声明可访问的序列化字段应用方法
    private static bool HasSerializedValueApplier(INamedTypeSymbol type, INamedTypeSymbol serializedValues)
    {
        foreach (IMethodSymbol method in type.GetMembers("ApplySerializedValues").OfType<IMethodSymbol>())
        {
            if (method.DeclaredAccessibility is not (Accessibility.Public or Accessibility.Internal)) continue;
            if (method.Parameters.Length != 1) continue;
            if (SymbolEqualityComparer.Default.Equals(method.Parameters[0].Type, serializedValues)) return true;
        }

        return false;
    }

    //构造生成源码
    private static string BuildSource(string moduleNamespace, List<ScriptTypeInfo> scriptTypes)
    {
        StringBuilder source = new();
        source.AppendLine("// <auto-generated />");
        source.AppendLine("#nullable enable");
        source.AppendLine("using System;");
        source.AppendLine("using System.Collections.Generic;");
        source.AppendLine();
        source.Append("namespace ").Append(moduleNamespace).AppendLine(";");
        source.AppendLine();
        source.AppendLine("public static class GameModule");
        source.AppendLine("{");
        source.AppendLine("    [global::System.Runtime.InteropServices.UnmanagedCallersOnly(EntryPoint = \"OrbedenGame_Initialize\", CallConvs = [typeof(global::System.Runtime.CompilerServices.CallConvCdecl)])]");
        source.AppendLine("    public static void OrbedenGame_Initialize(global::System.IntPtr nativeApi)");
        source.AppendLine("    {");
        source.AppendLine("        global::Orbeden.ScriptRuntime.Initialize(nativeApi, new ScriptFactory());");
        source.AppendLine("    }");
        source.AppendLine();
        source.AppendLine("    [global::System.Runtime.InteropServices.UnmanagedCallersOnly(EntryPoint = \"OrbedenGame_Shutdown\", CallConvs = [typeof(global::System.Runtime.CompilerServices.CallConvCdecl)])]");
        source.AppendLine("    public static void OrbedenGame_Shutdown()");
        source.AppendLine("    {");
        source.AppendLine("        global::Orbeden.ScriptRuntime.Shutdown();");
        source.AppendLine("    }");
        source.AppendLine();
        source.AppendLine("    [global::System.Runtime.InteropServices.UnmanagedCallersOnly(EntryPoint = \"OrbedenGame_Update\", CallConvs = [typeof(global::System.Runtime.CompilerServices.CallConvCdecl)])]");
        source.AppendLine("    public static void OrbedenGame_Update(float deltaTime)");
        source.AppendLine("    {");
        source.AppendLine("        global::Orbeden.ScriptRuntime.Update(deltaTime);");
        source.AppendLine("    }");
        source.AppendLine();
        source.AppendLine("    [global::System.Runtime.InteropServices.UnmanagedCallersOnly(EntryPoint = \"OrbedenGame_FixedUpdate\", CallConvs = [typeof(global::System.Runtime.CompilerServices.CallConvCdecl)])]");
        source.AppendLine("    public static void OrbedenGame_FixedUpdate(float fixedDeltaTime)");
        source.AppendLine("    {");
        source.AppendLine("        global::Orbeden.ScriptRuntime.FixedUpdate(fixedDeltaTime);");
        source.AppendLine("    }");
        source.AppendLine();
        source.AppendLine("    [global::System.Runtime.InteropServices.UnmanagedCallersOnly(EntryPoint = \"OrbedenGame_DrawGui\", CallConvs = [typeof(global::System.Runtime.CompilerServices.CallConvCdecl)])]");
        source.AppendLine("    public static void OrbedenGame_DrawGui()");
        source.AppendLine("    {");
        source.AppendLine("        global::Orbeden.ScriptRuntime.DrawGUI();");
        source.AppendLine("    }");
        source.AppendLine();
        source.AppendLine("    private sealed class ScriptFactory : global::Orbeden.IScriptFactory");
        source.AppendLine("    {");
        source.AppendLine("        public global::Orbeden.ScriptBehaviour? Create(string typeName, global::Orbeden.Ens ens, IReadOnlyDictionary<string, string> values)");
        source.AppendLine("        {");
        source.AppendLine("            return typeName switch");
        source.AppendLine("            {");
        foreach (ScriptTypeInfo scriptType in scriptTypes)
        {
            source.Append("                \"").Append(scriptType.TypeName).Append("\" => Create").Append(scriptType.FactoryName).AppendLine("(ens, values),");
        }
        source.AppendLine("                _ => null,");
        source.AppendLine("            };");
        source.AppendLine("        }");

        foreach (ScriptTypeInfo scriptType in scriptTypes)
        {
            source.AppendLine();
            source.Append("        private static global::Orbeden.ScriptBehaviour Create").Append(scriptType.FactoryName).AppendLine("(global::Orbeden.Ens ens, IReadOnlyDictionary<string, string> values)");
            source.AppendLine("        {");
            source.Append("            ").Append(scriptType.FullyQualifiedName).Append(" script = new(ens);").AppendLine();
            if (scriptType.HasSerializedValueApplier)
            {
                source.Append("            script.ApplySerializedValues(values);").AppendLine();
            }
            source.AppendLine("            return script;");
            source.AppendLine("        }");
        }

        source.AppendLine("    }");
        source.AppendLine("}");
        return source.ToString();
    }

    private sealed record ScriptTypeInfo(INamedTypeSymbol Type, int Index, bool HasSerializedValueApplier)
    {
        public string TypeName { get; } = Type.ToDisplayString();
        public string FullyQualifiedName { get; } = Type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
        public string FactoryName { get; } = "Script" + Index;
    }
}
