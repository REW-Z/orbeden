#pragma once

#include "Defines/types.h"
#include "Runtime/Gui/RetainedGuiTypes.h"
#include "Runtime/Native/NativeApiAbi.h"
#include "Runtime/Native/NativeCall.h"

#pragma pack(push, 8)

//RetainedGUI 原生函数表，由 C# 薄层持有并调用。表头之后是固定顺序的指针槽。
//槽位只增不改：新增能力一律追加在表尾，既有槽位的含义与顺序保持稳定。
struct RetainedGuiApi
{
public:
    uint32 version = 2;
    uint32 structSize = sizeof(RetainedGuiApi);

    //0 创建上下文。返回 0 表示失败；句柄高 32 位为代次、低 32 位为槽位。
    void* CreateContext = nullptr;
    //1 销毁上下文。重复销毁无副作用。
    void* DestroyContext = nullptr;
    //2 提交网格变更。
    void* UpdateMeshes = nullptr;
    //3 释放不再被引用的网格。
    void* RemoveMeshes = nullptr;
    //4 提交一块画布的命令与矩阵；暂存到本帧，EndFrame 才发布。
    void* SubmitCanvas = nullptr;
    //5 原子发布本帧。
    void* EndFrame = nullptr;
    //6 读取视图快照；返回所需条数，容量不足不部分写。
    void* ReadViews = nullptr;
    //7 读取输入事件；返回所需条数，容量不足不部分写、不消费。
    void* ReadInput = nullptr;
    //8 确认消费输入事件。
    void* ConsumeInput = nullptr;
    //9 批量写入布局派生的本地位置。
    void* ApplyDerivedPositions = nullptr;
    //10 读取已呈现帧的窗口深度；viewerId 挑出多相机下这条视图属于哪一台。
    void* ReadDepth = nullptr;
    //11 查询字形度量。
    void* QueryGlyphs = nullptr;
    //12 光栅化字形；返回所需字节数。
    void* RasterizeGlyphs = nullptr;
    //13 读取两个字形的字距。
    void* GetKerning = nullptr;
    //14 设置文本输入焦点与光标矩形。
    void* SetTextInput = nullptr;
    //15 读取剪贴板；返回所需 UTF-8 字节数。
    void* ReadClipboard = nullptr;
    //16 写入剪贴板。
    void* WriteClipboard = nullptr;
    //17 读取世界结构变化；返回所需条数并写出是否需要整表重建。
    void* ReadChanges = nullptr;
    //18 读取主显示目标的像素尺寸；屏幕画布首帧的视口引导靠它，没有它就只能等
    //   画布先被渲染过一次才拿得到视图，而没视口又不会被提交。
    void* ReadDisplaySize = nullptr;

    //创建完整函数表。
    static RetainedGuiApi Create();
};

#pragma pack(pop)

//取得进程期稳定只读表。返回值在整个进程内有效，原生 UI 上下文的生命周期与它无关。
const RetainedGuiApi* GetRetainedGuiApiTable();

//渲染侧读取已发布帧的入口。上下文内部结构不外露，这里只给渲染器需要的那几样。
//所有指针在下一个 EndFrame 之前保持有效。
namespace RetainedGuiFrame
{
    //一块已发布画布的命令与矩阵。
    struct CanvasView
    {
        const UICanvasSubmission* submission = nullptr;
        const UIDrawCommand* commands = nullptr;
        int32 commandCount = 0;
        const matrix4x4* matrices = nullptr;
        int32 matrixCount = 0;
    };

    //已发布帧号；没有任何已发布内容时为 0。
    uint64 GetPublishedFrame();

    //收集已发布帧的全部画布，返回条数。
    int32 CollectCanvases(List<CanvasView>& out);

    //取网格内容。revision 与上次不同说明内容变了，需要重新上传；
    //返回假表示网格已经不存在，持有它的 GPU 缓冲可以释放。
    bool GetMesh(uint64 meshId, uint64& revision, const UIVertex*& vertices, int32& vertexCount,
        const uint32*& indices, int32& indexCount);

    //主显示目标的像素尺寸。渲染器每帧报一次：屏幕画布的首帧视口只能来自它，
    //否则"没视口就不提交、不提交就不会有视图"会锁死。
    void SetDisplaySize(int32 width, int32 height);
    bool GetDisplaySize(int32& width, int32& height);

    //渲染器每帧为每块画布发布一次视图快照：托管侧据此知道显示区域的像素尺寸。
    //快照落后渲染一帧，属于设计内的采样延迟。
    void BeginViewPublish();
    //logicalSize 是显示区域的逻辑尺寸，logicalOrigin 是视图在目标里的像素原点；
    //view/projection 只对世界空间画布有意义，托管侧用它反投影指针射线。
    //viewerId 是这台视图的观察相机；屏幕与离屏视图传 0。
    void PublishView(uint64 canvasId, int32 width, int32 height, uint32 flags,
        const vector2& logicalOrigin, const vector2& logicalSize,
        const matrix4x4& viewMatrix, const matrix4x4& projectionMatrix, uint64 viewerId = 0);

    //深度回读由渲染系统提供：ABI 层不依赖渲染模块，只留一个函数指针。
    //reader(viewId, presentedFrame, x, y, depth) 返回假表示这一帧没有可读的深度。
    using DepthReader = bool (*)(uint64 viewId, uint64 viewerId, uint64 presentedFrame,
        int32 x, int32 y, float32* depth);
    void SetDepthReader(DepthReader reader);
}

//表头 8 字节（version 与 structSize），其后是 19 个 Cdecl 槽位。
//托管侧按同一顺序构造委托，两侧尺寸不符会在各自构建期拦下。
static_assert(sizeof(RetainedGuiApi) == 8 + sizeof(void*) * 19, "RetainedGuiApi ABI slot count changed.");
