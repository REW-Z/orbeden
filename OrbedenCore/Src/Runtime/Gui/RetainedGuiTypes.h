#pragma once

#include "Defines/types.h"
#include "Rendering/RenderTypes.h"
#include "Runtime/EngineTypes.h"
#include "Runtime/EnsId.h"

//RetainedGUI 的跨语言数据合同。字段顺序、尺寸与对齐由两侧的静态断言钉住，
//托管侧对应 CoreCS/RetainedGuiTypes.cs。修改任何结构都必须同时改两侧并复核断言。

#pragma pack(push, 8)

//一条批量提交的派生位置。clear 非零时忽略 position，只清除该 Ens 的覆盖。
struct UIDerivedPosition
{
    EnsId ens;
    vector3 position;
    uint32 clear = 0;
};

//网格更新：把一段顶点与索引写进某个网格。
struct UIMeshUpdate
{
    uint64 meshId = 0;
    uint64 revision = 0;
    uint32 vertexOffset = 0;
    uint32 vertexCount = 0;
    uint32 indexOffset = 0;
    uint32 indexCount = 0;
};

//绘制命令的种类。
enum class UIDrawCommandKind : uint32
{
    Draw = 0,
    PushRectangle = 1,
    PushImageAlpha = 2,
    Pop = 3,
};

//一条绘制命令。裁剪命令用 meshId/matrixIndex 表示形状，纹理与阈值表示 Alpha 来源与阈值。
struct UIDrawCommand
{
    uint64 meshId = 0;
    int32 textureObjectId = 0;
    uint32 materialKind = 0;
    uint32 matrixIndex = 0;
    uint32 commandKind = 0;
    uint32 firstIndex = 0;
    uint32 indexCount = 0;
    float32 distanceRange = 4.0f;
    float32 threshold = 0.0f;
    color tint;
    //显式材质的对象 ID；0 表示用引擎内置 UI 材质。
    //着色器与参数由原生材质系统解析，托管侧只给身份。
    uint64 materialObjectId = 0;
};

//一块画布一帧的提交内容。
struct UICanvasSubmission
{
    uint64 frameId = 0;
    uint64 canvasId = 0;
    uint64 viewId = 0;
    int32 outputTextureObjectId = 0;
    uint32 renderMode = 0;
    int32 sortOrder = 0;
    uint32 drawLayer = 1;
    int32 width = 0;
    int32 height = 0;
    matrix4x4 viewProjection;
};

//UIView 的标志位。
enum class UIViewFlags : uint32
{
    Primary = 1,
    EditorPreview = 2,
    WorldSpaceCamera = 4,
    Presented = 8,
};

//一个 UI 视图的呈现状态。
struct UIView
{
    uint64 viewId = 0;
    uint64 presentedFrame = 0;
    //这块视图属于哪台相机；屏幕与离屏视图为 0。
    uint64 viewerId = 0;
    int32 width = 0;
    int32 height = 0;
    vector2 logicalOrigin;
    vector2 logicalSize;
    matrix4x4 view;
    matrix4x4 projection;
    uint32 flags = 0;
};

//世界结构变化记录的种类。
enum class UISceneChangeKind : uint32
{
    Added = 0,
    Removed = 1,
    Reparented = 2,
    ActiveChanged = 3,
    TransformChanged = 4,
    FieldsChanged = 5,
};

//世界结构变化记录的标志位。
enum class UISceneChangeFlags : uint32
{
    //写入来自布局派生位置，接收方不应据此反向触发重建。
    DerivedWrite = 1,
};

//一条世界结构变化记录。
struct UISceneChange
{
    uint64 sequence = 0;
    uint64 worldRevision = 0;
    EnsId ens;
    EnsId parent;
    int32 objectId = 0;
    uint32 kind = 0;
    uint32 flags = 0;
};

//输入记录的种类。数值是跨语言合同，只能追加在尾部。
enum class UIInputKind : uint32
{
    PointerMove = 0,
    PointerDown = 1,
    PointerUp = 2,
    PointerCancel = 3,
    Wheel = 4,
    KeyDown = 5,
    KeyUp = 6,
    TextCommit = 7,
    CompositionStart = 8,
    CompositionUpdate = 9,
    CompositionCommit = 10,
    CompositionCancel = 11,
    WindowFocusLost = 12,
    GamepadState = 13,
};

//输入设备。pointerId=0 为鼠标，触摸 id 从 1 开始。
enum class UIInputDevice : uint32
{
    Mouse = 0,
    Touch = 1,
    Gamepad = 2,
    Keyboard = 3,
};

//指针按键；触摸固定按左键。
enum class UIPointerButton : uint32
{
    Left = 0,
    Right = 1,
    Middle = 2,
};

//手柄的轴与键。
enum class UIGamepadKey : uint32
{
    AxisX = 0,
    AxisY = 1,
    Submit = 2,
    Cancel = 3,
};

//修饰键位。
enum class UIInputModifiers : uint32
{
    Shift = 1,
    Control = 2,
    Alt = 4,
};

//一条有序输入事件。sequence 单调递增并保留到达顺序；文本放在同帧的 UTF-8 池里。
struct UIInputRecord
{
    uint64 sequence = 0;
    uint64 textSession = 0;
    float64 timestamp = 0.0;
    uint32 windowId = 0;
    uint32 pointerId = 0;
    uint32 kind = 0;
    uint32 device = 0;
    uint32 key = 0;
    uint32 modifiers = 0;
    vector2 position;
    vector2 delta;
    float32 value = 0.0f;
    uint32 textOffset = 0;
    uint32 textLength = 0;
    uint32 caretScalar = 0;
};

//字形请求。fontRevision 变化时原生侧要重新取字体面。
struct UIGlyphRequest
{
    int32 fontObjectId = 0;
    uint32 scalar = 0;
    uint32 glyphIndex = 0;
    uint32 rasterMode = 0;
    uint32 pixelSize = 0;
    uint64 fontRevision = 0;
};

//字形结果与像素布局。byteOffset/byteCount 指向本次查询返回的像素缓冲。
struct UIGlyphResult
{
    uint32 glyphIndex = 0;
    float32 advance = 0.0f;
    float32 bearingX = 0.0f;
    float32 bearingY = 0.0f;
    float32 width = 0.0f;
    float32 height = 0.0f;
    int32 bitmapWidth = 0;
    int32 bitmapHeight = 0;
    int32 channels = 0;
    int32 rowStride = 0;
    float32 originX = 0.0f;
    float32 originY = 0.0f;
    uint32 byteOffset = 0;
    uint32 byteCount = 0;
};

#pragma pack(pop)

//UI 片段使用的材质种类，决定着色器分支与距离场处理方式。
enum class UIMaterialKind : uint32
{
    ImageStraight = 0,
    ImagePremultiplied = 1,
    Bitmap = 2,
    SDF = 3,
    MSDF = 4,
};

//UI 顶点。偏移 0/12/20，stride 36，按顶点缓冲直接上传。
#pragma pack(push, 4)

struct UIVertex
{
    vector3 position;
    vector2 uv;
    color tint;
};

#pragma pack(pop)

static_assert(sizeof(UIMeshUpdate) == 32);
static_assert(sizeof(UIDrawCommand) == 64);
static_assert(offsetof(UIDrawCommand, distanceRange) == 32);
static_assert(offsetof(UIDrawCommand, tint) == 40);
static_assert(offsetof(UIDrawCommand, materialObjectId) == 56);
static_assert(sizeof(UICanvasSubmission) == 112);
static_assert(offsetof(UICanvasSubmission, viewProjection) == 48);
static_assert(sizeof(UIView) == 184);
static_assert(offsetof(UIView, viewerId) == 16);
static_assert(offsetof(UIView, view) == 48);
static_assert(offsetof(UIView, flags) == 176);
static_assert(sizeof(UISceneChange) == 48);
static_assert(offsetof(UISceneChange, objectId) == 32);
static_assert(sizeof(UIInputRecord) == 80);
static_assert(offsetof(UIInputRecord, position) == 48);
static_assert(offsetof(UIInputRecord, textOffset) == 68);
static_assert(sizeof(UIGlyphRequest) == 32);
static_assert(offsetof(UIGlyphRequest, fontRevision) == 24);
static_assert(sizeof(UIGlyphResult) == 56);
static_assert(offsetof(UIGlyphResult, byteOffset) == 48);
static_assert(sizeof(UIDerivedPosition) == 24);
static_assert(offsetof(UIDerivedPosition, position) == 8);
static_assert(offsetof(UIDerivedPosition, clear) == 20);
static_assert(sizeof(UIVertex) == 36);
static_assert(offsetof(UIVertex, position) == 0);
static_assert(offsetof(UIVertex, uv) == 12);
static_assert(offsetof(UIVertex, tint) == 20);
