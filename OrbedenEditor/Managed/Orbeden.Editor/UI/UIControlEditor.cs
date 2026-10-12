using Orbeden;

namespace OrbedenEditor;

/// <summary>绘制控件属性、OrbEvent 和交互配置提示。</summary>
[CustomEditor(typeof(UIControl), true)]
public sealed class UIControlEditor : ComponentEditor
{
    /// <summary>绘制控件检视面板。</summary>
    public override void OnDrawInspector()
    {
        if (Target.Ens.GetComponent<Button>() is Button button)
        {
            Image? image = button.Ens.GetComponent<Image>();
            if (image == null)
                EditorGUI.Warning("This button requires an Image on the same node.");
            else if (!image.GetRaycastTarget())
                EditorGUI.Warning("The button's Image has Raycast Target disabled.");
        }
        DrawDefaultInspector();
    }
}
