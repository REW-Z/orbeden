using System;
using System.Collections.Generic;

namespace OrbedenEditor;

/// <summary>
/// 资源检视面板的类型扩展点。资源不是组件，拿不到 ComponentEditor，
/// 因此这里按原生类型名登记绘制回调；面板在对象清单里遇到匹配类型时调用它。
/// </summary>
public static class EditorAssetInspectors
{
    //回调收到资源 Key 与原生对象 ID；对象 ID 为 0 表示该子资源还没加载。
    private static readonly Dictionary<string, Action<string, int>> Inspectors = new(StringComparer.Ordinal);

    /// <summary>登记一个类型的资源检视；同名类型重复登记时后一次覆盖。</summary>
    public static void Register(string nativeTypeName, Action<string, int> draw)
    {
        ArgumentException.ThrowIfNullOrEmpty(nativeTypeName);
        ArgumentNullException.ThrowIfNull(draw);
        Inspectors[nativeTypeName] = draw;
    }

    /// <summary>注销一个类型的资源检视。</summary>
    public static void Unregister(string nativeTypeName) => Inspectors.Remove(nativeTypeName);

    /// <summary>清空全部登记。</summary>
    public static void Clear() => Inspectors.Clear();

    /// <summary>取一个类型的绘制回调；没有登记时返回空。</summary>
    internal static Action<string, int>? Find(string nativeTypeName) =>
        Inspectors.TryGetValue(nativeTypeName, out Action<string, int>? draw) ? draw : null;

    /// <summary>当前登记数量。</summary>
    public static int Count => Inspectors.Count;
}
