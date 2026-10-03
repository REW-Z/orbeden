using Orbeden;

namespace OrbedenEditor;

/// <summary>场景手柄这一帧的交互状态。</summary>
public enum EditorSceneHandleState
{
    /// <summary>既没悬停也没拖动。</summary>
    Idle = 0,
    /// <summary>鼠标停在上面，尚未按下。</summary>
    Hovered = 1,
    /// <summary>正在被拖动；鼠标可能已经离开方块。</summary>
    Dragging = 2,
}

/// <summary>
/// 托管场景手柄的公共原语。自定义编辑器只描述"手柄画在哪、多大"，
/// 命中、按住、拖拽中不断线这些交给 ImGui 的条目状态处理：
/// 一个正在拖动的方块就是当前活动条目，场景拾取会因此让路，不会在你松手时顺手选走别的对象。
/// 调用点必须处在 SceneView 的绘制上下文里（即 ComponentEditor.OnSceneGui）。
/// </summary>
public static class EditorSceneHandles
{
    /// <summary>默认手柄方块的边长（屏幕像素）。</summary>
    public const float DefaultHandleSize = 10.0f;

    /// <summary>当前鼠标的屏幕坐标；没有绑定原生层时返回零。</summary>
    public static vector2 MousePosition => NativeEditorGUI.GetMousePos();

    /// <summary>把世界点投影到场景屏幕坐标，见 <see cref="Gizmos.ProjectPoint"/>。</summary>
    public static bool ProjectPoint(vector3 world, out vector2 screen) => Gizmos.ProjectPoint(world, out screen);

    /// <summary>
    /// 在屏幕位置放一个方形手柄。<paramref name="center"/> 是方块中心，
    /// 返回它这一帧的状态；拖动状态会一直保持到松开鼠标为止。
    /// </summary>
    public static EditorSceneHandleState Draw(string id, vector2 center, color tint,
        float size = DefaultHandleSize)
    {
        if (string.IsNullOrEmpty(id) || !float.IsFinite(size) || size <= 0.0f) return EditorSceneHandleState.Idle;
        if (!float.IsFinite(center.x) || !float.IsFinite(center.y)) return EditorSceneHandleState.Idle;

        float half = size * 0.5f;
        //光标先挪到方块左上角，InvisibleButton 才是原地投放；ImGui 只认最后一次摆放。
        NativeEditorGUI.SetCursorScreenPos(new vector2(center.x - half, center.y - half));
        NativeEditorGUI.InvisibleButton(id, new vector2(size, size));

        bool active = NativeEditorGUI.IsItemActive();
        bool hovered = active || NativeEditorGUI.IsItemHovered();

        //方块本体：没碰到时压暗，悬停提亮、拖动实心，状态一眼能看出来。
        rectBuffer[0] = new EditorRectPrimitive
        {
            MinX = center.x - half,
            MinY = center.y - half,
            MaxX = center.x + half,
            MaxY = center.y + half,
            R = tint.r,
            G = tint.g,
            B = tint.b,
            A = active ? 1.0f : hovered ? 0.8f : 0.5f,
            Rounding = 0.0f,
        };
        NativeEditorGUI.DrawRects(rectBuffer, 1);

        return active ? EditorSceneHandleState.Dragging
            : hovered ? EditorSceneHandleState.Hovered
            : EditorSceneHandleState.Idle;
    }

    //手柄是每帧重复绘制的小方块：复用同一块缓冲，避免每次拖拽都产生垃圾。
    private static readonly EditorRectPrimitive[] rectBuffer = new EditorRectPrimitive[1];
}
