using System;
using System.Runtime.InteropServices;
using Orbeden;

namespace OrbedenEditor;

internal enum PanelDockPlacement
{
    Center,
    Left,
    Right,
    Top,
    Bottom,
    Floating,
}

/// <summary>Panel 名称和默认布局信息。</summary>
internal readonly struct EditorPanelInfo
{
    public string Id { get; }
    public string Title { get; }
    public bool DefaultVisible { get; }
    public vector2 DefaultSize { get; }
    public PanelDockPlacement DefaultDock { get; }
    public float DefaultDockRatio { get; }
    public int Order { get; }

    /// <summary>创建一份硬编码 Panel 信息。</summary>
    public EditorPanelInfo(string id,
        string title,
        bool defaultVisible,
        vector2 defaultSize,
        PanelDockPlacement defaultDock,
        float defaultDockRatio,
        int order)
    {
        Id = id;
        Title = title;
        DefaultVisible = defaultVisible;
        DefaultSize = defaultSize;
        DefaultDock = defaultDock;
        DefaultDockRatio = defaultDockRatio;
        Order = order;
    }
}

/// <summary>当前 Panel 绘制所需的编辑器上下文。</summary>
internal readonly struct EditorPanelContext
{
    public EnsId SelectedEns { get; }
    public IReadOnlyList<EnsId> SelectedEnsList { get; }
    public IReadOnlyList<string> SelectedStableIds { get; }
    public string SelectedStableId { get; }

    /// <summary>创建当前帧 Panel 上下文。</summary>
    public EditorPanelContext(EnsId selectedEns, IReadOnlyList<EnsId> selectedEnsList,
        IReadOnlyList<string> selectedStableIds, string selectedStableId)
    {
        SelectedEns = selectedEns;
        SelectedEnsList = selectedEnsList;
        SelectedStableIds = selectedStableIds;
        SelectedStableId = selectedStableId;
    }
}

/// <summary>C# Editor Panel 基类。</summary>
internal abstract class EditorPanel
{
    public abstract EditorPanelInfo Info { get; }

    /// <summary>绘制 Panel 内容。</summary>
    public abstract void Draw(EditorPanelContext context);

    /// <summary>Panel 显示时调用。</summary>
    public virtual void OnShown() { }

    /// <summary>Panel 隐藏时调用。</summary>
    public virtual void OnHidden() { }

    /// <summary>用户游戏程序集加载时调用。</summary>
    public virtual void OnGameAssemblyLoaded(string assemblyPath, string sidecarPath) { }

    /// <summary>用户游戏程序集卸载时调用。</summary>
    public virtual void OnGameAssemblyUnloaded() { }

    /// <summary>资源路径变化后同步 Panel 内部缓存。</summary>
    public virtual void OnAssetReferencesRemapped(string oldKey, string newKey, bool prefix) { }

    /// <summary>统一保存当前 Panel 暂存的项目数据。</summary>
    public virtual bool SavePendingChanges() => true;
}

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorPanelNativeApi
{
    public IntPtr Context;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, byte*, int, byte, float, float, int, float, int, byte> RegisterPanel;
}
#pragma warning restore CS0649
