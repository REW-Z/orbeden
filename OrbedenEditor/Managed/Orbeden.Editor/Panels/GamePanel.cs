using Orbeden;

namespace OrbedenEditor;

/// <summary>显示完整的游戏画面</summary>
internal sealed class GamePanel : EditorPanel
{
    public override EditorPanelInfo Info => new("game", "Game", false,
        new vector2(960, 540), PanelDockPlacement.Center, 0.6f, 55, false, false);

    //绘制游戏画面
    protected override void DrawContent(EditorPanelContext context)
    {
        EditorGUI.DrawGameView();
    }
}
