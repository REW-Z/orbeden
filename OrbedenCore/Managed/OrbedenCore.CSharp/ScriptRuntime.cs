using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Orbeden;

/// <summary>创建已挂载脚本实例的工厂。</summary>
public interface IScriptFactory
{
    /// <summary>创建指定类型的脚本实例。</summary>
    ScriptBehaviour? Create(string typeName, Ens ens, IReadOnlyDictionary<string, string> values);
}

/// <summary>管理当前 World 的托管脚本生命周期。</summary>
public static class ScriptRuntime
{
    private sealed class ScriptMount
    {
        public string Id = string.Empty;
        public string StableId = string.Empty;
        public string Type = string.Empty;
        public Dictionary<string, string> Values = [];
    }

    private static readonly List<ScriptBehaviour> scripts = [];

    /// <summary>初始化原生绑定并创建 World 脚本。</summary>
    public static void Initialize(IntPtr nativeApi, IScriptFactory factory)
    {
        OrbedenCoreRuntime.Initialize(nativeApi);
        Shutdown();

        //读取启动场景脚本挂载清单
        string sidecarPath = GetStartupWorldSidecarPath();
        if (string.IsNullOrEmpty(sidecarPath) || !File.Exists(sidecarPath))
        {
            Console.WriteLine("ScriptRuntime: world script sidecar was not found.");
            return;
        }

        //创建已挂载脚本实例
        foreach (ScriptMount mount in ReadScriptMounts(sidecarPath))
        {
            Ens ens = Ens.Find(mount.StableId);
            if (!ens.IsValid)
            {
                Console.WriteLine($"ScriptRuntime: missing Ens stableId '{mount.StableId}'.");
                continue;
            }

            ScriptBehaviour? script = factory.Create(mount.Type, ens, mount.Values);
            if (script == null)
            {
                Console.WriteLine($"ScriptRuntime: unsupported script type '{mount.Type}'.");
                continue;
            }

            script.SetMountId(mount.Id);
            scripts.Add(script);
        }

        //启动已创建脚本实例
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

    /// <summary>固定步长更新已挂载脚本。</summary>
    public static void FixedUpdate(float fixedDeltaTime)
    {
        foreach (ScriptBehaviour script in scripts)
        {
            if (script.Ens.IsValid)
            {
                script.InvokeFixedUpdate(fixedDeltaTime);
            }
        }
    }

    /// <summary>绘制已挂载脚本的运行时 GUI。</summary>
    public static void DrawGUI()
    {
        foreach (ScriptBehaviour script in scripts)
        {
            if (script.Ens.IsValid)
            {
                script.InvokeDrawGUI();
            }
        }
    }

    /// <summary>关闭当前 World 的脚本实例。</summary>
    public static void Shutdown()
    {
        for (int index = scripts.Count - 1; index >= 0; index--)
        {
            scripts[index].InvokeEnd();
        }

        scripts.Clear();
    }

    //获取当前启动 World 的脚本挂载文件
    private static string GetStartupWorldSidecarPath()
    {
        string projectRoot = PathDefines.ContentRoot;
        if (string.IsNullOrWhiteSpace(projectRoot) || !Directory.Exists(projectRoot)) return string.Empty;

        string? projectFile = Directory.EnumerateFiles(projectRoot, "*.oeproj", SearchOption.TopDirectoryOnly).FirstOrDefault();
        if (string.IsNullOrEmpty(projectFile)) return string.Empty;

        string startupWorld = ReadAttribute(File.ReadAllText(projectFile), "startupWorld");
        if (string.IsNullOrWhiteSpace(startupWorld)) return string.Empty;

        string worldPath = Path.Combine(projectRoot, startupWorld.Replace('/', Path.DirectorySeparatorChar));
        return Path.GetFullPath(worldPath) + ".scripts.json";
    }

    //读取项目文件中的单行属性
    private static string ReadAttribute(string text, string name)
    {
        string token = name + "=\"";
        int start = text.IndexOf(token, StringComparison.Ordinal);
        if (start < 0) return string.Empty;

        start += token.Length;
        int end = text.IndexOf('"', start);
        return end > start ? text[start..end] : string.Empty;
    }

    //读取脚本挂载列表
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

    //读取单个脚本挂载项
    private static ScriptMount? ReadScriptMount(JsonElement element)
    {
        string stableId = element.TryGetProperty("stableId", out JsonElement stableIdElement) ? stableIdElement.GetString() ?? string.Empty : string.Empty;
        string type = element.TryGetProperty("type", out JsonElement typeElement) ? typeElement.GetString() ?? string.Empty : string.Empty;
        if (string.IsNullOrWhiteSpace(stableId) || string.IsNullOrWhiteSpace(type)) return null;

        string id = element.TryGetProperty("id", out JsonElement idElement) ? idElement.GetString() ?? string.Empty : string.Empty;
        if (string.IsNullOrWhiteSpace(id)) id = Guid.NewGuid().ToString("N");

        ScriptMount mount = new() { Id = id, StableId = stableId, Type = StripAssemblyName(type) };
        if (!element.TryGetProperty("values", out JsonElement valuesElement)) return mount;
        if (valuesElement.ValueKind != JsonValueKind.Object) return mount;

        foreach (JsonProperty property in valuesElement.EnumerateObject())
        {
            mount.Values[property.Name] = ReadSerializedValue(property.Value);
        }

        return mount;
    }

    //读取序列化字段文本
    private static string ReadSerializedValue(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object && element.TryGetProperty("value", out JsonElement valueElement))
        {
            return GetJsonValueText(valueElement);
        }

        return GetJsonValueText(element);
    }

    //转换 Json 字段文本
    private static string GetJsonValueText(JsonElement element)
    {
        return element.ValueKind == JsonValueKind.String ? element.GetString() ?? string.Empty : element.GetRawText();
    }

    //去掉脚本类型中的程序集后缀
    private static string StripAssemblyName(string typeName)
    {
        string value = typeName.Trim();
        int commaIndex = value.IndexOf(',');
        return commaIndex >= 0 ? value[..commaIndex].Trim() : value;
    }
}

/// <summary>读取脚本挂载中的基础字段值。</summary>
public static class ScriptValueReader
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

        string[] parts = text.Split([' ', ',', ';'], StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length != 3) return false;
        if (!float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out float x)) return false;
        if (!float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out float y)) return false;
        if (!float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out float z)) return false;

        value = new vector3(x, y, z);
        return true;
    }
}
