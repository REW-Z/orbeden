using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>网格更新：把一段顶点与索引写进某个网格。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct UIMeshUpdate
{
    /// <summary>目标网格。</summary>
    public ulong meshId;

    /// <summary>内容版本；推进表示顶点布局发生变化。</summary>
    public ulong revision;

    /// <summary>顶点数据在本次上传缓冲里的起始下标。</summary>
    public uint vertexOffset;

    /// <summary>顶点数量。</summary>
    public uint vertexCount;

    /// <summary>索引数据在本次上传缓冲里的起始下标。</summary>
    public uint indexOffset;

    /// <summary>索引数量。</summary>
    public uint indexCount;
}

/// <summary>绘制命令的种类。</summary>
public enum UIDrawCommandKind : uint
{
    /// <summary>绘制一个片段。</summary>
    Draw = 0,

    /// <summary>压入矩形裁剪。</summary>
    PushRectangle = 1,

    /// <summary>按纹理 Alpha 压入软裁剪。</summary>
    PushImageAlpha = 2,

    /// <summary>弹出当前裁剪层。</summary>
    Pop = 3,
}

/// <summary>一条绘制命令。裁剪命令用 meshId/matrixIndex 表示形状，纹理与阈值表示 Alpha 来源。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct UIDrawCommand
{
    /// <summary>绘制使用的网格；裁剪命令用它表示形状。</summary>
    public ulong meshId;

    /// <summary>纹理运行时 ID；0 表示引擎白纹理。</summary>
    public int textureObjectId;

    /// <summary>材质种类，取 UIMaterialKind。</summary>
    public uint materialKind;

    /// <summary>本画布矩阵数组里的下标。</summary>
    public uint matrixIndex;

    /// <summary>命令种类，取 UIDrawCommandKind。</summary>
    public uint commandKind;

    /// <summary>片段在网格索引缓冲里的起始下标。</summary>
    public uint firstIndex;

    /// <summary>片段的索引数量。</summary>
    public uint indexCount;

    /// <summary>距离场取值范围。</summary>
    public float distanceRange;

    /// <summary>软裁剪阈值。</summary>
    public float threshold;

    /// <summary>提交颜色。</summary>
    public color tint;

    /// <summary>显式材质的对象 ID；0 表示用引擎内置 UI 材质。</summary>
    public ulong materialObjectId;
}

/// <summary>一块画布一帧的提交内容。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct UICanvasSubmission
{
    /// <summary>所属帧。</summary>
    public ulong frameId;

    /// <summary>画布标识，取自组件运行时 ID。</summary>
    public ulong canvasId;

    /// <summary>目标视图。</summary>
    public ulong viewId;

    /// <summary>世界空间画布的观察相机（Ens ID）；0 表示不限定，任何相机都可以画。</summary>
    public ulong viewerId;

    /// <summary>离屏画布的输出纹理运行时 ID；屏幕画布为 0。</summary>
    public int outputTextureObjectId;

    /// <summary>渲染方式，取 CanvasRenderMode。</summary>
    public uint renderMode;

    /// <summary>排序权重。</summary>
    public int sortOrder;

    /// <summary>绘制层。</summary>
    public uint drawLayer;

    /// <summary>目标像素宽度。</summary>
    public int width;

    /// <summary>目标像素高度。</summary>
    public int height;

    /// <summary>视图投影矩阵。</summary>
    public matrix4x4 viewProjection;
}

/// <summary>UIView 的标志位。</summary>
public static class UIViewFlags
{
    /// <summary>主显示目标。</summary>
    public const uint Primary = 1;

    /// <summary>编辑器预览目标。</summary>
    public const uint EditorPreview = 2;

    /// <summary>世界空间相机视图。</summary>
    public const uint WorldSpaceCamera = 4;

    /// <summary>本帧已经成功呈现。</summary>
    public const uint Presented = 8;
}

/// <summary>一个 UI 视图的呈现状态。用于屏幕画布的正交投影与逻辑坐标换算。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct UIView
{
    /// <summary>视图标识。</summary>
    public ulong viewId;

    /// <summary>最近一次成功呈现的帧号；命中快照按它取用。</summary>
    public ulong presentedFrame;

    /// <summary>这块视图属于哪台相机；屏幕与离屏视图为 0。</summary>
    public ulong viewerId;

    /// <summary>目标像素宽度。</summary>
    public int width;

    /// <summary>目标像素高度。</summary>
    public int height;

    /// <summary>逻辑坐标原点，等于目标显示区域在窗口逻辑坐标里的左下角。</summary>
    public vector2 logicalOrigin;

    /// <summary>逻辑尺寸。</summary>
    public vector2 logicalSize;

    /// <summary>视图矩阵。</summary>
    public matrix4x4 view;

    /// <summary>投影矩阵。</summary>
    public matrix4x4 projection;

    /// <summary>标志位，取 UIViewFlags。</summary>
    public uint flags;
}

/// <summary>输入记录的种类；与原生 UIInputKind 一致，只能追加在尾部。</summary>
public enum UIInputKind : uint
{
    /// <summary>指针移动。</summary>
    PointerMove = 0,

    /// <summary>指针按下。</summary>
    PointerDown = 1,

    /// <summary>指针抬起。</summary>
    PointerUp = 2,

    /// <summary>指针取消；失焦、隐藏、销毁等场景。</summary>
    PointerCancel = 3,

    /// <summary>滚轮。</summary>
    Wheel = 4,

    /// <summary>按键按下。</summary>
    KeyDown = 5,

    /// <summary>按键抬起。</summary>
    KeyUp = 6,

    /// <summary>文本提交。</summary>
    TextCommit = 7,

    /// <summary>输入法组合开始。</summary>
    CompositionStart = 8,

    /// <summary>输入法组合更新。</summary>
    CompositionUpdate = 9,

    /// <summary>输入法组合提交。</summary>
    CompositionCommit = 10,

    /// <summary>输入法组合取消。</summary>
    CompositionCancel = 11,

    /// <summary>窗口失焦。</summary>
    WindowFocusLost = 12,

    /// <summary>手柄状态。</summary>
    GamepadState = 13,
}

/// <summary>输入设备；pointerId=0 为鼠标，触摸 id 从 1 开始。</summary>
public enum UIInputDevice : uint
{
    /// <summary>鼠标。</summary>
    Mouse = 0,

    /// <summary>触摸。</summary>
    Touch = 1,

    /// <summary>手柄。</summary>
    Gamepad = 2,

    /// <summary>键盘。</summary>
    Keyboard = 3,
}

/// <summary>指针按键；触摸固定按左键。</summary>
public enum UIPointerButton : uint
{
    /// <summary>左键。</summary>
    Left = 0,

    /// <summary>右键。</summary>
    Right = 1,

    /// <summary>中键。</summary>
    Middle = 2,
}

/// <summary>手柄的轴与键。</summary>
public enum UIGamepadKey : uint
{
    /// <summary>横轴。</summary>
    AxisX = 0,

    /// <summary>纵轴。</summary>
    AxisY = 1,

    /// <summary>提交键。</summary>
    Submit = 2,

    /// <summary>取消键。</summary>
    Cancel = 3,
}

/// <summary>修饰键位。</summary>
public enum UIInputModifiers : uint
{
    /// <summary>Shift。</summary>
    Shift = 1,

    /// <summary>Control。</summary>
    Control = 2,

    /// <summary>Alt。</summary>
    Alt = 4,
}

/// <summary>一条有序输入事件。sequence 单调递增并保留到达顺序；文本放在同帧的 UTF-8 池里。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct UIInputRecord
{
    /// <summary>到达序号。</summary>
    public ulong sequence;

    /// <summary>文本输入会话标识；非文本事件为 0。</summary>
    public ulong textSession;

    /// <summary>平台时间戳，秒。</summary>
    public double timestamp;

    /// <summary>窗口标识。</summary>
    public uint windowId;

    /// <summary>指针标识；鼠标为 0，触摸从 1 开始。</summary>
    public uint pointerId;

    /// <summary>事件种类。</summary>
    public uint kind;

    /// <summary>设备种类。</summary>
    public uint device;

    /// <summary>按键标识；含义由设备决定。</summary>
    public uint key;

    /// <summary>修饰键位：Shift=1、Control=2、Alt=4。</summary>
    public uint modifiers;

    /// <summary>窗口逻辑坐标位置。</summary>
    public vector2 position;

    /// <summary>本帧增量。</summary>
    public vector2 delta;

    /// <summary>数值；手柄轴与滚轮用它。</summary>
    public float value;

    /// <summary>文本在本帧 UTF-8 池中的偏移。</summary>
    public uint textOffset;

    /// <summary>文本字节数。</summary>
    public uint textLength;

    /// <summary>组合态光标所在的标量下标。</summary>
    public uint caretScalar;
}

/// <summary>字形请求。fontRevision 变化时原生侧重新取字体面。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct UIGlyphRequest
{
    /// <summary>字体资源运行时 ID。</summary>
    public int fontObjectId;

    /// <summary>Unicode 标量。</summary>
    public uint scalar;

    /// <summary>字形下标；为 0 表示由标量查表。</summary>
    public uint glyphIndex;

    /// <summary>光栅化模式，取 FontRasterMode。</summary>
    public uint rasterMode;

    /// <summary>位图模式的像素字号；距离场模式为 0。</summary>
    public uint pixelSize;

    /// <summary>字体内容版本。</summary>
    public ulong fontRevision;
}

/// <summary>字形结果与像素布局。byteOffset/byteCount 指向本次查询返回的像素缓冲。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct UIGlyphResult
{
    /// <summary>字形下标。</summary>
    public uint glyphIndex;

    /// <summary>水平步进，字体单位换算后的结果。</summary>
    public float advance;

    /// <summary>位图相对原点的水平偏移。</summary>
    public float bearingX;

    /// <summary>位图相对原点的垂直偏移。</summary>
    public float bearingY;

    /// <summary>位图宽度，像素。</summary>
    public float width;

    /// <summary>位图高度，像素。</summary>
    public float height;

    /// <summary>位图实际像素宽度。</summary>
    public int bitmapWidth;

    /// <summary>位图实际像素高度。</summary>
    public int bitmapHeight;

    /// <summary>通道数：R8 为 1，RGB8 为 3。</summary>
    public int channels;

    /// <summary>每行字节数。</summary>
    public int rowStride;

    /// <summary>位图原点相对字形原点的水平偏移。</summary>
    public float originX;

    /// <summary>位图原点相对字形原点的垂直偏移。</summary>
    public float originY;

    /// <summary>像素在本批缓冲中的字节偏移。</summary>
    public uint byteOffset;

    /// <summary>像素字节数。</summary>
    public uint byteCount;
}

/// <summary>UI 片段使用的材质种类；决定着色器分支与距离场处理方式。</summary>
public enum UIMaterialKind : uint
{
    /// <summary>直通 Alpha 的普通图片。</summary>
    ImageStraight = 0,

    /// <summary>预乘 Alpha 的图片，画布输出走这一支。</summary>
    ImagePremultiplied = 1,

    /// <summary>位图字形。</summary>
    Bitmap = 2,

    /// <summary>单通道距离场字形。</summary>
    SDF = 3,

    /// <summary>多通道距离场字形。</summary>
    MSDF = 4,
}

/// <summary>
/// UI 顶点。跨语言布局与 Native/Runtime/Gui/RetainedGuiTypes.h 的 UIVertex 一致：
/// 偏移 0/12/20，stride 36，Pack=4。
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct UIVertex
{
    /// <summary>本地空间位置。</summary>
    public vector3 position;

    /// <summary>纹理坐标，原点在左下。</summary>
    public vector2 uv;

    /// <summary>线性颜色。</summary>
    public color tint;
}

/// <summary>一条批量提交的派生位置。clear 非零时忽略 position，只清除该 Ens 的覆盖。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct UIDerivedPosition
{
    /// <summary>目标 Ens。</summary>
    public EnsId ens;

    /// <summary>派生位置，写入 Transform 的解析位置覆盖。</summary>
    public vector3 position;

    /// <summary>非零表示清除覆盖，恢复作者位置。</summary>
    public uint clear;
}

/// <summary>世界结构变化记录的种类；取值与原生 RetainedGuiTypes.h 一致。</summary>
public enum UISceneChangeKind : uint
{
    /// <summary>Ens 或 UI 组件新挂载。</summary>
    Added = 0,

    /// <summary>Ens 或组件被移除。</summary>
    Removed = 1,

    /// <summary>父级变化。</summary>
    Reparented = 2,

    /// <summary>层级活动状态变化。</summary>
    ActiveChanged = 3,

    /// <summary>解析位置变化。</summary>
    TransformChanged = 4,

    /// <summary>组件字段被外部整体应用。</summary>
    FieldsChanged = 5,
}

/// <summary>世界结构变化记录的标志位。</summary>
public static class UISceneChangeFlags
{
    /// <summary>写入来自布局派生位置，读取方不应据此反向触发重建。</summary>
    public const uint DerivedWrite = 1;
}

/// <summary>
/// 一条世界结构变化记录。跨语言布局与 Native/Runtime/Gui/RetainedGuiTypes.h 的
/// UISceneChange 一致，字段顺序与尺寸由两侧 static_assert 钉住。
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct UISceneChange
{
    /// <summary>到达顺序，单调递增。</summary>
    public ulong sequence;

    /// <summary>产生记录时的世界代次；与当前代次不符的记录直接丢弃。</summary>
    public ulong worldRevision;

    /// <summary>发生变化的 Ens。</summary>
    public EnsId ens;

    /// <summary>新的父 Ens；未变化时为空。</summary>
    public EnsId parent;

    /// <summary>发生变化的组件运行时 ID；仅 Ens 级变化时为 0。</summary>
    public int objectId;

    /// <summary>变化种类，取 UISceneChangeKind。</summary>
    public uint kind;

    /// <summary>标志位，取 UISceneChangeFlags。</summary>
    public uint flags;
}
