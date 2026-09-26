//SceneView（场景视口）的渲染与交互。
//
//渲染：
//- 渲染区域就是面板内容区：renderPosition/renderSize 取内容区左上角与可用尺寸，乘 DisplayFramebufferScale
//  得到离屏目标的像素尺寸。尺寸变化时整体重建（后端不支持原位 resize）；面板不可见或内容区过小时
//  释放目标并跳过渲染。可见性用 sceneView.visible 的单帧闩锁判断，本帧绘制过才为 true。
//- 场景图由 DrawSceneView 提交，选择轮廓、托管 Gizmo 与手柄随后叠在它上面。
//
//交互：
//- 能不能交互只看 interactPosition/interactSize（IsMouseOverSceneView），拾取、相机拖拽、手柄与
//  预制体投放都以它为闸门。
//- 投影与拾取共用 RenderCamera::viewProjectionMatrix（PrepareGizmoView 缓存），鼠标坐标按渲染矩形
//  换算；换用别的矩阵会让手柄位置与命中区错位。
//
//面板侧 ScenePanel 是固定工作区叶子：不可关闭、拖出或并入标签页，也不绘制面板外壳。

#include "Editor/EditorScene.h"

#include "Application.h"
#include "Editor/ManagedEditorBridge.h"
#include "Platform/GlfwWindow.h"
#include "Rendering/RenderMath.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/Object/Ens.h"
#include "Runtime/Native/NativeCall.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/Object/Object.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/World.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

EditorScene* EditorScene::activeScene = nullptr;

namespace
{
    //读取变换矩阵中一个轴的长度。
    float32 GetAxisLength(const matrix4x4& matrix, int32 offset)
    {
        float32 x = matrix.m[offset];
        float32 y = matrix.m[offset + 1];
        float32 z = matrix.m[offset + 2];
        return std::sqrt(x * x + y * y + z * z);
    }

    //把旋转矩阵转换为单位四元数。
    quaternion GetRotation(const matrix4x4& matrix, const vector3& scale)
    {
        float32 xScale = std::abs(scale.x) > 0.000001f ? scale.x : 1.0f;
        float32 yScale = std::abs(scale.y) > 0.000001f ? scale.y : 1.0f;
        float32 zScale = std::abs(scale.z) > 0.000001f ? scale.z : 1.0f;
        float32 m00 = matrix.m[0] / xScale;
        float32 m01 = matrix.m[4] / yScale;
        float32 m02 = matrix.m[8] / zScale;
        float32 m10 = matrix.m[1] / xScale;
        float32 m11 = matrix.m[5] / yScale;
        float32 m12 = matrix.m[9] / zScale;
        float32 m20 = matrix.m[2] / xScale;
        float32 m21 = matrix.m[6] / yScale;
        float32 m22 = matrix.m[10] / zScale;

        quaternion result;
        float32 trace = m00 + m11 + m22;
        if (trace > 0.0f)
        {
            float32 value = std::sqrt(trace + 1.0f) * 2.0f;
            result.w = 0.25f * value;
            result.x = (m21 - m12) / value;
            result.y = (m02 - m20) / value;
            result.z = (m10 - m01) / value;
        }
        else if (m00 > m11 && m00 > m22)
        {
            float32 value = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            result.w = (m21 - m12) / value;
            result.x = 0.25f * value;
            result.y = (m01 + m10) / value;
            result.z = (m02 + m20) / value;
        }
        else if (m11 > m22)
        {
            float32 value = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            result.w = (m02 - m20) / value;
            result.x = (m01 + m10) / value;
            result.y = 0.25f * value;
            result.z = (m12 + m21) / value;
        }
        else
        {
            float32 value = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            result.w = (m10 - m01) / value;
            result.x = (m02 + m20) / value;
            result.y = (m12 + m21) / value;
            result.z = 0.25f * value;
        }

        float32 length = std::sqrt(result.x * result.x + result.y * result.y + result.z * result.z + result.w * result.w);
        if (length <= 0.000001f) return quaternion();
        result.x /= length;
        result.y /= length;
        result.z /= length;
        result.w /= length;
        return result;
    }

    //把局部矩阵分解回Transform使用的TRS字段。
    void DecomposeTransform(const matrix4x4& matrix, vector3& position, quaternion& rotation, vector3& scale)
    {
        position = RenderMath::GetTranslation(matrix);
        scale = { GetAxisLength(matrix, 0), GetAxisLength(matrix, 4), GetAxisLength(matrix, 8) };

        vector3 xAxis = { matrix.m[0], matrix.m[1], matrix.m[2] };
        vector3 yAxis = { matrix.m[4], matrix.m[5], matrix.m[6] };
        vector3 zAxis = { matrix.m[8], matrix.m[9], matrix.m[10] };
        if (RenderMath::Dot(RenderMath::Cross(xAxis, yAxis), zAxis) < 0.0f) scale.x = -scale.x;
        rotation = GetRotation(matrix, scale);
    }


    constexpr float32 Pi = 3.14159265358979323846f;
    constexpr const char* EditorCameraId = "world://editor/camera";
    constexpr uint8 ExplicitSelection = 1;
    constexpr uint8 DescendantSelection = 2;

    EditorScene* CurrentGizmoScene = nullptr;

    struct WeldKey
    {
    public:
        int64 x = 0;
        int64 y = 0;
        int64 z = 0;

        /// <summary>判断两个焊接网格坐标是否一致。</summary>
        bool operator==(const WeldKey& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct WeldKeyHash
    {
        /// <summary>计算焊接网格坐标的哈希。</summary>
        usize operator()(const WeldKey& value) const
        {
            usize hash = std::hash<int64>()(value.x);
            hash ^= std::hash<int64>()(value.y) + static_cast<usize>(0x9e3779b9u) + (hash << 6) + (hash >> 2);
            hash ^= std::hash<int64>()(value.z) + static_cast<usize>(0x9e3779b9u) + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    //计算四元数乘积。
    quaternion Multiply(const quaternion& a, const quaternion& b)
    {
        return
        {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        };
    }

    //把相机欧拉角转换为旋转四元数。
    quaternion GetYawPitchRotation(float32 yawDegrees, float32 pitchDegrees)
    {
        float32 yaw = yawDegrees * Pi / 180.0f;
        float32 pitch = pitchDegrees * Pi / 180.0f;
        quaternion yawRotation = { 0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f) };
        quaternion pitchRotation = { std::sin(pitch * 0.5f), 0.0f, 0.0f, std::cos(pitch * 0.5f) };
        return Multiply(yawRotation, pitchRotation);
    }

    //获取相机前方向。
    vector3 GetForward(float32 yawDegrees, float32 pitchDegrees)
    {
        float32 yaw = yawDegrees * Pi / 180.0f;
        float32 pitch = pitchDegrees * Pi / 180.0f;
        float32 cosPitch = std::cos(pitch);
        return RenderMath::Normalize({ -std::sin(yaw) * cosPitch, std::sin(pitch), -std::cos(yaw) * cosPitch });
    }

    //获取相机右方向。
    vector3 GetRight(float32 yawDegrees)
    {
        float32 yaw = yawDegrees * Pi / 180.0f;
        return RenderMath::Normalize({ std::cos(yaw), 0.0f, -std::sin(yaw) });
    }

    //获取相机上方向。
    vector3 GetUp(float32 yawDegrees, float32 pitchDegrees)
    {
        return RenderMath::Normalize(RenderMath::Cross(GetRight(yawDegrees), GetForward(yawDegrees, pitchDegrees)));
    }

    //读取编辑器相机的半竖直视场角（弧度）；组件缺失时按 60 度算。平移按焦点平面换算像素尺寸要用它
    float32 GetHalfVerticalFov(World& world, EnsId cameraEns)
    {
        Ens* ens = world.GetEns(cameraEns);
        Camera* camera = ens ? ens->GetComponent<Camera>() : nullptr;
        return (camera ? std::clamp(camera->fieldOfView, 1.0f, 179.0f) : 60.0f) * Pi / 360.0f;
    }

    //滚轮推近的聚焦距离下限，免得把相机推到焦点上
    constexpr float32 MinimumFocusDistance = 0.05f;

    //滚轮每格改变聚焦距离的比例，是缩放手感的唯一旋钮：调小则贴近时更精细、拉远更慢，调大反之。
    //这里坚持全程等比，不加最小步长——一旦给每格兜底一个固定世界距离，近处就会一跳穿进模型、
    //远处又会慢得挪不动，等比曲线白设了。
    constexpr float32 ZoomStep = 0.1f;

    //聚焦动画的时长与缓动。双击是「带我过去」：起步要立刻响应，所以用缓出而不是缓入；
    //落位又要稳，所以靠三次方的尾巴把速度收干净。
    constexpr float32 FocusAnimationSeconds = 0.4f;

    float32 EaseOutCubic(float32 progress)
    {
        float32 remaining = 1.0f - progress;
        return 1.0f - remaining * remaining * remaining;
    }

    //相加两个三维向量。
    vector3 Add(const vector3& a, const vector3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    //缩放一个三维向量。
    vector3 Scale(const vector3& value, float32 scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    //按比例插值两个三维向量。
    vector3 Lerp(const vector3& from, const vector3& to, float32 amount)
    {
        return {
            from.x + (to.x - from.x) * amount,
            from.y + (to.y - from.y) * amount,
            from.z + (to.z - from.z) * amount };
    }

    //获取编辑器 GLFW 窗口。
    GLFWwindow* GetGlfwWindow(Application& app)
    {
        GlfwWindow* window = dynamic_cast<GlfwWindow*>(app.GetWindow());
        return window ? window->GetGlfwWindow() : nullptr;
    }

    //判断 GLFW 鼠标按钮是否按下。
    bool IsMouseDown(GLFWwindow* window, int32 button)
    {
        return window && glfwGetMouseButton(window, button) == GLFW_PRESS;
    }

    //判断是否有真实 ImGui 控件正在占用场景点击。
    bool HasBlockingImGuiActiveItem()
    {
        ImGuiContext* context = ImGui::GetCurrentContext();
        if (!context || context->ActiveId == 0) return false;

        bool windowBackgroundActive = context->ActiveIdWindow
            && context->ActiveId == context->ActiveIdWindow->MoveId
            && context->ActiveIdDisabledId == 0;
        return !windowBackgroundActive;
    }

    //把 EnsId 合并为本帧查找键。
    uint64 GetEnsKey(EnsId ens)
    {
        return (static_cast<uint64>(ens.version) << 32) | static_cast<uint64>(ens.id);
    }

    //选择类型对应的描边色：显式选择橙色，所选父节点的后代蓝色。
    color GetSelectionTint(uint8 selectionType)
    {
        return selectionType == ExplicitSelection
            ? color { 1.0f, 0.525f, 0.094f, 1.0f }
            : color { 0.227f, 0.569f, 1.0f, 1.0f };
    }

    //合并两个已有效的世界包围盒。
    bounds3 UnionBounds(const bounds3& a, const bounds3& b)
    {
        bounds3 result;
        const float32 aMin[3] = { a.center.x - a.extents.x, a.center.y - a.extents.y, a.center.z - a.extents.z };
        const float32 aMax[3] = { a.center.x + a.extents.x, a.center.y + a.extents.y, a.center.z + a.extents.z };
        const float32 bMin[3] = { b.center.x - b.extents.x, b.center.y - b.extents.y, b.center.z - b.extents.z };
        const float32 bMax[3] = { b.center.x + b.extents.x, b.center.y + b.extents.y, b.center.z + b.extents.z };
        float32* center = &result.center.x;
        float32* extents = &result.extents.x;
        for (int32 axis = 0; axis < 3; ++axis)
        {
            float32 minimum = std::min(aMin[axis], bMin[axis]);
            float32 maximum = std::max(aMax[axis], bMax[axis]);
            center[axis] = (minimum + maximum) * 0.5f;
            extents[axis] = (maximum - minimum) * 0.5f;
        }
        result.valid = true;
        return result;
    }

    //把 Ens 及其后代里所有网格的包围盒合并成世界包围盒；子树内没有网格时返回 false。
    //这里是聚焦用的：只吃已经装载的网格，不管渲染剔除与可见性。
    bool MergeSubtreeBounds(World& world, EnsId root, bounds3& merged)
    {
        bool hasBounds = false;
        List<EnsId> pending;
        pending.push_back(root);
        while (!pending.empty())
        {
            EnsId current = pending.back();
            pending.pop_back();
            Ens* ens = world.GetEns(current);
            Transform* transform = world.GetTransform(current);
            if (!ens || !transform) continue;

            StaticMeshRenderer* renderer = ens->GetComponent<StaticMeshRenderer>();
            Mesh* mesh = renderer ? renderer->GetRenderMesh() : nullptr;
            const bounds3* localBounds = mesh ? &mesh->GetLocalBounds() : nullptr;
            if (localBounds && localBounds->valid)
            {
                bounds3 worldBounds = RenderMath::TransformBounds(transform->worldMatrix, *localBounds);
                if (worldBounds.valid)
                {
                    merged = hasBounds ? UnionBounds(merged, worldBounds) : worldBounds;
                    hasBounds = true;
                }
            }

            for (EnsId child = transform->firstChild; !child.IsNull();)
            {
                pending.push_back(child);
                Transform* childTransform = world.GetTransform(child);
                child = childTransform ? childTransform->next : EnsId();
            }
        }
        return hasBounds;
    }

    //把线性颜色转换为 ImGui 颜色。
    ImU32 ToImColor(const EditorGizmoColor& color)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            std::clamp(color.r, 0.0f, 1.0f),
            std::clamp(color.g, 0.0f, 1.0f),
            std::clamp(color.b, 0.0f, 1.0f),
            std::clamp(color.a, 0.0f, 1.0f)));
    }

    /// <summary>读取当前场景选择，供 CustomEditor 派发选中回调。</summary>
    uint8 ORBEDEN_NATIVE_CALL IsGizmoEnsSelected(EnsId ens)
    {
        EditorScene* scene = EditorScene::GetActiveScene();
        return scene && scene->IsSelected(ens) ? 1 : 0;
    }

    /// <summary>读取组件 Gizmos 总开关。</summary>
    uint8 ORBEDEN_NATIVE_CALL AreGizmosVisible()
    {
        return CurrentGizmoScene && CurrentGizmoScene->GetGizmosVisible() ? 1 : 0;
    }

    //把三维点投影到屏幕坐标，与手柄共用同一套投影约定。
    bool ProjectGizmoPoint(const EditorGizmoVector3& point, ImVec2& screen)
    {
        if (!CurrentGizmoScene) return false;

        const EditorSceneViewState& view = CurrentGizmoScene->GetSceneViewState();
        vector2 projected;
        if (!EditorGizmoHandles::ProjectPoint(CurrentGizmoScene->GetGizmoViewProjection(),
            view.renderPosition, view.renderSize, { point.x, point.y, point.z }, projected))
            return false;

        screen.x = projected.x;
        screen.y = projected.y;
        return true;
    }

    //绘制一条托管三维 Handle 线。
    void ORBEDEN_NATIVE_CALL DrawGizmoLine(EditorGizmoVector3 a, EditorGizmoVector3 b, EditorGizmoColor color)
    {
        ImVec2 screenA;
        ImVec2 screenB;
        if (!ProjectGizmoPoint(a, screenA) || !ProjectGizmoPoint(b, screenB)) return;
        ImGui::GetWindowDrawList()->AddLine(screenA, screenB, ToImColor(color), 2.0f);
    }

    //取出一次待提交的手柄编辑，供托管侧每帧轮询。
    int32 ORBEDEN_NATIVE_CALL TakeGizmoEditNative(EditorGizmoEdit* edit)
    {
        EditorScene* scene = edit ? EditorScene::GetActiveScene() : nullptr;
        return scene && scene->TakeGizmoEdit(*edit) ? 1 : 0;
    }

    //绘制手柄模式图标：位移四向箭头、旋转半圆弧、缩放方块加实心角。
    void DrawHandleModeIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
        EditorGizmoMode mode, ImU32 color)
    {
        ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        float32 side = std::min(max.x - min.x, max.y - min.y);
        float32 arm = side * 0.32f;

        if (mode == EditorGizmoMode::Move)
        {
            drawList->AddLine(ImVec2(center.x - arm, center.y), ImVec2(center.x + arm, center.y), color, 1.6f);
            drawList->AddLine(ImVec2(center.x, center.y - arm), ImVec2(center.x, center.y + arm), color, 1.6f);
            drawList->AddTriangleFilled(ImVec2(center.x + arm, center.y),
                ImVec2(center.x + arm - 4.0f, center.y - 3.0f), ImVec2(center.x + arm - 4.0f, center.y + 3.0f), color);
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - arm),
                ImVec2(center.x - 3.0f, center.y - arm + 4.0f), ImVec2(center.x + 3.0f, center.y - arm + 4.0f), color);
        }
        else if (mode == EditorGizmoMode::Rotate)
        {
            constexpr int32 Segments = 20;
            ImVec2 points[Segments + 1];
            for (int32 index = 0; index <= Segments; ++index)
            {
                float32 angle = 4.71238898f + 5.49778714f
                    * static_cast<float32>(index) / static_cast<float32>(Segments);
                points[index] = ImVec2(center.x + std::cos(angle) * arm,
                    center.y + std::sin(angle) * arm);
            }
            drawList->AddPolyline(points, Segments + 1, color, ImDrawFlags_None, 1.6f);
        }
        else
        {
            drawList->AddRect(ImVec2(center.x - arm, center.y - arm),
                ImVec2(center.x + arm * 0.2f, center.y + arm * 0.2f), color, 1.0f, ImDrawFlags_None, 1.6f);
            drawList->AddRectFilled(ImVec2(center.x + arm * 0.2f, center.y + arm * 0.2f),
                ImVec2(center.x + arm, center.y + arm), color);
        }
    }

    //绘制一个托管三维 Handle 标签。
    void ORBEDEN_NATIVE_CALL DrawGizmoLabel(EditorGizmoVector3 position, const uint8* text, int32 length)
    {
        ImVec2 screen;
        if (!ProjectGizmoPoint(position, screen)) return;

        const char* begin = text && length > 0 ? reinterpret_cast<const char*>(text) : "";
        const char* end = begin + std::max(length, 0);
        ImGui::GetWindowDrawList()->AddText(screen, IM_COL32(255, 245, 180, 255), begin, end);
    }
}

EditorScene::EditorScene(Application& application, ManagedEditorBridge& bridge)
    : app(application)
    , managedBridge(bridge)
    , gizmoHandles(*this)
{
    activeScene = this;
}

//释放场景视口的离屏目标。
EditorScene::~EditorScene()
{
    ReleaseSceneViewTarget();
    if (activeScene == this) activeScene = nullptr;
}

//获取当前活动的编辑器场景。
EditorScene* EditorScene::GetActiveScene()
{
    return activeScene;
}

//获取本帧场景视口矩形与像素尺寸。
const EditorSceneViewState& EditorScene::GetSceneViewState() const
{
    return sceneView;
}

//释放场景视口的离屏目标。
void EditorScene::ReleaseSceneViewTarget()
{
    RenderSystem* renderSystem = app.GetSystem<RenderSystem>();
    if (renderSystem && sceneTarget.IsValid()) renderSystem->DeleteRenderTarget(sceneTarget);
    sceneTarget = RenderTargetID();
    sceneTargetTexture = GpuTextureID();
    sceneTargetWidth = 0;
    sceneTargetHeight = 0;
}

//按场景视口可见性维护离屏目标并绑定编辑相机。
void EditorScene::RefreshSceneViewTarget(World& world)
{
    //本帧记录只对下一次刷新有效，未绘制即视为不可见
    int32 requestedWidth = sceneView.visible ? std::max(sceneView.pixelWidth, 1) : 0;
    int32 requestedHeight = sceneView.visible ? std::max(sceneView.pixelHeight, 1) : 0;
    sceneView.visible = false;

    //尺寸或可见性变化时整体重建，后端不支持原位 resize
    if (requestedWidth != sceneTargetWidth || requestedHeight != sceneTargetHeight)
    {
        ReleaseSceneViewTarget();
        if (RenderSystem* renderSystem = app.GetSystem<RenderSystem>())
        {
            if (requestedWidth > 0 && requestedHeight > 0)
            {
                sceneTarget = renderSystem->CreateRenderTarget(requestedWidth, requestedHeight);
                if (sceneTarget.IsValid())
                {
                    sceneTargetTexture = renderSystem->GetRenderTargetTexture(sceneTarget);
                    sceneTargetWidth = requestedWidth;
                    sceneTargetHeight = requestedHeight;
                }
            }
        }
    }

    //把编辑相机绑定到当前离屏目标，无目标时回到主窗口帧缓冲
    Ens* editorCamera = world.GetEns(cameraEns);
    Camera* camera = editorCamera ? editorCamera->GetComponent<Camera>() : nullptr;
    if (camera) camera->renderTargetId = sceneTarget.id;
}

//绘制原生场景视口图像并叠加轮廓与 Handles。
void EditorScene::DrawSceneView()
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 viewMin = ImGui::GetCursorScreenPos();
    ImVec2 viewSize = ImGui::GetContentRegionAvail();
    if (viewSize.x < 8.0f || viewSize.y < 8.0f)
    {
        //内容区过小时暂停视口渲染
        sceneView.visible = false;
        ImGui::Dummy(ImVec2(std::max(viewSize.x, 1.0f), std::max(viewSize.y, 1.0f)));
        return;
    }

    //渲染区域就是本面板内容区，不再铺满主窗口：渲染范围与交互范围一致
    sceneView.renderPosition = { viewMin.x, viewMin.y };
    sceneView.renderSize = { viewSize.x, viewSize.y };
    sceneView.interactPosition = { viewMin.x, viewMin.y };
    sceneView.interactSize = { viewSize.x, viewSize.y };
    sceneView.pixelWidth = std::max(static_cast<int32>(std::lround(viewSize.x * io.DisplayFramebufferScale.x)), 1);
    sceneView.pixelHeight = std::max(static_cast<int32>(std::lround(viewSize.y * io.DisplayFramebufferScale.y)), 1);
    sceneView.visible = true;

    //离屏纹理原点在左下角，交换 V 轴与 ImGui 的左上角原点对齐
    if (sceneTargetTexture.id != 0)
        ImGui::Image(static_cast<ImTextureID>(sceneTargetTexture.id), viewSize,
            ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    else
        ImGui::Dummy(viewSize);

    DrawSceneOverlay();
}

//在背景绘制列表上提交选择轮廓与托管 Handles。
void EditorScene::DrawSceneOverlay()
{
    World& world = app.GetWorld();
    RenderSystem* renderSystem = app.GetSystem<RenderSystem>();
    if (!renderSystem || sceneView.renderSize.x <= 0.0f || sceneView.renderSize.y <= 0.0f) return;

    const RenderScene& scene = renderSystem->GetCurrentScene();
    //手柄与托管 Gizmo 共用渲染这张离屏图用的相机矩阵；手柄的写入必须早于拾取
    PrepareGizmoView(scene);
    DrawGizmoToolbar();
    DrawManagedGizmos();
    UpdateGizmoHandles(world);
    managedBridge.DrawSceneGizmos(true);
    HandleSelection(scene);
    SubmitSelectionHighlight(world);
    componentGizmos.Draw(world, *this, gizmoView);
    DrawGizmoHandles();
}

//更新编辑器观察相机。
void EditorScene::Update(World& world, float32 deltaTime, float32 mouseWheel)
{
    //编辑器相机是临时对象，每帧都在写它，不能因此把场景标成有改动
    World::DirtySuppressionScope suppression(world);
    CreateEditorCamera(world);

    Transform* transform = world.GetTransform(cameraEns);
    GLFWwindow* window = GetGlfwWindow(app);
    if (!transform || !window) return;

    //聚焦动画先走一步；下面任何手动操控都会把它打断
    vector3 animatedPosition {};
    if (AdvanceFocusAnimation(deltaTime, animatedPosition)) transform->SetLocalPosition(animatedPosition);

    bool cameraOwnsMouse = cameraMouseDragging || IsMouseOverSceneView();
    if (cameraOwnsMouse)
    {
        //判断当前鼠标拖拽模式。
        bool altHeld = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS
            || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        int32 mode = 0;
        if (IsMouseDown(window, GLFW_MOUSE_BUTTON_RIGHT)) mode = altHeld ? 3 : 1;
        else if (IsMouseDown(window, GLFW_MOUSE_BUTTON_MIDDLE)) mode = 2;
        else if (IsMouseDown(window, GLFW_MOUSE_BUTTON_LEFT) && altHeld) mode = 1;

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        if (mode != 0)
        {
            //手一动就接管，免得动画和鼠标抢着写位置
            focusAnimating = false;
            if (cameraMouseDragging && cameraMouseMode == mode)
            {
                float32 deltaX = static_cast<float32>(mouseX - previousMouseX);
                float32 deltaY = static_cast<float32>(mouseY - previousMouseY);
                float32 halfFov = GetHalfVerticalFov(world, cameraEns);
                if (mode == 1)
                {
                    //原地转头：按右键拖拽，位置不动
                    cameraYaw -= deltaX * 0.12f;
                    cameraPitch = std::clamp(cameraPitch - deltaY * 0.12f, -82.0f, 82.0f);
                }
                else if (mode == 2)
                {
                    //平移：一个像素在焦点平面上对应多少世界单位，速度就跟着聚焦距离走
                    float32 worldPerPixel = 2.0f * cameraFocusDistance * std::tan(halfFov)
                        / std::max(sceneView.renderSize.y, 1.0f);
                    vector3 pan = Add(
                        Scale(GetRight(cameraYaw), -deltaX * worldPerPixel),
                        Scale(GetUp(cameraYaw, cameraPitch), deltaY * worldPerPixel));
                    transform->SetLocalPosition(Add(transform->GetLocalPosition(), pan));
                }
                else
                {
                    //环绕旋转：焦点钉在世界里，相机绕着它摆。转完再滚轮，推近的仍是同一个物体
                    vector3 focus = Add(transform->GetLocalPosition(),
                        Scale(GetForward(cameraYaw, cameraPitch), cameraFocusDistance));
                    cameraYaw -= deltaX * 0.12f;
                    cameraPitch = std::clamp(cameraPitch - deltaY * 0.12f, -82.0f, 82.0f);
                    transform->SetLocalPosition(Add(focus,
                        Scale(GetForward(cameraYaw, cameraPitch), -cameraFocusDistance)));
                }
            }

            cameraMouseDragging = true;
            cameraMouseMode = mode;
            previousMouseX = mouseX;
            previousMouseY = mouseY;
        }
        else
        {
            cameraMouseDragging = false;
            cameraMouseMode = 0;
        }

        //滚轮推近：每格按当前聚焦距离成比例地改，焦点保持不动、相机沿视线挪过去。
        //等比才是「越近越慢」的来源——贴近物体时每格只挪一点点，拉远时每格挪得多，不至于滚半天。
        if (mouseWheel != 0.0f)
        {
            focusAnimating = false;
            //单次事件可能包含好几格（快速拨轮、触摸板），限幅免得一格冲到模型里面
            float32 scale = std::clamp(1.0f - mouseWheel * ZoomStep, 0.5f, 2.0f);
            float32 targetDistance = std::max(cameraFocusDistance * scale, MinimumFocusDistance);
            float32 advance = cameraFocusDistance - targetDistance;
            cameraFocusDistance = targetDistance;
            transform->SetLocalPosition(Add(transform->GetLocalPosition(),
                Scale(GetForward(cameraYaw, cameraPitch), advance)));
        }
    }
    else
    {
        cameraMouseDragging = false;
        cameraMouseMode = 0;
    }

    transform->SetLocalRotation(GetYawPitchRotation(cameraYaw, cameraPitch));
    cameraState.hasValue = true;
    cameraState.position = transform->GetLocalPosition();
    cameraState.yaw = cameraYaw;
    cameraState.pitch = cameraPitch;
    //聚焦距离随布局一起存盘：它决定平移与缩放的速率，重开项目时不能退回默认值
    cameraState.focusDistance = cameraFocusDistance;
}

//把编辑器观察相机对准指定 Ens：只挪位置，保持当前朝向。
void EditorScene::FocusEns(World& world, EnsId ens)
{
    //Play 中编辑器相机已被摘除，临时对象也不该成为聚焦目标
    Ens* target = world.GetEns(ens);
    if (!target || !target->Transform() || IsTemporaryEns(ens)) return;

    Ens* editorCamera = world.GetEns(cameraEns);
    if (!editorCamera) editorCamera = world.FindEns(StringId(EditorCameraId));
    Transform* cameraTransform = editorCamera ? editorCamera->Transform() : nullptr;
    if (!cameraTransform) return;

    //聚焦点取子树包围盒中心，子树内没有网格时退化为对象自身位置
    vector3 focus = target->Transform()->GetWorldPosition();
    float32 radius = 0.0f;
    bounds3 bounds {};
    if (MergeSubtreeBounds(world, ens, bounds))
    {
        focus = bounds.center;
        radius = std::sqrt(bounds.extents.x * bounds.extents.x
            + bounds.extents.y * bounds.extents.y + bounds.extents.z * bounds.extents.z);
    }

    //包围球要在竖直与水平两个方向都装得下：竖屏时横向视场更窄，取两者中较小的那个
    float32 halfFov = GetHalfVerticalFov(world, editorCamera->GetId());
    if (sceneView.renderSize.x > 0.0f && sceneView.renderSize.y > 0.0f)
    {
        float32 halfFovX = std::atan(std::tan(halfFov) * sceneView.renderSize.x / sceneView.renderSize.y);
        halfFov = std::min(halfFov, halfFovX);
    }
    halfFov = std::max(halfFov, 0.0001f);

    //空对象聚焦后不该贴到镜头上，留一个最小距离
    constexpr float32 MinimumFramingDistance = 1.0f;
    constexpr float32 BoundsMargin = 1.35f;
    float32 distance = std::max(radius * BoundsMargin / std::tan(halfFov), MinimumFramingDistance);

    //这里只排一次动画，真正的写入在 Update 里（它带着自己的脏标记抑制），所以不必再开一层作用域
    vector3 forward = GetForward(cameraYaw, cameraPitch);
    StartFocusAnimation(cameraTransform->GetLocalPosition(),
        { focus.x - forward.x * distance, focus.y - forward.y * distance, focus.z - forward.z * distance },
        distance);
}

//启动一次聚焦动画：位置与聚焦距离共用一条缓动曲线。
void EditorScene::StartFocusAnimation(const vector3& fromPosition, const vector3& toPosition, float32 toDistance)
{
    focusAnimationFrom = fromPosition;
    focusAnimationTo = toPosition;
    focusDistanceFrom = cameraFocusDistance;
    focusDistanceTo = toDistance;

    //起止几乎没差别就不值得动半秒，直接落位
    float32 deltaX = toPosition.x - fromPosition.x;
    float32 deltaY = toPosition.y - fromPosition.y;
    float32 deltaZ = toPosition.z - fromPosition.z;
    if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < 0.000001f
        && std::abs(toDistance - cameraFocusDistance) < 0.000001f)
    {
        focusAnimating = false;
        return;
    }

    focusAnimationElapsed = 0.0f;
    focusAnimating = true;
}

//推进一次聚焦动画；返回本帧是否写出了位置。
bool EditorScene::AdvanceFocusAnimation(float32 deltaTime, vector3& position)
{
    if (!focusAnimating) return false;

    focusAnimationElapsed += deltaTime;
    float32 progress = std::min(focusAnimationElapsed / FocusAnimationSeconds, 1.0f);
    float32 eased = EaseOutCubic(progress);
    position = Lerp(focusAnimationFrom, focusAnimationTo, eased);
    cameraFocusDistance = std::max(
        focusDistanceFrom + (focusDistanceTo - focusDistanceFrom) * eased, MinimumFocusDistance);
    focusAnimating = progress < 1.0f;
    return true;
}

//聚焦动画是否还在进行。
bool EditorScene::IsAnimatingFocus() const
{
    return focusAnimating;
}

//判断鼠标是否位于场景视口矩形内。
bool EditorScene::IsMouseOverSceneView() const
{
    GLFWwindow* window = GetGlfwWindow(app);
    if (!window || !sceneView.visible
        || sceneView.interactSize.x <= 0.0f || sceneView.interactSize.y <= 0.0f) return false;

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    return mouseX >= sceneView.interactPosition.x
        && mouseX <= sceneView.interactPosition.x + sceneView.interactSize.x
        && mouseY >= sceneView.interactPosition.y
        && mouseY <= sceneView.interactPosition.y + sceneView.interactSize.y;
}

//取消当前鼠标交互。
void EditorScene::CancelInteraction()
{
    cameraMouseDragging = false;
    cameraMouseMode = 0;
    selectionPressed = false;
    selectionDragged = false;
    gizmoHandles.CancelDrag(app.GetWorld());
}

//清空选择和场景绘制缓存。
void EditorScene::ClearSceneState()
{
    ClearSelection();
    CancelInteraction();
}

//移除已经失效的选择对象。
void EditorScene::PruneSelection(const World& world)
{
    selectedEns.erase(std::remove_if(selectedEns.begin(), selectedEns.end(), [&world](EnsId ens)
    {
        return !world.IsAlive(ens);
    }), selectedEns.end());

    if (std::find(selectedEns.begin(), selectedEns.end(), activeEns) == selectedEns.end())
    {
        activeEns = selectedEns.empty() ? EnsId() : selectedEns.back();
    }
}

//选择一个 Ens。
//移动层级节点并保持指定的变换空间
bool EditorScene::MoveEns(World& world, EnsId child, EnsId parent, EnsId before, bool preserveWorld)
{
    if (IsTemporaryEns(child) || (!parent.IsNull() && IsTemporaryEns(parent))
        || (!before.IsNull() && IsTemporaryEns(before))) return false;
    Transform* transform = world.GetTransform(child);
    if (!transform) return false;
    EnsId oldParent = transform->parent;
    matrix4x4 worldMatrix = transform->worldMatrix;
    if (!world.MoveEns(child, parent, before)) return false;
    if (!preserveWorld || oldParent == parent) return true;
    Transform* parentTransform = world.GetTransform(parent);
    matrix4x4 localMatrix = parentTransform
        ? RenderMath::Mul(RenderMath::Inverse(parentTransform->worldMatrix), worldMatrix) : worldMatrix;
    vector3 position, scale;
    quaternion rotation;
    DecomposeTransform(localMatrix, position, rotation, scale);
    transform->SetLocalPosition(position);
    transform->SetLocalRotation(rotation);
    transform->SetLocalScale(scale);
    return true;
}

void EditorScene::SelectEns(EnsId ens)
{
    selectedEns.clear();
    activeEns = ens;
    if (!ens.IsNull()) selectedEns.push_back(ens);
}

//切换一个 Ens 的选择状态。
void EditorScene::ToggleEns(EnsId ens)
{
    if (ens.IsNull()) return;

    auto it = std::find(selectedEns.begin(), selectedEns.end(), ens);
    if (it == selectedEns.end())
    {
        selectedEns.push_back(ens);
        activeEns = ens;
        return;
    }

    bool removedActive = activeEns == ens;
    selectedEns.erase(it);
    if (removedActive) activeEns = selectedEns.empty() ? EnsId() : selectedEns.back();
}

//清空当前选择。
void EditorScene::ClearSelection()
{
    selectedEns.clear();
    activeEns = EnsId();
}

//获取当前活动选择。
EnsId EditorScene::GetSelectedEns() const
{
    return activeEns;
}

//获取完整选择列表。
const List<EnsId>& EditorScene::GetSelectedEnsList() const
{
    return selectedEns;
}

//判断指定 Ens 是否被选中。
bool EditorScene::IsSelected(EnsId ens) const
{
    return std::find(selectedEns.begin(), selectedEns.end(), ens) != selectedEns.end();
}

//获取当前活动选择的稳定 ID。
std::string EditorScene::GetSelectedStableId() const
{
    return GetStableId(activeEns);
}

//获取指定 Ens 的稳定 ID。
std::string EditorScene::GetStableId(EnsId ens) const
{
    if (ens.IsNull()) return std::string();
    const Ens* value = app.GetWorld().GetEns(ens);
    return value ? value->GetInstanceId().GetPath() : std::string();
}

//判断 Ens 是否属于编辑器临时场景对象。
bool EditorScene::IsTemporaryEns(EnsId ens) const
{
    const Ens* value = app.GetWorld().GetEns(ens);
    return value && value->GetInstanceId().GetPath() == EditorCameraId;
}

//把观察相机状态写入布局。
void EditorScene::WriteLayout(EditorLayoutState& layout)
{
    CaptureCameraState(app.GetWorld());
    layout.editorCamera = cameraState;
}

//应用布局中的观察相机状态。
void EditorScene::ApplyLayout(const EditorLayoutState& layout, World& world)
{
    //布局恢复的是编辑器相机，不属于场景内容
    World::DirtySuppressionScope suppression(world);
    cameraState = layout.editorCamera;
    cameraYaw = cameraState.yaw;
    cameraPitch = cameraState.pitch;
    cameraEns = EnsId();
    //换项目时相机位置由布局说了算，飞在半路的聚焦动画必须让位，否则下一帧把相机拽回旧目标
    focusAnimating = false;
    CreateEditorCamera(world);

    if (!cameraState.hasValue) return;
    if (Transform* transform = world.GetTransform(cameraEns))
    {
        transform->SetLocalPosition(cameraState.position);
        transform->SetLocalRotation(GetYawPitchRotation(cameraYaw, cameraPitch));
        transform->SetLocalScale({ 1.0f, 1.0f, 1.0f });
    }
}

//序列化场景前暂时移除编辑器相机。
bool EditorScene::RemoveCameraForSerialization(World& world)
{
    bool hadCamera = world.GetEns(cameraEns) || world.FindEns(StringId(EditorCameraId));
    CaptureCameraState(world);
    RemoveCamera(world);
    return hadCamera;
}

//恢复编辑器观察相机。
void EditorScene::RestoreCamera(World& world)
{
    //恢复编辑器相机同样不改变场景内容
    World::DirtySuppressionScope suppression(world);
    focusAnimating = false;
    CreateEditorCamera(world);
    if (!cameraState.hasValue) return;

    if (Transform* transform = world.GetTransform(cameraEns))
    {
        transform->SetLocalPosition(cameraState.position);
        transform->SetLocalRotation(GetYawPitchRotation(cameraYaw, cameraPitch));
        transform->SetLocalScale({ 1.0f, 1.0f, 1.0f });
    }
}

//进入 Play 前移除临时相机并清理无效选择。
void EditorScene::EnterPlayMode(World& world)
{
    gizmoHandles.CancelDrag(world);
    playModeActive = true;
    bool clearSelection = activeEns.IsNull() || !world.IsAlive(activeEns) || IsTemporaryEns(activeEns);
    RemoveCameraForSerialization(world);
    if (clearSelection) ClearSelection();
}

//退出 Play 后重置选择并恢复观察相机。
void EditorScene::ExitPlayMode(World& world)
{
    playModeActive = false;
    ClearSceneState();
    RestoreCamera(world);
}

//获取托管 Handles 使用的原生函数表。
EditorGizmoApi EditorScene::GetGizmoApi()
{
    EditorGizmoApi api;
    api.Line3D = reinterpret_cast<void*>(&DrawGizmoLine);
    api.Label3D = reinterpret_cast<void*>(&DrawGizmoLabel);
    api.IsSelected = reinterpret_cast<void*>(&IsGizmoEnsSelected);
    api.IsVisible = reinterpret_cast<void*>(&AreGizmosVisible);
    api.TakeEdit = reinterpret_cast<void*>(&TakeGizmoEditNative);
    return api;
}

//获取当前 Handles 视图投影矩阵。
const matrix4x4& EditorScene::GetGizmoViewProjection() const
{
    return gizmoViewProjection;
}

//创建或修复编辑器观察相机。
void EditorScene::CreateEditorCamera(World& world)
{
    //编辑器相机不进场景文件，它的创建与写入都不算场景改动
    World::DirtySuppressionScope suppression(world);
    Ens* editorCamera = world.GetEns(cameraEns);
    if (!editorCamera) editorCamera = world.FindEns(StringId(EditorCameraId));
    if (!editorCamera)
    {
        editorCamera = world.CreateEnsWithStableId(EditorCameraId, "EditorCamera");
        if (editorCamera)
        {
            if (Transform* transform = editorCamera->Transform())
            {
                transform->SetLocalPosition(cameraState.hasValue
                    ? cameraState.position
                    : vector3 { 5.0f, 3.2f, 7.0f });
                transform->SetLocalScale({ 1.0f, 1.0f, 1.0f });
            }
        }
    }
    if (!editorCamera) return;

    Camera* camera = editorCamera->GetComponent<Camera>();
    if (!camera) camera = editorCamera->AddComponent<Camera>();
    if (camera)
    {
        camera->SetEnabled(true);
        camera->fieldOfView = 60.0f;
        camera->nearPlane = 0.1f;
        camera->farPlane = 1000.0f;
        camera->depth = 10000.0f;
        camera->clearMode = ClearMode::SolidColor;
        camera->clearColor = { 0.62f, 0.78f, 0.96f, 1.0f };
    }

    if (cameraState.hasValue)
    {
        cameraYaw = cameraState.yaw;
        cameraPitch = cameraState.pitch;
        //聚焦距离要跟着恢复：平移与缩放的速率都按它算，给错值手感会明显失真
        cameraFocusDistance = std::max(cameraState.focusDistance, MinimumFocusDistance);
    }
    if (Transform* transform = editorCamera->Transform())
    {
        transform->SetLocalRotation(GetYawPitchRotation(cameraYaw, cameraPitch));
    }
    cameraEns = editorCamera->GetId();
}

//记录当前编辑器观察相机状态。
void EditorScene::CaptureCameraState(World& world)
{
    Ens* editorCamera = world.GetEns(cameraEns);
    if (!editorCamera) editorCamera = world.FindEns(StringId(EditorCameraId));
    if (!editorCamera)
    {
        if (!cameraState.hasValue)
        {
            cameraState.hasValue = true;
            cameraState.yaw = cameraYaw;
            cameraState.pitch = cameraPitch;
        }
        return;
    }

    Transform* transform = editorCamera->Transform();
    if (!transform) return;
    cameraState.hasValue = true;
    cameraState.position = transform->GetLocalPosition();
    cameraState.yaw = cameraYaw;
    cameraState.pitch = cameraPitch;
    cameraState.focusDistance = cameraFocusDistance;
}

//移除当前编辑器观察相机。
void EditorScene::RemoveCamera(World& world)
{
    //编辑器相机的销毁同样不算场景改动
    World::DirtySuppressionScope suppression(world);
    Ens* editorCamera = world.GetEns(cameraEns);
    if (!editorCamera) editorCamera = world.FindEns(StringId(EditorCameraId));
    if (editorCamera) editorCamera->Destroy();

    cameraEns = EnsId();
    //相机没了，动画不能一直挂着——否则会一直请求重绘空转
    focusAnimating = false;
    CancelInteraction();
}

//处理中央工作区鼠标选择。
void EditorScene::HandleSelection(const RenderScene& scene)
{
    ImGuiIO& io = ImGui::GetIO();

    //只在场景视口内捕获一次普通左键点击。
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        selectionPressed = IsMouseOverSceneView()
            && !io.KeyAlt
            && !cameraMouseDragging
            && !HasBlockingImGuiActiveItem()
            && !gizmoToolbarHovered
            && !gizmoHandles.OwnsMouse();
        selectionDragged = false;
        selectionCtrl = io.KeyCtrl;
        selectionStart = { io.MousePos.x, io.MousePos.y };
    }
    if (!selectionPressed) return;

    //超过点击阈值或切入相机操作后取消选择。
    float32 deltaX = io.MousePos.x - selectionStart.x;
    float32 deltaY = io.MousePos.y - selectionStart.y;
    if (deltaX * deltaX + deltaY * deltaY > 16.0f
        || io.KeyAlt
        || cameraMouseDragging
        || HasBlockingImGuiActiveItem())
    {
        selectionDragged = true;
    }

    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) selectionPressed = false;
        return;
    }

    bool shouldPick = !selectionDragged
        && IsMouseOverSceneView()
        && !HasBlockingImGuiActiveItem();
    selectionPressed = false;
    selectionDragged = false;
    if (!shouldPick) return;

    EnsId hit;
    vector3 hitPosition;
    RaycastScene(scene, { io.MousePos.x, io.MousePos.y }, hit, hitPosition);
    if (selectionCtrl)
    {
        if (!hit.IsNull()) ToggleEns(hit);
    }
    else if (!hit.IsNull()) SelectEns(hit);
    else ClearSelection();
}

//投射鼠标射线并返回最近命中的对象与命中点。
bool EditorScene::RaycastScene(const RenderScene& scene, const vector2& screenPosition,
    EnsId& hitEns, vector3& hitPosition) const
{
    hitEns = EnsId();
    hitPosition = { 0.0f, 0.0f, 0.0f };

    const RenderCamera* camera = nullptr;
    for (const RenderCamera& candidate : scene.cameras)
    {
        if (candidate.ens == cameraEns)
        {
            camera = &candidate;
            break;
        }
    }
    if (!camera || !camera->renderTargetId.IsValid() || camera->viewportWidth <= 0 || camera->viewportHeight <= 0) return false;
    if (sceneView.renderSize.x <= 0.0f || sceneView.renderSize.y <= 0.0f) return false;

    //把 ImGui 逻辑坐标按场景渲染矩形转换为相机 NDC。
    if (screenPosition.x < sceneView.renderPosition.x
        || screenPosition.x > sceneView.renderPosition.x + sceneView.renderSize.x
        || screenPosition.y < sceneView.renderPosition.y
        || screenPosition.y > sceneView.renderPosition.y + sceneView.renderSize.y)
    {
        return false;
    }

    //射线与手柄共用同一套投影约定，保证命中区和手柄绘制一致
    vector3 rayOrigin;
    vector3 rayDirection;
    float32 rayLength = 0.0f;
    if (!EditorGizmoHandles::BuildScreenRay(camera->viewProjectionMatrix, sceneView.renderPosition,
        sceneView.renderSize, screenPosition, rayOrigin, rayDirection, rayLength))
        return false;
    EnsId closestEns;
    float32 closestDistance = rayLength;
    for (StaticMeshRenderer* renderer : scene.renderers)
    {
        Mesh* mesh = renderer ? renderer->mesh.Get() : nullptr;
        if (!renderer || !renderer->IsRenderSceneEligible() || !mesh) continue;
        if ((renderer->drawLayer & camera->drawLayerMask) == 0) continue;

        EnsId rendererEns = renderer->GetEnsId();
        Ens* currentEns = app.GetWorld().GetEns(rendererEns);
        StaticMeshRenderer* currentRenderer = currentEns ? currentEns->GetComponent<StaticMeshRenderer>() : nullptr;
        Transform* transform = app.GetWorld().GetTransform(rendererEns);
        if (currentRenderer != renderer || !transform) continue;

        bounds3 worldBounds = RenderMath::TransformBounds(transform->worldMatrix, mesh->GetLocalBounds());
        if (!worldBounds.valid) continue;

        //先用世界包围盒排除不可能命中的绘制项。
        float32 boundsMin[3] =
        {
            worldBounds.center.x - worldBounds.extents.x,
            worldBounds.center.y - worldBounds.extents.y,
            worldBounds.center.z - worldBounds.extents.z,
        };
        float32 boundsMax[3] =
        {
            worldBounds.center.x + worldBounds.extents.x,
            worldBounds.center.y + worldBounds.extents.y,
            worldBounds.center.z + worldBounds.extents.z,
        };
        float32 origin[3] = { rayOrigin.x, rayOrigin.y, rayOrigin.z };
        float32 direction[3] = { rayDirection.x, rayDirection.y, rayDirection.z };
        float32 entryDistance = 0.0f;
        float32 exitDistance = closestDistance;
        bool intersectsBounds = true;
        for (int32 axis = 0; axis < 3; ++axis)
        {
            if (std::abs(direction[axis]) <= 0.000001f)
            {
                if (origin[axis] < boundsMin[axis] || origin[axis] > boundsMax[axis]) intersectsBounds = false;
                continue;
            }

            float32 first = (boundsMin[axis] - origin[axis]) / direction[axis];
            float32 second = (boundsMax[axis] - origin[axis]) / direction[axis];
            if (first > second) std::swap(first, second);
            entryDistance = std::max(entryDistance, first);
            exitDistance = std::min(exitDistance, second);
            if (entryDistance > exitDistance)
            {
                intersectsBounds = false;
                break;
            }
        }
        if (!intersectsBounds) continue;

        //在世界空间进行双面三角形精确检测。
        const List<vector3>& vertices = mesh->vertices;
        const List<uint32>& indices = mesh->indices;
        for (usize subMeshIndex = 0; subMeshIndex < mesh->subMeshes.size(); ++subMeshIndex)
        {
            const SubMesh& subMesh = mesh->subMeshes[subMeshIndex];
            usize indexStart = static_cast<usize>(subMesh.indexStart);
            usize indexCount = static_cast<usize>(subMesh.indexCount);
            //与绘制一致：没有材质的子网格不参与拾取
            if (subMeshIndex >= currentRenderer->materials.size() || !currentRenderer->materials[subMeshIndex].Get()
                || indexCount == 0
                || indexStart > indices.size() || indexCount > indices.size() - indexStart) continue;

            usize indexEnd = indexStart + indexCount - indexCount % 3;
            for (usize index = indexStart; index + 2 < indexEnd; index += 3)
            {
                uint32 indexA = indices[index];
                uint32 indexB = indices[index + 1];
                uint32 indexC = indices[index + 2];
                if (indexA >= vertices.size() || indexB >= vertices.size() || indexC >= vertices.size()) continue;

                vector3 a = RenderMath::TransformPoint(transform->worldMatrix, vertices[indexA]);
                vector3 b = RenderMath::TransformPoint(transform->worldMatrix, vertices[indexB]);
                vector3 c = RenderMath::TransformPoint(transform->worldMatrix, vertices[indexC]);
                vector3 edgeAB = { b.x - a.x, b.y - a.y, b.z - a.z };
                vector3 edgeAC = { c.x - a.x, c.y - a.y, c.z - a.z };
                vector3 crossDirection = RenderMath::Cross(rayDirection, edgeAC);
                float32 determinant = RenderMath::Dot(edgeAB, crossDirection);
                if (std::abs(determinant) <= 0.000001f) continue;

                float32 inverseDeterminant = 1.0f / determinant;
                vector3 toOrigin = { rayOrigin.x - a.x, rayOrigin.y - a.y, rayOrigin.z - a.z };
                float32 u = RenderMath::Dot(toOrigin, crossDirection) * inverseDeterminant;
                if (u < 0.0f || u > 1.0f) continue;

                vector3 crossOrigin = RenderMath::Cross(toOrigin, edgeAB);
                float32 v = RenderMath::Dot(rayDirection, crossOrigin) * inverseDeterminant;
                if (v < 0.0f || u + v > 1.0f) continue;

                float32 distance = RenderMath::Dot(edgeAC, crossOrigin) * inverseDeterminant;
                if (distance >= 0.0f && distance < closestDistance)
                {
                    closestDistance = distance;
                    closestEns = rendererEns;
                }
            }
        }
    }

    if (closestEns.IsNull()) return false;
    hitEns = closestEns;
    hitPosition = Add(rayOrigin, Scale(rayDirection, closestDistance));
    return true;
}

//解析场景投放点：优先表面命中，其次地面交点，最后相机前方。
bool EditorScene::ResolveSceneDropPosition(vector3& position) const
{
    World& world = app.GetWorld();
    Transform* transform = world.GetTransform(cameraEns);
    if (!transform) return false;

    //优先使用鼠标命中的表面位置
    if (RenderSystem* renderSystem = app.GetSystem<RenderSystem>())
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        EnsId hitEns;
        vector3 hitPosition;
        if (RaycastScene(renderSystem->GetCurrentScene(), { mouse.x, mouse.y }, hitEns, hitPosition))
        {
            position = hitPosition;
            return true;
        }
    }

    vector3 origin = transform->GetLocalPosition();
    vector3 forward = GetForward(cameraYaw, cameraPitch);

    //其次使用视线与世界地面 Y=0 的交点
    constexpr float32 GroundPlaneY = 0.0f;
    if (std::abs(forward.y) > 0.000001f)
    {
        float32 distance = (GroundPlaneY - origin.y) / forward.y;
        if (distance > 0.0f)
        {
            position = Add(origin, Scale(forward, distance));
            return true;
        }
    }

    //最后放在相机前方十个世界单位处
    position = Add(origin, Scale(forward, 10.0f));
    return true;
}

//把当前选择及其后代提交给渲染系统，由屏幕空间后处理绘制描边。
void EditorScene::SubmitSelectionHighlight(World& world)
{
    RenderSystem* renderSystem = app.GetSystem<RenderSystem>();
    if (!renderSystem) return;

    //显式选择优先，后代只补没有被显式选中的节点
    std::unordered_map<uint64, RenderSystem::SelectionHighlight> highlights;
    for (EnsId ens : selectedEns)
    {
        if (!ens.IsNull() && world.IsAlive(ens))
        {
            highlights[GetEnsKey(ens)] = RenderSystem::SelectionHighlight { ens, GetSelectionTint(ExplicitSelection) };
        }
    }

    List<EnsId> pendingEns;
    for (EnsId ens : selectedEns)
    {
        Transform* selectedTransform = world.GetTransform(ens);
        if (!selectedTransform) continue;

        EnsId child = selectedTransform->firstChild;
        while (!child.IsNull())
        {
            pendingEns.push_back(child);
            Transform* childTransform = world.GetTransform(child);
            child = childTransform ? childTransform->next : EnsId();
        }
    }
    while (!pendingEns.empty())
    {
        EnsId ens = pendingEns.back();
        pendingEns.pop_back();
        if (ens.IsNull() || !world.IsAlive(ens)) continue;
        if (highlights.find(GetEnsKey(ens)) != highlights.end()) continue;

        highlights.emplace(GetEnsKey(ens), RenderSystem::SelectionHighlight { ens, GetSelectionTint(DescendantSelection) });
        Transform* transform = world.GetTransform(ens);
        if (!transform) continue;

        EnsId child = transform->firstChild;
        while (!child.IsNull())
        {
            pendingEns.push_back(child);
            Transform* childTransform = world.GetTransform(child);
            child = childTransform ? childTransform->next : EnsId();
        }
    }

    List<RenderSystem::SelectionHighlight> items;
    items.reserve(highlights.size());
    for (const auto& entry : highlights) items.push_back(entry.second);
    renderSystem->SetSelectionHighlights(items);
}

//绘制托管 Scene Handles。
void EditorScene::DrawManagedGizmos()
{
    if (!gizmoViewValid) return;

    ImGuiWindow* window = ImGui::GetCurrentContext()->CurrentWindow;
    ImVec2 savedCursor = ImGui::GetCursorScreenPos();
    ImVec2 savedMax = window->DC.CursorMaxPos;
    ImGui::PushClipRect(ImVec2(sceneView.renderPosition.x, sceneView.renderPosition.y),
        ImVec2(sceneView.renderPosition.x + sceneView.renderSize.x, sceneView.renderPosition.y + sceneView.renderSize.y), true);
    ImGui::SetCursorScreenPos(ImVec2(sceneView.renderPosition.x + 8, sceneView.renderPosition.y + 40));
    ImGui::BeginGroup();
    CurrentGizmoScene = this;
    managedBridge.DrawSceneGizmos();
    if (CurrentGizmoScene == this) CurrentGizmoScene = nullptr;
    ImGui::EndGroup();
    gizmoToolbarHovered |= ImGui::IsItemHovered();
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(savedCursor);
    window->DC.CursorMaxPos = savedMax;
    window->DC.IsSetPos = false;
}

//准备本帧手柄与托管 Gizmo 共用的视图投影。
void EditorScene::PrepareGizmoView(const RenderScene& scene)
{
    gizmoViewValid = false;
    gizmoView = EditorGizmoView();
    if (sceneView.renderSize.x <= 0.0f || sceneView.renderSize.y <= 0.0f) return;

    //渲染这张离屏图用的就是这份矩阵，手柄位置与命中才能和画面像素严格对齐
    const RenderCamera* camera = nullptr;
    for (const RenderCamera& candidate : scene.cameras)
    {
        if (candidate.ens == cameraEns)
        {
            camera = &candidate;
            break;
        }
    }
    //与 RaycastScene 用同一组门槛，保证画得出手柄就一定拾取得到
    if (!camera || !camera->renderTargetId.IsValid()
        || camera->viewportWidth <= 0 || camera->viewportHeight <= 0) return;

    gizmoViewProjection = camera->viewProjectionMatrix;
    gizmoCameraRight = RenderMath::Normalize(
        RenderMath::TransformDirection(camera->worldMatrix, { 1.0f, 0.0f, 0.0f }));
    gizmoCameraUp = RenderMath::Normalize(
        RenderMath::TransformDirection(camera->worldMatrix, { 0.0f, 1.0f, 0.0f }));
    gizmoCameraForward = RenderMath::Normalize(
        RenderMath::TransformDirection(camera->worldMatrix, { 0.0f, 0.0f, -1.0f }));

    gizmoView.viewProjection = gizmoViewProjection;
    gizmoView.cameraRight = gizmoCameraRight;
    gizmoView.cameraUp = gizmoCameraUp;
    gizmoView.cameraForward = gizmoCameraForward;
    gizmoView.renderPosition = sceneView.renderPosition;
    gizmoView.renderSize = sceneView.renderSize;
    gizmoView.valid = true;
    gizmoViewValid = true;
}

//绘制场景视图左上角的模式工具栏。
void EditorScene::DrawGizmoToolbar()
{
    gizmoToolbarHovered = false;
    if (!gizmoViewValid) return;
    if (sceneView.renderSize.x < 200.0f || sceneView.renderSize.y < 90.0f) return;

    //工具栏是压在视口上的叠加层，不参与本窗口的内容尺寸：
    //借用的光标位、被撑大的内容边界与 SetCursorScreenPos 标记都要还原，
    //否则 End() 会判定"用 SetCursorPos 撑大窗口边界"而断言。
    ImGuiContext* context = ImGui::GetCurrentContext();
    ImGuiWindow* window = context ? context->CurrentWindow : nullptr;
    if (!window) return;

    const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
    const ImVec2 savedMax = window->DC.CursorMaxPos;
    ImGui::SetCursorScreenPos(ImVec2(sceneView.renderPosition.x + 8.0f, sceneView.renderPosition.y + 8.0f));

    //鼠标落在按钮上时手柄要让位，否则点不动按钮
    bool overToolbar = false;
    const EditorGizmoMode modes[3] = { EditorGizmoMode::Move, EditorGizmoMode::Rotate, EditorGizmoMode::Scale };
    const char* ids[3] = { "##handle_mode_move", "##handle_mode_rotate", "##handle_mode_scale" };
    for (int32 index = 0; index < 3; ++index)
    {
        if (index > 0) ImGui::SameLine(0.0f, 4.0f);

        bool current = gizmoHandles.GetMode() == modes[index];
        if (current) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool clicked = ImGui::Button(ids[index], ImVec2(26.0f, 22.0f));
        if (current) ImGui::PopStyleColor();

        overToolbar |= ImGui::IsItemHovered();
        DrawHandleModeIcon(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            modes[index], ImGui::GetColorU32(ImGuiCol_Text));
        if (clicked) gizmoHandles.SetMode(modes[index]);
    }

    //坐标空间切换：显示的是当前空间，点击切到另一个
    ImGui::SameLine(0.0f, 8.0f);
    bool localMode = gizmoHandles.GetOrientation() == EditorGizmoOrientation::Local;
    if (localMode) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(localMode ? "Local##handle_space" : "Global##handle_space", ImVec2(56.0f, 22.0f)))
    {
        gizmoHandles.SetOrientation(localMode ? EditorGizmoOrientation::Global : EditorGizmoOrientation::Local);
    }
    if (localMode) ImGui::PopStyleColor();

    overToolbar |= ImGui::IsItemHovered();
    if (sceneView.renderSize.x >= 300.0f)
    {
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button(componentGizmos.enabled ? "Gizmos##component_gizmos" : "Gizmos Off##component_gizmos", ImVec2(0,22)))
            ImGui::OpenPopup("component_gizmos_options");
        overToolbar |= ImGui::IsItemHovered();
        if (ImGui::BeginPopup("component_gizmos_options"))
        {
            ImGui::Checkbox("Show Gizmos", &componentGizmos.enabled);
            ImGui::Separator();
            ImGui::Checkbox("Directional Lights", &componentGizmos.lights);
            ImGui::Checkbox("Colliders", &componentGizmos.colliders);
            ImGui::Checkbox("All Colliders", &componentGizmos.allColliders);
            ImGui::EndPopup();
            overToolbar = true;
        }
    }
    ImGui::SetCursorScreenPos(savedCursor);
    window->DC.CursorMaxPos = savedMax;
    window->DC.IsSetPos = false;
    gizmoToolbarHovered = overToolbar;
}

//处理手柄命中与拖拽。
void EditorScene::UpdateGizmoHandles(World& world)
{
    if (!gizmoViewValid)
    {
        gizmoHandles.CancelDrag(world);
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    bool overSceneView = IsMouseOverSceneView();
    bool interactive = !playModeActive
        && overSceneView
        && !gizmoToolbarHovered
        && !io.KeyAlt
        && !cameraMouseDragging
        && !HasBlockingImGuiActiveItem();

    //模式与坐标系快捷键由 EditorSystem 的快捷键表统一分发
    gizmoHandles.Update(world, gizmoView, interactive);
}

//绘制手柄。
void EditorScene::DrawGizmoHandles()
{
    if (!gizmoViewValid || playModeActive) return;
    gizmoHandles.Draw(app.GetWorld(), gizmoView);
}

//取出一条待提交的手柄编辑。
bool EditorScene::TakeGizmoEdit(EditorGizmoEdit& edit)
{
    return gizmoHandles.TakeEdit(edit);
}

//判断手柄是否正在拖拽。
bool EditorScene::IsGizmoDragging() const
{
    return gizmoHandles.IsDragging();
}

//获取手柄编辑模式。
EditorGizmoMode EditorScene::GetGizmoMode() const { return gizmoHandles.GetMode(); }

//设置手柄编辑模式。
void EditorScene::SetGizmoMode(EditorGizmoMode value) { gizmoHandles.SetMode(value); }

//获取手柄坐标系。
EditorGizmoOrientation EditorScene::GetGizmoOrientation() const { return gizmoHandles.GetOrientation(); }

//设置手柄坐标系。
void EditorScene::SetGizmoOrientation(EditorGizmoOrientation value) { gizmoHandles.SetOrientation(value); }
