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

    //按选择类型绘制三层发光线。
    void DrawGlowLine(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, uint8 selectionType)
    {
        if (!drawList) return;

        bool explicitSelection = selectionType == ExplicitSelection;
        int32 red = explicitSelection ? 255 : 58;
        int32 green = explicitSelection ? 134 : 145;
        int32 blue = explicitSelection ? 24 : 255;
        drawList->AddLine(a, b, IM_COL32(red, green, blue, 42), 7.0f);
        drawList->AddLine(a, b, IM_COL32(red, green, blue, 112), 4.0f);
        drawList->AddLine(a, b, IM_COL32(red, green, blue, 255), 2.0f);
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

    //把三维点投影到屏幕坐标。
    bool ProjectGizmoPoint(const EditorGizmoVector3& point, ImVec2& screen)
    {
        if (!CurrentGizmoScene) return false;

        const matrix4x4& viewProjection = CurrentGizmoScene->GetGizmoViewProjection();
        const EditorSceneViewState& view = CurrentGizmoScene->GetSceneViewState();
        if (view.renderSize.x <= 0.0f || view.renderSize.y <= 0.0f) return false;

        float32 x = viewProjection.m[0] * point.x + viewProjection.m[4] * point.y + viewProjection.m[8] * point.z + viewProjection.m[12];
        float32 y = viewProjection.m[1] * point.x + viewProjection.m[5] * point.y + viewProjection.m[9] * point.z + viewProjection.m[13];
        float32 z = viewProjection.m[2] * point.x + viewProjection.m[6] * point.y + viewProjection.m[10] * point.z + viewProjection.m[14];
        float32 w = viewProjection.m[3] * point.x + viewProjection.m[7] * point.y + viewProjection.m[11] * point.z + viewProjection.m[15];
        if (std::abs(w) <= 0.000001f || w < 0.0f) return false;

        float32 inverseW = 1.0f / w;
        float32 ndcX = x * inverseW;
        float32 ndcY = y * inverseW;
        float32 ndcZ = z * inverseW;
        if (ndcZ < -1.0f || ndcZ > 1.0f) return false;

        screen.x = view.renderPosition.x + (ndcX * 0.5f + 0.5f) * view.renderSize.x;
        screen.y = view.renderPosition.y + (1.0f - (ndcY * 0.5f + 0.5f)) * view.renderSize.y;
        return true;
    }

    //绘制一条托管三维 Handle 线。
    void ORBEDEN_NATIVE_CALL DrawGizmoLine(EditorGizmoVector3 a, EditorGizmoVector3 b, EditorGizmoColor color)
    {
        ImVec2 screenA;
        ImVec2 screenB;
        if (!ProjectGizmoPoint(a, screenA) || !ProjectGizmoPoint(b, screenB)) return;
        ImGui::GetBackgroundDrawList()->AddLine(screenA, screenB, ToImColor(color), 2.0f);
    }

    //绘制一个托管三维 Handle 标签。
    void ORBEDEN_NATIVE_CALL DrawGizmoLabel(EditorGizmoVector3 position, const uint8* text, int32 length)
    {
        ImVec2 screen;
        if (!ProjectGizmoPoint(position, screen)) return;

        const char* begin = text && length > 0 ? reinterpret_cast<const char*>(text) : "";
        const char* end = begin + std::max(length, 0);
        ImGui::GetBackgroundDrawList()->AddText(screen, IM_COL32(255, 245, 180, 255), begin, end);
    }
}

EditorScene::EditorScene(Application& application, ManagedEditorBridge& bridge)
    : app(application)
    , managedBridge(bridge)
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
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 interactMin = ImGui::GetCursorScreenPos();
    ImVec2 interactSize = ImGui::GetContentRegionAvail();
    if (!viewport || interactSize.x < 8.0f || interactSize.y < 8.0f)
    {
        //内容区过小时暂停视口渲染
        ImGui::Dummy(ImVec2(std::max(interactSize.x, 1.0f), std::max(interactSize.y, 1.0f)));
        return;
    }

    //渲染区域始终铺满主窗口，可交互区域是本面板内容区
    sceneView.renderPosition = { viewport->Pos.x, viewport->Pos.y };
    sceneView.renderSize = { viewport->Size.x, viewport->Size.y };
    sceneView.interactPosition = { interactMin.x, interactMin.y };
    sceneView.interactSize = { interactSize.x, interactSize.y };
    sceneView.pixelWidth = std::max(static_cast<int32>(std::lround(sceneView.renderSize.x * io.DisplayFramebufferScale.x)), 1);
    sceneView.pixelHeight = std::max(static_cast<int32>(std::lround(sceneView.renderSize.y * io.DisplayFramebufferScale.y)), 1);
    sceneView.visible = true;

    //图像绘制到背景列表，保证像改造前一样始终铺在停靠面板之下
    if (sceneTargetTexture.id != 0)
    {
        //离屏纹理原点在左下角，交换 V 轴与 ImGui 的左上角原点对齐
        ImVec2 min(sceneView.renderPosition.x, sceneView.renderPosition.y);
        ImVec2 max(min.x + sceneView.renderSize.x, min.y + sceneView.renderSize.y);
        ImGui::GetBackgroundDrawList()->AddImage(static_cast<ImTextureID>(sceneTargetTexture.id),
            min, max, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }

    //透明占位项只提供悬停与投放目标，可见像素全部来自背景列表
    if (sceneTargetTexture.id != 0)
    {
        ImGui::ImageWithBg(static_cast<ImTextureID>(sceneTargetTexture.id), interactSize,
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
    }
    else
    {
        ImGui::Dummy(interactSize);
    }

    DrawSceneOverlay();
}

//在背景绘制列表上提交选择轮廓与托管 Handles。
void EditorScene::DrawSceneOverlay()
{
    World& world = app.GetWorld();
    RenderSystem* renderSystem = app.GetSystem<RenderSystem>();
    if (!renderSystem || sceneView.renderSize.x <= 0.0f || sceneView.renderSize.y <= 0.0f) return;

    const RenderScene& scene = renderSystem->GetCurrentScene();
    HandleSelection(scene);
    DrawSelectionOutline(scene, world, sceneView.renderPosition, sceneView.renderSize);
    DrawManagedGizmos();
}

//更新编辑器观察相机。
void EditorScene::Update(World& world, float32 deltaTime, float32 mouseWheel)
{
    (void)deltaTime;
    CreateEditorCamera(world);

    Transform* transform = world.GetTransform(cameraEns);
    GLFWwindow* window = GetGlfwWindow(app);
    if (!transform || !window) return;

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
            if (cameraMouseDragging && cameraMouseMode == mode)
            {
                float32 deltaX = static_cast<float32>(mouseX - previousMouseX);
                float32 deltaY = static_cast<float32>(mouseY - previousMouseY);
                if (mode == 1)
                {
                    cameraYaw -= deltaX * 0.12f;
                    cameraPitch = std::clamp(cameraPitch - deltaY * 0.12f, -82.0f, 82.0f);
                }
                else if (mode == 2)
                {
                    constexpr float32 PanScale = 0.01f;
                    vector3 pan = Add(
                        Scale(GetRight(cameraYaw), -deltaX * PanScale),
                        Scale(GetUp(cameraYaw, cameraPitch), deltaY * PanScale));
                    transform->SetLocalPosition(Add(transform->GetLocalPosition(), pan));
                }
                else
                {
                    vector3 forward = GetForward(cameraYaw, cameraPitch);
                    transform->SetLocalPosition(Add(transform->GetLocalPosition(),
                        Scale(forward, -deltaY * cameraMoveSpeed * 0.01f)));
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

        //移动场景相机
        if (mouseWheel != 0.0f)
        {
            vector3 forward = GetForward(cameraYaw, cameraPitch);
            transform->SetLocalPosition(Add(transform->GetLocalPosition(),
                Scale(forward, mouseWheel * cameraMoveSpeed * 0.5f)));
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
}

//清空选择和场景绘制缓存。
void EditorScene::ClearSceneState()
{
    ClearSelection();
    ClearTopologyCache();
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
    cameraState = layout.editorCamera;
    cameraYaw = cameraState.yaw;
    cameraPitch = cameraState.pitch;
    cameraEns = EnsId();
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
    bool clearSelection = activeEns.IsNull() || !world.IsAlive(activeEns) || IsTemporaryEns(activeEns);
    RemoveCameraForSerialization(world);
    if (clearSelection) ClearSelection();
}

//退出 Play 后重置选择并恢复观察相机。
void EditorScene::ExitPlayMode(World& world)
{
    ClearSceneState();
    RestoreCamera(world);
}

//获取托管 Handles 使用的原生函数表。
EditorGizmoApi EditorScene::GetGizmoApi()
{
    EditorGizmoApi api;
    api.Line3D = reinterpret_cast<void*>(&DrawGizmoLine);
    api.Label3D = reinterpret_cast<void*>(&DrawGizmoLabel);
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
}

//移除当前编辑器观察相机。
void EditorScene::RemoveCamera(World& world)
{
    Ens* editorCamera = world.GetEns(cameraEns);
    if (!editorCamera) editorCamera = world.FindEns(StringId(EditorCameraId));
    if (editorCamera) editorCamera->Destroy();

    cameraEns = EnsId();
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
            && !HasBlockingImGuiActiveItem();
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

    //把 ImGui 逻辑坐标按铺满主窗口的渲染矩形转换为相机 NDC。
    if (screenPosition.x < sceneView.renderPosition.x
        || screenPosition.x > sceneView.renderPosition.x + sceneView.renderSize.x
        || screenPosition.y < sceneView.renderPosition.y
        || screenPosition.y > sceneView.renderPosition.y + sceneView.renderSize.y)
    {
        return false;
    }

    float32 ndcX = ((screenPosition.x - sceneView.renderPosition.x) / sceneView.renderSize.x) * 2.0f - 1.0f;
    float32 ndcY = 1.0f - ((screenPosition.y - sceneView.renderPosition.y) / sceneView.renderSize.y) * 2.0f;
    matrix4x4 inverseViewProjection = RenderMath::Inverse(camera->viewProjectionMatrix);
    vector3 rayOrigin = RenderMath::TransformPoint(inverseViewProjection, { ndcX, ndcY, -1.0f });
    vector3 rayEnd = RenderMath::TransformPoint(inverseViewProjection, { ndcX, ndcY, 1.0f });
    vector3 rayDelta = { rayEnd.x - rayOrigin.x, rayEnd.y - rayOrigin.y, rayEnd.z - rayOrigin.z };
    float32 rayLengthSquared = RenderMath::Dot(rayDelta, rayDelta);
    if (rayLengthSquared <= 0.000001f) return false;

    float32 rayLength = std::sqrt(rayLengthSquared);
    vector3 rayDirection = { rayDelta.x / rayLength, rayDelta.y / rayLength, rayDelta.z / rayLength };
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
        for (const SubMesh& subMesh : mesh->subMeshes)
        {
            usize indexStart = static_cast<usize>(subMesh.indexStart);
            usize indexCount = static_cast<usize>(subMesh.indexCount);
            if (!subMesh.material.Get() || indexCount == 0 ||
                indexStart > indices.size() || indexCount > indices.size() - indexStart) continue;

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

//绘制当前选择及其后代的屏幕空间轮廓。
void EditorScene::DrawSelectionOutline(const RenderScene& scene, World& world,
    const vector2& viewPosition, const vector2& viewSize)
{
    if (selectedEns.empty() || viewSize.x <= 0.0f || viewSize.y <= 0.0f) return;

    //定期回收已经销毁或长期未使用的拓扑缓存。
    ++frameIndex;
    if (frameIndex % 300 == 0)
    {
        for (auto cacheIt = topologyCache.begin(); cacheIt != topologyCache.end();)
        {
            List<MeshTopology>& entries = cacheIt->second;
            entries.erase(std::remove_if(entries.begin(), entries.end(), [this](const MeshTopology& entry)
            {
                return !Object::IsObjectAlive(entry.objectId) || frameIndex - entry.lastUsedFrame > 600;
            }), entries.end());
            if (entries.empty()) cacheIt = topologyCache.erase(cacheIt);
            else ++cacheIt;
        }
    }

    const RenderCamera* camera = nullptr;
    for (const RenderCamera& candidate : scene.cameras)
    {
        if (candidate.ens == cameraEns)
        {
            camera = &candidate;
            break;
        }
    }
    if (!camera || !camera->renderTargetId.IsValid() || camera->viewportWidth <= 0 || camera->viewportHeight <= 0) return;

    //收集显式选择和层级后代。
    std::unordered_map<uint64, uint8> selectionTypes;
    List<EnsId> pendingEns;
    for (EnsId ens : selectedEns)
    {
        if (!ens.IsNull() && world.IsAlive(ens)) selectionTypes[GetEnsKey(ens)] = ExplicitSelection;
    }
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

        uint64 key = GetEnsKey(ens);
        if (selectionTypes.find(key) != selectionTypes.end()) continue;
        selectionTypes.emplace(key, DescendantSelection);
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

    struct InstanceGroup
    {
    public:
        EnsId ens;
        StaticMeshRenderer* renderer = nullptr;
        Mesh* mesh = nullptr;
        matrix4x4 localToWorld;
        uint8 selectionType = DescendantSelection;
        List<IndexRange> ranges;
    };

    //按 renderer 实例聚合同一对象的所有有效 SubMesh 范围。
    List<InstanceGroup> groups;
    for (StaticMeshRenderer* renderer : scene.renderers)
    {
        if (!renderer || !renderer->IsRenderSceneEligible()) continue;

        EnsId rendererEns = renderer->GetEnsId();
        auto selectionIt = selectionTypes.find(GetEnsKey(rendererEns));
        if (selectionIt == selectionTypes.end()) continue;

        Ens* currentEns = world.GetEns(rendererEns);
        StaticMeshRenderer* currentRenderer = currentEns ? currentEns->GetComponent<StaticMeshRenderer>() : nullptr;
        Transform* transform = world.GetTransform(rendererEns);
        Mesh* mesh = renderer->mesh.Get();
        if (currentRenderer != renderer || !transform || !mesh) continue;
        if ((renderer->drawLayer & camera->drawLayerMask) == 0) continue;

        bounds3 worldBounds = RenderMath::TransformBounds(transform->worldMatrix, mesh->GetLocalBounds());
        if (!RenderMath::Intersects(camera->viewFrustum, worldBounds)) continue;

        InstanceGroup group;
        group.ens = rendererEns;
        group.renderer = renderer;
        group.mesh = mesh;
        group.localToWorld = transform->worldMatrix;
        group.selectionType = selectionIt->second;
        for (const SubMesh& subMesh : mesh->subMeshes)
        {
            usize start = static_cast<usize>(subMesh.indexStart);
            usize count = static_cast<usize>(subMesh.indexCount);
            if (!subMesh.material.Get() || start > mesh->indices.size() || count > mesh->indices.size() - start) continue;
            count -= count % 3;
            if (count > 0) group.ranges.push_back({ subMesh.indexStart, static_cast<uint32>(count) });
        }
        if (!group.ranges.empty()) groups.push_back(group);
    }
    if (groups.empty()) return;

    //规范索引范围并让显式选择最后绘制。
    for (InstanceGroup& group : groups)
    {
        std::sort(group.ranges.begin(), group.ranges.end(), [](const IndexRange& a, const IndexRange& b)
        {
            return a.start != b.start ? a.start < b.start : a.count < b.count;
        });
        group.ranges.erase(std::unique(group.ranges.begin(), group.ranges.end()), group.ranges.end());
    }
    std::stable_sort(groups.begin(), groups.end(), [](const InstanceGroup& a, const InstanceGroup& b)
    {
        return a.selectionType > b.selectionType;
    });

    ImVec2 cameraPosition(sceneView.renderPosition.x, sceneView.renderPosition.y);
    ImVec2 cameraSize(sceneView.renderSize.x, sceneView.renderSize.y);
    if (cameraSize.x <= 0.0f || cameraSize.y <= 0.0f) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    drawList->PushClipRect(
        ImVec2(viewPosition.x, viewPosition.y),
        ImVec2(viewPosition.x + viewSize.x, viewPosition.y + viewSize.y),
        true);

    for (const InstanceGroup& group : groups)
    {
        const MeshTopology& topology = GetTopology(group.mesh, group.ranges);
        if (topology.vertices.empty() || topology.triangles.empty() || topology.edges.empty()) continue;

        //计算本实例所有焊接顶点的世界和裁剪坐标。
        matrix4x4 localToClip = RenderMath::Mul(camera->viewProjectionMatrix, group.localToWorld);
        worldVerticesScratch.resize(topology.vertices.size());
        clipVerticesScratch.resize(topology.vertices.size());
        for (usize index = 0; index < topology.vertices.size(); ++index)
        {
            worldVerticesScratch[index] = RenderMath::TransformPoint(group.localToWorld, topology.vertices[index]);
            clipVerticesScratch[index] = TransformClip(localToClip, topology.vertices[index]);
        }

        //按相机位置判断每个面的世界空间朝向。
        faceOrientationsScratch.assign(topology.triangles.size(), 0);
        for (usize index = 0; index < topology.triangles.size(); ++index)
        {
            const TopologyTriangle& triangle = topology.triangles[index];
            const vector3& a = worldVerticesScratch[triangle.a];
            const vector3& b = worldVerticesScratch[triangle.b];
            const vector3& c = worldVerticesScratch[triangle.c];
            vector3 ab = { b.x - a.x, b.y - a.y, b.z - a.z };
            vector3 ac = { c.x - a.x, c.y - a.y, c.z - a.z };
            vector3 normal = RenderMath::Cross(ab, ac);
            vector3 center = { (a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f, (a.z + b.z + c.z) / 3.0f };
            vector3 toCamera = { camera->position.x - center.x, camera->position.y - center.y, camera->position.z - center.z };
            float32 facing = RenderMath::Dot(normal, toCamera);
            float32 facingScaleSquared = RenderMath::Dot(normal, normal) * RenderMath::Dot(toCamera, toCamera);
            float32 thresholdSquared = facingScaleSquared * 0.000000000001f;
            if (facing * facing > thresholdSquared) faceOrientationsScratch[index] = facing > 0.0f ? 1 : -1;
        }

        //绘制开放边和正反混合轮廓边。
        for (const TopologyEdge& edge : topology.edges)
        {
            bool drawEdge = edge.faceCount == 1;
            bool hasFront = false;
            bool hasBack = false;
            bool hasTangent = false;
            for (uint32 faceIndex = 0; faceIndex < edge.faceCount; ++faceIndex)
            {
                uint32 face = topology.edgeFaces[edge.faceOffset + faceIndex];
                if (face >= faceOrientationsScratch.size()) continue;
                int8 orientation = faceOrientationsScratch[face];
                hasFront |= orientation > 0;
                hasBack |= orientation < 0;
                hasTangent |= orientation == 0;
            }
            drawEdge |= hasFront && hasBack;
            drawEdge |= hasTangent && (hasFront || hasBack);
            if (!drawEdge) continue;

            ClipPoint a = clipVerticesScratch[edge.a];
            ClipPoint b = clipVerticesScratch[edge.b];
            if (!ClipLine(a, b)) continue;
            if (std::abs(a.w) <= 0.000001f || std::abs(b.w) <= 0.000001f) continue;
            if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.w)
                || !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.w)) continue;

            ImVec2 screenA(
                cameraPosition.x + (a.x / a.w * 0.5f + 0.5f) * cameraSize.x,
                cameraPosition.y + (1.0f - (a.y / a.w * 0.5f + 0.5f)) * cameraSize.y);
            ImVec2 screenB(
                cameraPosition.x + (b.x / b.w * 0.5f + 0.5f) * cameraSize.x,
                cameraPosition.y + (1.0f - (b.y / b.w * 0.5f + 0.5f)) * cameraSize.y);
            DrawGlowLine(drawList, screenA, screenB, group.selectionType);
        }
    }

    drawList->PopClipRect();
}

//清空网格拓扑缓存。
void EditorScene::ClearTopologyCache()
{
    topologyCache.clear();
    topologyCache.rehash(0);
    worldVerticesScratch.clear();
    worldVerticesScratch.shrink_to_fit();
    clipVerticesScratch.clear();
    clipVerticesScratch.shrink_to_fit();
    faceOrientationsScratch.clear();
    faceOrientationsScratch.shrink_to_fit();
    frameIndex = 0;
}

//获取与网格数据和有效索引范围匹配的拓扑缓存。
const EditorScene::MeshTopology& EditorScene::GetTopology(Mesh* mesh, const List<IndexRange>& ranges)
{
    if (mesh && mesh->IsDirty(MeshDirtyFlags::Editor))
    {
        topologyCache.erase(mesh);
        mesh->ClearDirty(MeshDirtyFlags::Editor);
    }

    List<MeshTopology>& entries = topologyCache[mesh];
    int32 objectId = mesh ? mesh->GetObjectId() : 0;
    uint64 instanceHash = mesh ? mesh->GetInstanceId().GetHash() : 0;
    usize vertexCount = mesh ? mesh->vertices.size() : 0;
    usize indexCount = mesh ? mesh->indices.size() : 0;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [objectId, instanceHash, vertexCount, indexCount](const MeshTopology& entry)
    {
        return entry.objectId != objectId
            || entry.instanceHash != instanceHash
            || entry.vertexCount != vertexCount
            || entry.indexCount != indexCount;
    }), entries.end());
    for (MeshTopology& entry : entries)
    {
        if (entry.ranges == ranges)
        {
            entry.lastUsedFrame = frameIndex;
            return entry;
        }
    }

    MeshTopology topology;
    topology.objectId = objectId;
    topology.instanceHash = instanceHash;
    topology.vertexCount = vertexCount;
    topology.indexCount = indexCount;
    topology.lastUsedFrame = frameIndex;
    topology.ranges = ranges;
    if (!mesh || mesh->vertices.empty() || mesh->indices.empty())
    {
        entries.push_back(topology);
        return entries.back();
    }

    //按局部包围盒尺度焊接位置接近的顶点。
    const bounds3& bounds = mesh->GetLocalBounds();
    float32 boundsScale = bounds.valid
        ? std::max({ bounds.extents.x * 2.0f, bounds.extents.y * 2.0f, bounds.extents.z * 2.0f })
        : 0.0f;
    float32 weldEpsilon = std::max(boundsScale * 0.00001f, 0.0000001f);
    float32 weldEpsilonSquared = weldEpsilon * weldEpsilon;
    List<uint32> weldedIndices(mesh->vertices.size(), std::numeric_limits<uint32>::max());
    std::unordered_map<WeldKey, List<uint32>, WeldKeyHash> weldBuckets;
    for (usize index = 0; index < mesh->vertices.size(); ++index)
    {
        const vector3& point = mesh->vertices[index];
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        {
            weldedIndices[index] = static_cast<uint32>(topology.vertices.size());
            topology.vertices.push_back(point);
            continue;
        }

        WeldKey cell =
        {
            static_cast<int64>(std::floor(point.x / weldEpsilon)),
            static_cast<int64>(std::floor(point.y / weldEpsilon)),
            static_cast<int64>(std::floor(point.z / weldEpsilon)),
        };
        uint32 weldedIndex = std::numeric_limits<uint32>::max();
        for (int32 z = -1; z <= 1 && weldedIndex == std::numeric_limits<uint32>::max(); ++z)
        {
            for (int32 y = -1; y <= 1 && weldedIndex == std::numeric_limits<uint32>::max(); ++y)
            {
                for (int32 x = -1; x <= 1 && weldedIndex == std::numeric_limits<uint32>::max(); ++x)
                {
                    auto bucketIt = weldBuckets.find({ cell.x + x, cell.y + y, cell.z + z });
                    if (bucketIt == weldBuckets.end()) continue;
                    for (uint32 candidate : bucketIt->second)
                    {
                        const vector3& other = topology.vertices[candidate];
                        float32 dx = point.x - other.x;
                        float32 dy = point.y - other.y;
                        float32 dz = point.z - other.z;
                        if (dx * dx + dy * dy + dz * dz <= weldEpsilonSquared)
                        {
                            weldedIndex = candidate;
                            break;
                        }
                    }
                }
            }
        }

        if (weldedIndex == std::numeric_limits<uint32>::max())
        {
            weldedIndex = static_cast<uint32>(topology.vertices.size());
            topology.vertices.push_back(point);
            weldBuckets[cell].push_back(weldedIndex);
        }
        weldedIndices[index] = weldedIndex;
    }

    //构建三角形和支持任意邻接面数量的无向边。
    struct EdgeFacePair
    {
    public:
        uint32 edge = 0;
        uint32 face = 0;
    };

    std::unordered_map<uint64, uint32> edgeIndices;
    List<EdgeFacePair> edgeFacePairs;
    for (const IndexRange& range : ranges)
    {
        usize end = static_cast<usize>(range.start) + static_cast<usize>(range.count);
        if (end > mesh->indices.size()) continue;
        for (usize index = range.start; index + 2 < end; index += 3)
        {
            uint32 originalA = mesh->indices[index];
            uint32 originalB = mesh->indices[index + 1];
            uint32 originalC = mesh->indices[index + 2];
            if (originalA >= weldedIndices.size() || originalB >= weldedIndices.size() || originalC >= weldedIndices.size()) continue;

            uint32 a = weldedIndices[originalA];
            uint32 b = weldedIndices[originalB];
            uint32 c = weldedIndices[originalC];
            if (a == b || b == c || c == a) continue;

            uint32 face = static_cast<uint32>(topology.triangles.size());
            topology.triangles.push_back({ a, b, c });
            const uint32 edgeVertices[3][2] = { { a, b }, { b, c }, { c, a } };
            for (const auto& pair : edgeVertices)
            {
                uint32 edgeA = std::min(pair[0], pair[1]);
                uint32 edgeB = std::max(pair[0], pair[1]);
                uint64 edgeKey = (static_cast<uint64>(edgeA) << 32) | static_cast<uint64>(edgeB);
                auto [edgeIt, inserted] = edgeIndices.emplace(edgeKey, static_cast<uint32>(topology.edges.size()));
                if (inserted)
                {
                    TopologyEdge edge;
                    edge.a = edgeA;
                    edge.b = edgeB;
                    topology.edges.push_back(edge);
                }

                uint32 edgeIndex = edgeIt->second;
                ++topology.edges[edgeIndex].faceCount;
                edgeFacePairs.push_back({ edgeIndex, face });
            }
        }
    }

    //把邻接面压入连续数组。
    uint32 faceOffset = 0;
    List<uint32> writeOffsets(topology.edges.size(), 0);
    for (usize edgeIndex = 0; edgeIndex < topology.edges.size(); ++edgeIndex)
    {
        TopologyEdge& edge = topology.edges[edgeIndex];
        edge.faceOffset = faceOffset;
        writeOffsets[edgeIndex] = faceOffset;
        faceOffset += edge.faceCount;
    }
    topology.edgeFaces.resize(faceOffset);
    for (const EdgeFacePair& pair : edgeFacePairs)
    {
        topology.edgeFaces[writeOffsets[pair.edge]++] = pair.face;
    }

    entries.push_back(std::move(topology));
    return entries.back();
}

//计算顶点的齐次裁剪空间坐标。
EditorScene::ClipPoint EditorScene::TransformClip(const matrix4x4& matrix, const vector3& point)
{
    ClipPoint result;
    result.x = matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12];
    result.y = matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13];
    result.z = matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14];
    result.w = matrix.m[3] * point.x + matrix.m[7] * point.y + matrix.m[11] * point.z + matrix.m[15];
    return result;
}

//将齐次裁剪空间线段裁剪到六个视锥平面内。
bool EditorScene::ClipLine(ClipPoint& a, ClipPoint& b)
{
    const ClipPoint start = a;
    const ClipPoint end = b;
    float32 enter = 0.0f;
    float32 exit = 1.0f;
    const float32 distancesA[6] =
    {
        start.x + start.w,
        start.w - start.x,
        start.y + start.w,
        start.w - start.y,
        start.z + start.w,
        start.w - start.z,
    };
    const float32 distancesB[6] =
    {
        end.x + end.w,
        end.w - end.x,
        end.y + end.w,
        end.w - end.y,
        end.z + end.w,
        end.w - end.z,
    };

    //逐平面收紧线段的有效参数区间。
    for (int32 plane = 0; plane < 6; ++plane)
    {
        float32 distanceA = distancesA[plane];
        float32 distanceB = distancesB[plane];
        if (distanceA < 0.0f && distanceB < 0.0f) return false;
        if (distanceA >= 0.0f && distanceB >= 0.0f) continue;

        float32 denominator = distanceA - distanceB;
        if (std::abs(denominator) <= 0.000001f) return false;
        float32 parameter = distanceA / denominator;
        if (distanceA < 0.0f) enter = std::max(enter, parameter);
        else exit = std::min(exit, parameter);
        if (enter > exit) return false;
    }

    auto interpolate = [](float32 from, float32 to, float32 value)
    {
        return from + (to - from) * value;
    };
    a =
    {
        interpolate(start.x, end.x, enter),
        interpolate(start.y, end.y, enter),
        interpolate(start.z, end.z, enter),
        interpolate(start.w, end.w, enter),
    };
    b =
    {
        interpolate(start.x, end.x, exit),
        interpolate(start.y, end.y, exit),
        interpolate(start.z, end.z, exit),
        interpolate(start.w, end.w, exit),
    };
    return true;
}

//绘制托管 Scene Handles。
void EditorScene::DrawManagedGizmos()
{
    World& world = app.GetWorld();
    Transform* transform = world.GetTransform(cameraEns);
    Camera* camera = nullptr;
    if (Ens* editorCamera = world.GetEns(cameraEns)) camera = editorCamera->GetComponent<Camera>();
    if (!transform || !camera || !camera->IsRenderSceneEligible()) return;

    float32 aspect = sceneView.pixelHeight > 0
        ? static_cast<float32>(sceneView.pixelWidth) / static_cast<float32>(sceneView.pixelHeight)
        : 1.0f;
    matrix4x4 worldMatrix = RenderMath::TRS(
        transform->GetLocalPosition(),
        transform->GetLocalRotation(),
        transform->GetLocalScale());
    matrix4x4 viewMatrix = RenderMath::Inverse(worldMatrix);
    matrix4x4 projectionMatrix = RenderMath::Perspective(camera->fieldOfView, aspect, camera->nearPlane, camera->farPlane);
    gizmoViewProjection = RenderMath::Mul(projectionMatrix, viewMatrix);

    CurrentGizmoScene = this;
    managedBridge.DrawSceneGizmos();
    if (CurrentGizmoScene == this) CurrentGizmoScene = nullptr;
}
