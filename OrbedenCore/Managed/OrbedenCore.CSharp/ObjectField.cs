using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>ObjectField 中可选择的资源对象。</summary>
public readonly struct ObjectFieldOption
{
    public string ResourceKey { get; }
    public string DisplayName { get; }

    /// <summary>创建一个资源选择项。</summary>
    public ObjectFieldOption(string resourceKey, string displayName)
    {
        ResourceKey = resourceKey ?? string.Empty;
        DisplayName = displayName ?? string.Empty;
    }
}

/// <summary>为 ObjectField 提供编辑器资源列表和加载能力。</summary>
public interface IObjectFieldAssetProvider
{
    /// <summary>读取指定对象类型可选择的资源。</summary>
    IReadOnlyList<ObjectFieldOption> GetAssets(Type objectType);

    /// <summary>加载资源对象。</summary>
    Object? Load(Type objectType, string resourceKey);
}
