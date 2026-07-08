using Orbeden;

namespace ExampleGame;

/// <summary>示例项目的运行时 GUI。</summary>
public static class GuiOverlay
{
    private static int clickCount;

    /// <summary>绘制示例运行时 GUI。</summary>
    public static void Draw()
    {
        bool visible = GUI.BeginPanel("C# Runtime GUI");
        try
        {
            if (!visible) return;

            GUI.Label("Hello from OrbedenCore.CSharp");
            if (GUI.Button("Click from C#"))
            {
                clickCount++;
            }

            GUI.Label($"Clicks: {clickCount}");
        }
        finally
        {
            GUI.EndPanel();
        }
    }
}
