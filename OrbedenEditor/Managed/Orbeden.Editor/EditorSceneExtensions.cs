using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Orbeden;

namespace OrbedenEditor;

/// <summary>场景点击使用的射线与可见距离上限。</summary>
public readonly record struct EditorScenePickRay(vector3 Origin, vector3 Direction, float MaximumDistance);

/// <summary>场景拾取结果；与原生桥共用布局。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct EditorScenePickHit
{
    public EnsId Ens;
    public vector3 Position;
}

/// <summary>源码包提供的场景辅助绘制与拾取扩展。</summary>
public interface IEditorSceneExtension
{
    /// <summary>绘制无需选择对象的场景辅助内容。</summary>
    void DrawOverlay();
    /// <summary>按场景射线选择可见对象。</summary>
    bool TryPick(in EditorScenePickRay ray, out EditorScenePickHit hit);
}

/// <summary>场景扩展注册表；编辑器只接收命中结果，不依赖具体 UI 组件类型。</summary>
public static class EditorSceneExtensions
{
    private static readonly Dictionary<string, IEditorSceneExtension> extensions = new(StringComparer.Ordinal);

    /// <summary>登记或替换场景扩展。</summary>
    public static void Register(string id, IEditorSceneExtension extension)
    {
        ArgumentException.ThrowIfNullOrEmpty(id);
        ArgumentNullException.ThrowIfNull(extension);
        extensions[id] = extension;
    }

    /// <summary>移除场景扩展并释放程序集引用。</summary>
    public static void Unregister(string id) => extensions.Remove(id);

    //绘制各个扩展的公共辅助内容
    internal static void DrawOverlay()
    {
        foreach (IEditorSceneExtension extension in extensions.Values)
        {
            try { extension.DrawOverlay(); }
            catch (Exception exception) { Console.Error.WriteLine($"Scene overlay failed: {exception}"); }
        }
    }

    //合并各个扩展的拾取结果
    internal static bool TryPick(in EditorScenePickRay ray, out EditorScenePickHit hit)
    {
        hit = default;
        float closest = ray.MaximumDistance;
        bool found = false;
        foreach (IEditorSceneExtension extension in extensions.Values)
        {
            try
            {
                if (!extension.TryPick(ray, out EditorScenePickHit candidate) || candidate.Ens.IsNull) continue;
                vector3 p = candidate.Position;
                float distance = (p.x - ray.Origin.x) * ray.Direction.x
                    + (p.y - ray.Origin.y) * ray.Direction.y + (p.z - ray.Origin.z) * ray.Direction.z;
                if (!float.IsFinite(distance) || distance < 0 || distance > closest + 1e-4f) continue;
                hit = candidate;
                closest = distance;
                found = true;
            }
            catch (Exception exception) { Console.Error.WriteLine($"Scene picking failed: {exception}"); }
        }
        return found;
    }
}
