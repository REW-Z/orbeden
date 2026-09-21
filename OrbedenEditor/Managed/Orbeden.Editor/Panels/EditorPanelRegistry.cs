using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

/// <summary>发现并调度当前 Editor 程序集中的 C# Panel。</summary>
internal static unsafe class EditorPanelRegistry
{
    private static readonly List<EditorPanel> panels = [];
    //与 panels 同索引：选择系统要知道持有选中项的面板是否还在显示
    private static readonly List<bool> panelVisible = [];

    /// <summary>发现 C# Panel 并推送给原生 PanelManager。</summary>
    public static bool Initialize(EditorPanelNativeApi api)
    {
        panels.Clear();
        panelVisible.Clear();
        if (api.RegisterPanel == null)
        {
            Console.Error.WriteLine("Editor Panel registration callback is null.");
            return false;
        }

        //创建面板并读取元数据
        List<(EditorPanel Panel, EditorPanelInfo Info, string TypeName)> candidates = [];
        IEnumerable<Type?> discoveredTypes;
        try
        {
            discoveredTypes = typeof(EditorPanelRegistry).Assembly.GetTypes();
        }
        catch (ReflectionTypeLoadException ex)
        {
            discoveredTypes = ex.Types;
            Console.Error.WriteLine($"Some Editor Panel types could not be loaded: {ex}");
        }

        foreach (Type? discoveredType in discoveredTypes)
        {
            if (discoveredType == null) continue;
            Type type = discoveredType;
            if (type.IsAbstract || !typeof(EditorPanel).IsAssignableFrom(type)) continue;

            try
            {
                if (Activator.CreateInstance(type, nonPublic: true) is not EditorPanel panel)
                {
                    Console.Error.WriteLine($"Editor Panel creation failed: {type.FullName}.");
                    continue;
                }

                EditorPanelInfo info = panel.Info;
                if (string.IsNullOrWhiteSpace(info.Id)
                    || info.DefaultSize.x <= 0.0f
                    || info.DefaultSize.y <= 0.0f)
                {
                    Console.Error.WriteLine($"Editor Panel metadata is invalid: {type.FullName}.");
                    continue;
                }

                candidates.Add((panel, info, type.FullName ?? type.Name));
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"Editor Panel discovery failed: {type.FullName}: {ex}");
            }
        }

        candidates.Sort((left, right) =>
        {
            int order = left.Info.Order.CompareTo(right.Info.Order);
            if (order != 0) return order;
            int id = string.Compare(left.Info.Id, right.Info.Id, StringComparison.Ordinal);
            return id != 0 ? id : string.Compare(left.TypeName, right.TypeName, StringComparison.Ordinal);
        });

        //记录已注册面板
        foreach (var (panel, info, _) in candidates)
        {
            byte[] id = Encoding.UTF8.GetBytes(info.Id);
            byte[] title = Encoding.UTF8.GetBytes(info.Title ?? string.Empty);
            fixed (byte* idPointer = id)
            fixed (byte* titlePointer = title)
            {
                byte accepted = api.RegisterPanel(api.Context,
                    panels.Count,
                    idPointer,
                    id.Length,
                    titlePointer,
                    title.Length,
                    info.DefaultVisible ? (byte)1 : (byte)0,
                    info.DefaultSize.x,
                    info.DefaultSize.y,
                    (int)info.DefaultDock,
                    info.DefaultDockRatio,
                    info.Order,
                    info.FixedWorkspace ? (byte)1 : (byte)0,
                    info.ShowBorder ? (byte)1 : (byte)0);
                if (accepted != 0)
                {
                    panels.Add(panel);
                    //默认可见性先落地：可见性没变过时原生侧不会回调 SetPanelVisible
                    panelVisible.Add(info.DefaultVisible);
                }
            }
        }

        return true;
    }

    /// <summary>绘制指定 C# Panel。</summary>
    public static void DrawPanel(int handle, EditorPanelContext context)
    {
        if (handle < 0 || handle >= panels.Count) return;

        panels[handle].Draw(context);
    }

    /// <summary>通知指定 C# Panel 显隐状态变化。</summary>
    public static void SetPanelVisible(int handle, bool visible)
    {
        if (handle < 0 || handle >= panels.Count) return;

        panelVisible[handle] = visible;
        try
        {
            if (visible) panels[handle].OnShown();
            else panels[handle].OnHidden();
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Editor Panel visibility callback failed: {panels[handle].GetType().FullName}: {ex}");
        }
    }

    /// <summary>按 id 查找仍在显示的面板；隐藏的面板不参与全局命令。</summary>
    public static EditorPanel? FindVisiblePanel(string panelId)
    {
        if (string.IsNullOrEmpty(panelId)) return null;
        for (int index = 0; index < panels.Count; index++)
        {
            if (!panelVisible[index]) continue;
            if (string.Equals(panels[index].Info.Id, panelId, StringComparison.Ordinal)) return panels[index];
        }
        return null;
    }

    /// <summary>向全部 C# Panel 广播游戏程序集加载事件。</summary>
    public static void LoadGameAssembly(string assemblyPath)
    {
        foreach (EditorPanel panel in panels)
        {
            try
            {
                panel.OnGameAssemblyLoaded(assemblyPath);
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"Editor Panel game assembly load failed: {panel.GetType().FullName}: {ex}");
            }
        }
    }

    /// <summary>向全部 C# Panel 广播游戏程序集卸载事件。</summary>
    public static void UnloadGameAssembly()
    {
        EditorObjectField.Clear();
        foreach (EditorPanel panel in panels)
        {
            try
            {
                panel.OnGameAssemblyUnloaded();
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"Editor Panel game assembly unload failed: {panel.GetType().FullName}: {ex}");
            }
        }
    }

    /// <summary>统一保存全部 Panel 的暂存项目数据。</summary>
    public static bool SavePendingChanges()
    {
        bool success = true;
        foreach (EditorPanel panel in panels)
        {
            try
            {
                success &= panel.SavePendingChanges();
            }
            catch (Exception ex)
            {
                success = false;
                Console.Error.WriteLine($"Editor Panel save failed: {panel.GetType().FullName}: {ex}");
            }
        }
        return success;
    }

    /// <summary>向全部 C# Panel 广播资源引用路径变化。</summary>
    public static void RemapAssetReferences(string oldKey, string newKey, bool prefix)
    {
        foreach (EditorPanel panel in panels)
        {
            try
            {
                panel.OnAssetReferencesRemapped(oldKey, newKey, prefix);
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"Editor Panel asset remap failed: {panel.GetType().FullName}: {ex}");
            }
        }
    }
}
