using Orbeden;

namespace OrbedenEditor;

/// <summary>显示原生场景渲染结果的中央场景 Panel。</summary>
/// <remarks>本面板只是承载视口的固定工作区叶子，不画面板外壳；渲染铺满整窗、交互只用面板内容区，
/// 两套矩形的约定见 OrbedenEditor/Src/Editor/EditorScene.cpp 顶部说明。</remarks>
internal sealed class ScenePanel : EditorPanel
{
    public override EditorPanelInfo Info => new("scene", "Scene", true,
        new vector2(960, 540), PanelDockPlacement.Center, 0.6f, 50, true);

    //绘制原生场景视口并接收预制体投放
    protected override void DrawContent(EditorPanelContext context)
    {
        EditorGUI.DrawSceneView();
        DrawDropTarget();
    }

    //按鼠标命中位置实例化场景投放的预制体
    private static void DrawDropTarget()
    {
        string key = NativeEditorGUI.ReadDrag(out int kind);
        if (key.Length == 0) return;

        bool valid = !EditorApplication.IsPlaying && kind == 2
            && key.EndsWith(".prefab", StringComparison.Ordinal);
        if (!NativeEditorGUI.AcceptDrag(valid)) return;

        //释放位置在提交时才解析，避免悬停期间反复投射
        if (!EditorGUI.ResolveSceneDropPosition(out vector3 position)) return;
        EditorPrefabActions.Instantiate(key, EnsId.Null, EnsId.Null, true, position);
    }
}
