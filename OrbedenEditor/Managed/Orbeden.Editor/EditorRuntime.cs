using System;
using System.Runtime.InteropServices;
using Orbeden;

namespace OrbedenEditor;

/// <summary>Editor 托管入口，由 C++ EditorSystem 调用。</summary>
public static class EditorRuntime
{
    private static int panelClickCount;

    /// <summary>初始化 Editor 托管桥接。</summary>
    [UnmanagedCallersOnly]
    public static void Initialize(IntPtr editorGizmoApi)
    {
        Gizmos.Initialize(editorGizmoApi);
    }

    /// <summary>绘制 C# Editor 面板。</summary>
    [UnmanagedCallersOnly]
    public static void DrawPanels()
    {
        bool visible = GUI.BeginPanel("C# Editor Panel");
        try
        {
            if (!visible) return;

            GUI.Label("Managed editor panel");
            if (GUI.Button("Editor C# Button"))
            {
                panelClickCount++;
            }

            GUI.Label($"Clicks: {panelClickCount}");
        }
        finally
        {
            GUI.EndPanel();
        }
    }

    /// <summary>绘制 C# SceneView Gizmos。</summary>
    [UnmanagedCallersOnly]
    public static void DrawSceneGizmos()
    {
        Gizmos.Line(new vector3(-1.5f, 0.05f, 0.0f), new vector3(1.5f, 0.05f, 0.0f), new color4(0.95f, 0.25f, 0.20f, 1.0f));
        Gizmos.Line(new vector3(0.0f, 0.05f, -1.5f), new vector3(0.0f, 0.05f, 1.5f), new color4(0.20f, 0.80f, 0.95f, 1.0f));
        Gizmos.Label(new vector3(0.0f, 1.35f, 0.0f), "C# Gizmo");
    }
}
