using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using Orbeden;

namespace ExampleGame;

/// <summary>按 world 脚本挂载清单启动用户脚本。</summary>
internal static class MountedScriptRuntime
{
    private sealed class ScriptMount
    {
        public string StableId = string.Empty;
        public string Type = string.Empty;
        public Dictionary<string, string> Values = [];
    }

    private static readonly List<ScriptBehaviour> scripts = [];

    /// <summary>读取当前 world 的脚本挂载清单并启动脚本。</summary>
    public static void Initialize()
    {
        Shutdown();

        string sidecarPath = GetStartupWorldSidecarPath();
        if (string.IsNullOrEmpty(sidecarPath) || !File.Exists(sidecarPath))
        {
            Console.WriteLine("MountedScriptRuntime: world script sidecar was not found.");
            return;
        }

        foreach (ScriptMount mount in ReadScriptMounts(sidecarPath))
        {
            Ens ens = Ens.Find(mount.StableId);
            if (!ens.IsValid)
            {
                Console.WriteLine($"MountedScriptRuntime: missing Ens stableId '{mount.StableId}'.");
                continue;
            }

            ScriptBehaviour? script = CreateScript(mount, ens);
            if (script == null)
            {
                Console.WriteLine($"MountedScriptRuntime: unsupported script type '{mount.Type}'.");
                continue;
            }

            scripts.Add(script);
        }

        foreach (ScriptBehaviour script in scripts)
        {
            script.InvokeStart();
        }
    }

    /// <summary>更新已挂载脚本。</summary>
    public static void Update(float deltaTime)
    {
        foreach (ScriptBehaviour script in scripts)
        {
            if (script.Ens.IsValid)
            {
                script.InvokeUpdate(deltaTime);
            }
        }
    }

    /// <summary>关闭已挂载脚本。</summary>
    public static void Shutdown()
    {
        for (int index = scripts.Count - 1; index >= 0; index--)
        {
            scripts[index].InvokeEnd();
        }

        scripts.Clear();
    }

    //创建 AOT 友好的脚本实例，不通过反射构造。
    private static ScriptBehaviour? CreateScript(ScriptMount mount, Ens ens)
    {
        return StripAssemblyName(mount.Type) switch
        {
            "ExampleGame.SampleBehaviour" => CreateSampleBehaviour(ens, mount.Values),
            "ExampleGame.CubeTestBehaviour" => CreateCubeTestBehaviour(ens, mount.Values),
            _ => null,
        };
    }

    //创建示例脚本并写入 sidecar 序列化字段。
    private static SampleBehaviour CreateSampleBehaviour(Ens ens, IReadOnlyDictionary<string, string> values)
    {
        SampleBehaviour script = new(ens);
        script.ApplySerializedValues(values);
        return script;
    }

    //创建 Cube 测试脚本并写入 sidecar 序列化字段。
    private static CubeTestBehaviour CreateCubeTestBehaviour(Ens ens, IReadOnlyDictionary<string, string> values)
    {
        CubeTestBehaviour script = new(ens);
        script.ApplySerializedValues(values);
        return script;
    }

    //获取当前启动 world 对应的脚本 sidecar。
    private static string GetStartupWorldSidecarPath()
    {
        string projectRoot = PathDefines.ProjectRoot;
        if (string.IsNullOrWhiteSpace(projectRoot) || !Directory.Exists(projectRoot)) return string.Empty;

        string? projectFile = Directory.EnumerateFiles(projectRoot, "*.oeproj", SearchOption.TopDirectoryOnly).FirstOrDefault();
        if (string.IsNullOrEmpty(projectFile)) return string.Empty;

        string startupWorld = ReadAttribute(File.ReadAllText(projectFile), "startupWorld");
        if (string.IsNullOrWhiteSpace(startupWorld)) return string.Empty;

        string worldPath = Path.Combine(projectRoot, startupWorld.Replace('/', Path.DirectorySeparatorChar));
        return Path.GetFullPath(worldPath) + ".scripts.json";
    }

    //读取 XML 单行项目文件中的属性。
    private static string ReadAttribute(string text, string name)
    {
        string token = name + "=\"";
        int start = text.IndexOf(token, StringComparison.Ordinal);
        if (start < 0) return string.Empty;

        start += token.Length;
        int end = text.IndexOf('"', start);
        return end > start ? text[start..end] : string.Empty;
    }

    //读取 world sidecar 脚本挂载项。
    private static IEnumerable<ScriptMount> ReadScriptMounts(string sidecarPath)
    {
        using JsonDocument document = JsonDocument.Parse(File.ReadAllText(sidecarPath));
        if (!document.RootElement.TryGetProperty("scripts", out JsonElement scriptsElement)) yield break;
        if (scriptsElement.ValueKind != JsonValueKind.Array) yield break;

        foreach (JsonElement element in scriptsElement.EnumerateArray())
        {
            ScriptMount? mount = ReadScriptMount(element);
            if (mount != null) yield return mount;
        }
    }

    //读取单个脚本挂载项。
    private static ScriptMount? ReadScriptMount(JsonElement element)
    {
        string stableId = element.TryGetProperty("stableId", out JsonElement stableIdElement) ? stableIdElement.GetString() ?? string.Empty : string.Empty;
        string type = element.TryGetProperty("type", out JsonElement typeElement) ? typeElement.GetString() ?? string.Empty : string.Empty;
        if (string.IsNullOrWhiteSpace(stableId) || string.IsNullOrWhiteSpace(type)) return null;

        ScriptMount mount = new() { StableId = stableId, Type = StripAssemblyName(type) };
        if (!element.TryGetProperty("values", out JsonElement valuesElement)) return mount;
        if (valuesElement.ValueKind != JsonValueKind.Object) return mount;

        foreach (JsonProperty property in valuesElement.EnumerateObject())
        {
            mount.Values[property.Name] = ReadSerializedValue(property.Value);
        }

        return mount;
    }

    //读取 sidecar 字段值文本。
    private static string ReadSerializedValue(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object && element.TryGetProperty("value", out JsonElement valueElement))
        {
            return GetJsonValueText(valueElement);
        }

        return GetJsonValueText(element);
    }

    //把 JsonElement 转成可解析文本。
    private static string GetJsonValueText(JsonElement element)
    {
        return element.ValueKind == JsonValueKind.String ? element.GetString() ?? string.Empty : element.GetRawText();
    }

    //规范化脚本类型名。
    private static string StripAssemblyName(string typeName)
    {
        string value = typeName.Trim();
        int commaIndex = value.IndexOf(',');
        return commaIndex >= 0 ? value[..commaIndex].Trim() : value;
    }
}

/// <summary>脚本 sidecar 字段值解析工具。</summary>
internal static class ScriptValueReader
{
    /// <summary>读取 string 字段。</summary>
    public static bool TryGetString(IReadOnlyDictionary<string, string> values, string name, out string value)
    {
        if (values.TryGetValue(name, out string? text))
        {
            value = text;
            return true;
        }

        value = string.Empty;
        return false;
    }

    /// <summary>读取 bool 字段。</summary>
    public static bool TryGetBool(IReadOnlyDictionary<string, string> values, string name, out bool value)
    {
        value = false;
        return values.TryGetValue(name, out string? text) && bool.TryParse(text, out value);
    }

    /// <summary>读取 int 字段。</summary>
    public static bool TryGetInt(IReadOnlyDictionary<string, string> values, string name, out int value)
    {
        value = 0;
        return values.TryGetValue(name, out string? text)
            && int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out value);
    }

    /// <summary>读取 float 字段。</summary>
    public static bool TryGetFloat(IReadOnlyDictionary<string, string> values, string name, out float value)
    {
        value = 0.0f;
        return values.TryGetValue(name, out string? text)
            && float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value);
    }

    /// <summary>读取 vector3 字段。</summary>
    public static bool TryGetVector3(IReadOnlyDictionary<string, string> values, string name, out vector3 value)
    {
        value = new vector3();
        if (!values.TryGetValue(name, out string? text)) return false;

        string[] parts = text.Split(new[] { ' ', ',', ';' }, StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length != 3) return false;
        if (!float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out float x)) return false;
        if (!float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out float y)) return false;
        if (!float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out float z)) return false;

        value = new vector3(x, y, z);
        return true;
    }
}
