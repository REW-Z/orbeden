#pragma once

#include "Editor/EditorGizmoAbi.h"
#include "Rendering/RenderTypes.h"
#include "Runtime/EnsId.h"

#include <vector>

class EditorScene;
class World;

//场景手柄的编辑模式。
enum class EditorGizmoMode : int32
{
    Move = 0,
    Rotate = 1,
    Scale = 2,
};

//场景手柄的坐标系：世界轴或物体局部轴。
enum class EditorGizmoOrientation : int32
{
    Global = 0,
    Local = 1,
};

//手柄当前指向的子控件。
enum class EditorGizmoHandle : int32
{
    None = 0,
    AxisX,
    AxisY,
    AxisZ,
    PlaneXY,
    PlaneYZ,
    PlaneXZ,
    Center,
};

//场景手柄一帧所需的相机与视口状态；绘制与命中必须共用这里的 viewProjection。
struct EditorGizmoView
{
public:
    matrix4x4 viewProjection;
    vector3 cameraRight = { 1.0f, 0.0f, 0.0f };
    vector3 cameraUp = { 0.0f, 1.0f, 0.0f };
    vector3 cameraForward = { 0.0f, 0.0f, -1.0f };
    vector2 renderPosition = { 0.0f, 0.0f };
    vector2 renderSize = { 0.0f, 0.0f };
    bool valid = false;
};

//Scene 视口的选择手柄：命中测试、拖拽数学与绘制。
class EditorGizmoHandles
{
public:
    //创建手柄并绑定所属场景
    explicit EditorGizmoHandles(EditorScene& owner);

    //获取手柄编辑模式
    EditorGizmoMode GetMode() const;
    //设置手柄编辑模式
    void SetMode(EditorGizmoMode value);
    //获取手柄坐标系
    EditorGizmoOrientation GetOrientation() const;
    //设置手柄坐标系
    void SetOrientation(EditorGizmoOrientation value);

    //每帧处理命中与拖拽；拖拽期间直接写入 Transform
    void Update(World& world, const EditorGizmoView& view, bool interactive);
    //在场景叠加层最上方绘制手柄
    void Draw(World& world, const EditorGizmoView& view) const;

    //取出一次待提交的编辑记录，没有时返回 false
    bool TakeEdit(EditorGizmoEdit& edit);
    //取消拖拽并还原起始变换
    void CancelDrag(World& world);

    //判断手柄是否正在拖拽
    bool IsDragging() const;
    //判断本帧是否占用了场景左键（拖拽中或命中了某个手柄）
    bool OwnsMouse() const;

    //把世界点投影到场景视口屏幕坐标
    static bool ProjectPoint(const matrix4x4& viewProjection, const vector2& renderPosition,
        const vector2& renderSize, const vector3& point, vector2& screen);
    //把场景视口屏幕坐标转换成世界射线，distance 为近到远平面之间的未归一化长度
    static bool BuildScreenRay(const matrix4x4& viewProjection, const vector2& renderPosition,
        const vector2& renderSize, const vector2& screenPosition, vector3& origin, vector3& direction,
        float32& distance);

private:
    //一次拖拽中单个目标的起始状态
    struct Target
    {
    public:
        EnsId ens;
        vector3 worldPosition;
        quaternion worldRotation;
        matrix4x4 parentWorldMatrix;
        quaternion parentWorldRotation;
        EditorGizmoVector3 localPosition;
        EditorGizmoQuaternion localRotation;
        EditorGizmoVector3 localScale;
    };

    //一帧的手柄几何上下文，命中与绘制共用
    struct Context
    {
    public:
        bool valid = false;
        vector3 pivot;
        vector3 axes[3];
        float32 worldPerPixel = 1.0f;
        quaternion pivotRotation;
    };

    //收集选择集并算出枢轴与三个手柄轴
    bool BuildContext(World& world, const EditorGizmoView& view, Context& context) const;
    //屏幕空间命中测试，返回命中的子控件
    EditorGizmoHandle Pick(const EditorGizmoView& view, const Context& context, const vector2& mouse) const;
    //拖拽开始时缓存全部目标与起始状态
    void BeginDrag(World& world, const EditorGizmoView& view, const Context& context, EditorGizmoHandle handle);
    //按当前模式推进一次拖拽
    void ApplyDrag(World& world, const EditorGizmoView& view);
    //还原拖拽起始状态
    void RestoreTargets(World& world) const;
    //记录一条待提交的编辑
    void PushEdit(const EditorGizmoEdit& edit);
    //解析单个目标的局部到世界缓存
    static bool CaptureTarget(World& world, EnsId ens, Target& target);

    EditorScene& scene;
    EditorGizmoMode mode = EditorGizmoMode::Move;
    EditorGizmoOrientation orientation = EditorGizmoOrientation::Global;
    EditorGizmoHandle hovered = EditorGizmoHandle::None;
    EditorGizmoHandle active = EditorGizmoHandle::None;
    bool dragActive = false;
    std::vector<Target> targets;
    std::vector<EditorGizmoEdit> pendingEdits;
    vector3 pivotWorld;
    vector3 startAxes[3];
    float32 startAxisParameter = 0.0f;
    float32 startAxisReference = 0.0f;
    float32 startMouseAngle = 0.0f;
    vector3 startPlaneHit;
    vector2 startMouse;
    vector2 startPivotScreen;
    float32 startMouseDistance = 1.0f;
    //本次缩放的当前比例，绘制轴端方块时让它跟着外移
    float32 currentScaleFactor = 1.0f;
};
