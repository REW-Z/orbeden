#pragma once

#include "Defines/types.h"
#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/RenderScene.h"
#include "Runtime/Gui/RetainedGuiBridge.h"

#include <unordered_map>
#include <unordered_set>

//覆盖层的输出目标：默认窗口帧缓冲（句柄为 0），或编辑器预览用的离屏目标。
struct UIOutputTarget
{
    GpuRenderTargetID renderTarget;
    int32 x = 0;
    int32 y = 0;
    int32 width = 0;
    int32 height = 0;
};

//RetainedGUI 的渲染器。只消费已发布的帧：上传网格、按命令绘制、维护覆盖率池。
//不做布局、不做批次决策、也不遍历控件树——那些都在托管侧。
class UIRenderer
{
public:
    //接入后端与资源管理器并创建内置资源；后端变化时重建。
    bool Initialize(RenderBackend& renderBackend, GpuResourceManager& resources);

    //释放全部 GPU 资源。
    void Shutdown();

    //离屏画布：在相机场景之前绘制，输出到画布自己的纹理。
    void RenderOffscreen();

    //世界空间画布：画进相机的场景缓冲，随后与场景一起走输出 Pass。
    void RenderWorldSpace(const RenderCamera& camera);

    //屏幕画布：画到指定输出目标，必须排在 ImGui 之前。
    void RenderOverlay(const UIOutputTarget& target);

    //设置显示区域的逻辑尺寸；视图快照要用它把窗口逻辑点换算到画布坐标。
    void SetDisplayLogicalSize(vector2 size);

    //让某视图的缓存失效；分辨率或目标变化后调用。
    void InvalidateView(uint64 viewId);

    //读取已呈现帧里某视图的窗口深度；视图或帧号对不上时返回假。
    bool ReadDepth(uint64 viewId, uint64 viewerId, uint64 presentedFrame,
        int32 x, int32 y, float32& depth) const;

    //让全部视图的缓存失效。
    void InvalidateAll();

    //本帧提交的普通绘制命令数，供性能面板读取。
    uint64 GetDrawCommandCount() const { return drawCommandCount; }

private:
    //一个网格的 GPU 缓冲与其内容版本。
    struct MeshGpu
    {
        GpuVertexBufferID vertexBuffer;
        GpuIndexBufferID indexBuffer;
        GpuVertexInputID vertexInput;
        usize vertexCapacity = 0;
        uint32 indexCapacity = 0;
        uint64 revision = 0;
        uint64 lastUsedFrame = 0;
        bool valid = false;
    };

    //覆盖率池里的一块 R8 目标。
    struct CoverageTarget
    {
        GpuRenderTargetID renderTarget;
        GpuTextureID texture;
        int32 width = 0;
        int32 height = 0;
        //压入本层时的父层下标；出栈时据此恢复。
        int32 parent = -1;
        bool inUse = false;
    };

    //一次画布绘制的目标描述。
    struct CanvasTarget
    {
        GpuRenderTargetID renderTarget;
        int32 x = 0;
        int32 y = 0;
        int32 width = 0;
        int32 height = 0;
        //离屏输出每帧从透明黑开始；屏幕与世界空间画布覆盖在已有内容之上，不清屏。
        bool clearOnFirstPass = false;
    };

    //一个视图最近一次呈现时的深度来源；命中快照按它回读深度。
    struct ViewDepth
    {
        uint64 frameId = 0;
        GpuRenderTargetID renderTarget;
        //视图在目标里的像素原点；窗口坐标要减掉它才是目标内坐标。
        int32 x = 0;
        int32 y = 0;
    };

    //深度来源的键：画布标识加观察者标识；屏幕与离屏视图的观察者为 0。
    struct ViewKey
    {
        uint64 viewId = 0;
        uint64 viewerId = 0;
        bool operator==(const ViewKey& other) const
        {
            return viewId == other.viewId && viewerId == other.viewerId;
        }
    };

    struct ViewKeyHash
    {
        usize operator()(const ViewKey& key) const
        {
            //两个 64 位标识按黄金比例混合：世界空间下画布相同、相机不同也要落进不同的槽。
            usize left = static_cast<usize>(key.viewId * 0x9E3779B97F4A7C15ull);
            usize right = static_cast<usize>(key.viewerId * 0xC2B2AE3D27D4EB4Full);
            return left ^ (right + 0x9E3779B9u + (left << 6) + (left >> 2));
        }
    };

    RenderBackend* backend = nullptr;
    GpuResourceManager* resources = nullptr;
    GpuShaderProgramID surfaceProgram;
    GpuShaderProgramID coverageProgram;
    GpuTextureID whiteTexture;
    std::unordered_map<uint64, MeshGpu> meshes;
    //视图到深度来源；屏幕与世界空间视图才有场景深度，离屏输出没有。
    //深度来源按"画布 + 观察者"分槽：多相机下同一块画布各有各的目标与深度。
    std::unordered_map<ViewKey, ViewDepth, ViewKeyHash> viewDepths;
    List<CoverageTarget> coveragePool;
    //当前画布正在使用的覆盖率目标下标；-1 表示根层，覆盖率为 1。
    int32 currentCoverage = -1;
    //裁剪目标分配失败后要跳过的嵌套层数；大于零时不画任何图形。
    int32 skippedClipDepth = 0;
    //当前画布的画布空间到裁剪空间矩阵，裁剪 Pass 也要用。
    matrix4x4 currentViewProjection;
    //当前绑定的绘制程序：显式材质会换程序，换回来时要重设帧级 uniform。
    GpuShaderProgramID boundProgram;
    //当前 Pass 里材质纹理的起始槽位；0 与 1 留给 UI 纹理与软遮罩覆盖率。
    static constexpr uint32 MaterialTextureSlotBase = 2;
    //已经为无效材质报过诊断的来源，只报一次。
    std::unordered_set<int32> invalidMaterials;
    //当前画布的目标，裁剪 Pass 结束后要恢复。
    CanvasTarget currentTarget;
    //下一次进入当前目标时是否清屏；只对离屏输出的首次进入为真。
    bool clearNextPass = false;
    uint64 drawCommandCount = 0;
    uint64 syncedFrame = 0;
    //已发布视图快照对应的帧号；换帧时清空重发。
    uint64 viewPublishFrame = 0;
    //显示区域的逻辑尺寸；由渲染系统在窗口创建与缩放时写入。
    vector2 displayLogicalSize;
    //当前世界空间画布对应的相机矩阵；屏幕画布发布单位矩阵。
    matrix4x4 cameraView;
    matrix4x4 cameraProjection;
    bool publishingWorldSpace = false;
    //本次绘制所属的观察相机（Ens ID）；世界空间画布的视图快照与提交过滤都用它。
    uint64 publishingViewer = 0;
    uint64 cameraId = 0;
    bool offscreenWarned = false;

    //把一块画布的命令画一遍。canvasWorld 非空时与提交里的矩阵复合，
    //世界空间画布正是用相机投影乘它自己的世界矩阵。
    void RenderCanvas(const RetainedGuiFrame::CanvasView& canvas, const CanvasTarget& target,
        const matrix4x4* canvasWorld);
    //按命令种类绘制一条命令。
    void RenderCommand(const UIDrawCommand& command, const matrix4x4* matrices, int32 matrixCount);
    //绘制一条普通命令。
    void DrawCommand(const UIDrawCommand& command, const matrix4x4* matrices, int32 matrixCount);
    //入栈一层裁剪：取目标、清零、投影形状、写 父覆盖率*本层。
    void PushCoverage(const UIDrawCommand& command, const matrix4x4* matrices, int32 matrixCount);
    //出栈一层裁剪：恢复父覆盖率并归还目标。
    void PopCoverage();

    //上传本帧用到的网格，并回收连续多帧没人引用的网格。
    void SyncMeshes(uint64 frameId);
    //确保网格的 GPU 缓冲存在且内容最新。
    const MeshGpu* EnsureMesh(uint64 meshId, uint64 frameId);
    //释放一个网格的 GPU 缓冲。
    void ReleaseMesh(MeshGpu& mesh);

    //取得一块可用覆盖率目标；分配失败返回空。
    int32 AcquireCoverage(int32 width, int32 height);
    //归还一块覆盖率目标。
    void ReleaseCoverage(int32 index);

    //创建内置资源；失败时记录错误并保持不可用。
    bool EnsurePrograms();
    //设置目标与视口，并绑定图元 shader 的公共 uniform。
    void RestoreSurfaceState();
    //解析一条命令引用的纹理；缺省时用白纹理。
    GpuTextureID ResolveTexture(int32 textureObjectId) const;

    //显式材质：取材质、切程序并绑定材质参数；保留参数由 DrawCommand 随后覆盖。
    void SetFrameUniforms();
    void BindCommandProgram(const UIDrawCommand& command);
    const GpuMaterial* GetCommandMaterial(const UIDrawCommand& command);
    const GpuShaderPass* GetCommandMaterialPass(const UIDrawCommand& command);
    void ReportInvalidMaterial(uint64 materialObjectId, const char* reason);
    static bool IsReservedUniform(const std::string& name);
    //只清屏，不画任何命令；循环依赖的离屏画布用它。
    void ClearTarget(const CanvasTarget& target);
};
