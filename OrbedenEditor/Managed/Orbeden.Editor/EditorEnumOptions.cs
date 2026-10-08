using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using Orbeden;

namespace OrbedenEditor;

/// <summary>
/// 枚举字段的下拉项解析与缓存。解析只在属性快照重建时发生一次，绘制路径只读结果；
/// 缓存按声明类型名保存，程序集装载或卸载时清空。
/// </summary>
internal static class EditorEnumOptions
{
    private static readonly Dictionary<string, (long Value, string Label)[]?> cache = new(StringComparer.Ordinal);

    /// <summary>按字段的声明类型名取出枚举的取值与显示名；不是枚举或解析不到时返回 null。</summary>
    internal static (long Value, string Label)[]? Resolve(string typeName)
    {
        if (string.IsNullOrEmpty(typeName)) return null;
        if (cache.TryGetValue(typeName, out (long, string)[]? cached)) return cached;

        Type? type = FindType(typeName);
        if (type is not { IsEnum: true })
        {
            //否定结果一并缓存：重复遇到同一个非枚举类型名不再查程序集
            cache[typeName] = null;
            return null;
        }
        Type underlying = Enum.GetUnderlyingType(type);
        (long, string)[]? options = null;
        //只认 32 位底的枚举：属性值本身只有 UInt32 与 Int32 两种承载
        if (underlying == typeof(uint) || underlying == typeof(int))
        {
            List<(long, string)> items = [];
            foreach (object option in Enum.GetValues(type))
            {
                long number = underlying == typeof(uint)
                    ? Convert.ToUInt32(option, CultureInfo.InvariantCulture)
                    : Convert.ToInt32(option, CultureInfo.InvariantCulture);
                items.Add((number, Enum.GetName(type!, option) ?? number.ToString(CultureInfo.InvariantCulture)));
            }
            items.Sort((left, right) => left.Item1.CompareTo(right.Item1));
            options = [.. items];
        }

        cache[typeName] = options;
        return options;
    }

    /// <summary>按数值取枚举的显示名，没有对应项时退回数字文本。</summary>
    internal static string GetLabel((long Value, string Label)[] options, long value)
    {
        foreach ((long option, string optionLabel) in options)
            if (option == value) return optionLabel;
        return value.ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>
    /// 按组件托管类型名与字段名反查枚举字段的声明类型名。
    /// 托管脚本字段上报给原生的类型名是序列化名（枚举会落到 uint32），检视面板据此拿不到枚举。
    /// </summary>
    internal static string ResolveFieldEnumName(string componentTypeName, string fieldName)
    {
        if (componentTypeName.Length == 0 || fieldName.Length == 0) return string.Empty;
        Type? component = FindType(componentTypeName);
        if (component == null) return string.Empty;

        Type? fieldType = component.GetField(fieldName,
            BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)?.FieldType;
        if (fieldType == null)
            fieldType = component.GetProperty(fieldName,
                BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)?.PropertyType;
        return fieldType is { IsEnum: true } ? fieldType.FullName ?? fieldType.Name : string.Empty;
    }

    /// <summary>程序集装载或卸载时清空缓存：选项表带的是上一次会话的枚举定义。</summary>
    internal static void Clear() => cache.Clear();

    /// <summary>按类型名找回类型：内建类型在 Orbeden 程序集里，脚本类型按全名在已加载程序集里找。</summary>
    private static Type? FindType(string typeName)
    {
        Assembly bindings = typeof(Skybox).Assembly;
        Type? type = bindings.GetType(typeName) ?? bindings.GetType("Orbeden." + typeName);
        if (type != null) return type;

        foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            type = assembly.GetType(typeName);
            if (type != null) return type;
        }

        return null;
    }
}
