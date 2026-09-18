#include "Editor/EditorGizmoHandles.h"

#include "Editor/EditorScene.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/World.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace
{
    //手柄各部件在屏幕上的像素尺寸，世界尺寸由枢轴处的换算比例推出
    constexpr float32 AxisPixels = 88.0f;
    constexpr float32 ArrowPixels = 14.0f;
    constexpr float32 ArrowWidthPixels = 9.0f;
    constexpr float32 AxisGapPixels = 4.0f;
    constexpr float32 PlaneOffsetPixels = 26.0f;
    constexpr float32 PlaneSizePixels = 18.0f;
    constexpr float32 CenterHalfPixels = 6.0f;
    constexpr float32 RingPixels = 78.0f;
    constexpr float32 OuterRingPixels = 92.0f;
    constexpr int32 RingSegments = 64;

    //命中容差与最小可交互面积
    constexpr float32 AxisPickPixels = 8.0f;
    constexpr float32 RingPickPixels = 9.0f;
    constexpr float32 CenterPickPixels = 7.0f;
    constexpr float32 MinPlaneScreenArea = 40.0f;
    constexpr float32 DegenerateEpsilon = 0.0001f;
    constexpr float32 MoveEpsilon = 0.0001f;

    //拖满一个手柄轴长对应的旋转角度与缩放比例
    constexpr float32 RotateDegreesPerAxis = 30.0f;
    constexpr float32 CenterScalePixelsPerDouble = 100.0f;

    constexpr float32 AxisThickness = 2.5f;
    constexpr float32 HotThickness = 3.5f;

    //三轴配色沿用 Unity 习惯，占用中统一转琥珀色
    constexpr EditorGizmoColor AxisColors[3] =
    {
        { 0.88f, 0.24f, 0.24f, 0.93f },
        { 0.50f, 0.83f, 0.20f, 0.93f },
        { 0.24f, 0.50f, 0.92f, 0.93f },
    };
    constexpr EditorGizmoColor HotColor = { 1.00f, 0.80f, 0.20f, 0.95f };
    constexpr EditorGizmoColor CenterColor = { 0.78f, 0.78f, 0.82f, 0.93f };

    //世界基轴，局部模式下再乘上手柄的参考旋转
    const vector3 BaseAxes[3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };

    //同一个向量按分量做四则运算，引擎矩阵工具里没有这层包装
    vector3 AddVector(const vector3& a, const vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    vector3 SubtractVector(const vector3& a, const vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    vector3 ScaleVector(const vector3& value, float32 scale) { return { value.x * scale, value.y * scale, value.z * scale }; }
    float32 LengthVector(const vector3& value) { return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z); }

    //屏幕坐标用同一套写法，省得每处都摊开分量
    vector2 AddVector(const vector2& a, const vector2& b) { return { a.x + b.x, a.y + b.y }; }
    vector2 SubtractVector(const vector2& a, const vector2& b) { return { a.x - b.x, a.y - b.y }; }
    vector2 ScaleVector(const vector2& value, float32 scale) { return { value.x * scale, value.y * scale }; }
    float32 LengthVector(const vector2& value) { return std::sqrt(value.x * value.x + value.y * value.y); }

    //按 Hamilton 约定做四元数乘法，使 R(a⊗b) = R(a)·R(b)
    quaternion MultiplyQuaternion(const quaternion& a, const quaternion& b)
    {
        return {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
        };
    }

    //单位四元数的共轭就是它的逆
    quaternion ConjugateQuaternion(const quaternion& value)
    {
        return { -value.x, -value.y, -value.z, value.w };
    }

    //归一化四元数
    quaternion NormalizeQuaternion(const quaternion& value)
    {
        float32 length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
        if (length <= DegenerateEpsilon) return quaternion();
        float32 inverseLength = 1.0f / length;
        return { value.x * inverseLength, value.y * inverseLength, value.z * inverseLength, value.w * inverseLength };
    }

    //绕单位轴构造旋转四元数
    quaternion AxisAngleQuaternion(const vector3& axis, float32 angle)
    {
        float32 half = angle * 0.5f;
        float32 sine = std::sin(half);
        return { axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half) };
    }

    //把角度折算到 (-π, π]，用于逐帧累加旋转量
    float32 WrapPi(float32 angle)
    {
        constexpr float32 TwoPi = 6.28318530718f;
        return angle - TwoPi * std::round(angle / TwoPi);
    }

    //点到线段的屏幕距离
    float32 DistanceToSegment(const vector2& point, const vector2& a, const vector2& b)
    {
        float32 abx = b.x - a.x;
        float32 aby = b.y - a.y;
        float32 lengthSquared = abx * abx + aby * aby;
        float32 t = lengthSquared > DegenerateEpsilon
            ? std::clamp(((point.x - a.x) * abx + (point.y - a.y) * aby) / lengthSquared, 0.0f, 1.0f)
            : 0.0f;
        float32 dx = point.x - (a.x + abx * t);
        float32 dy = point.y - (a.y + aby * t);
        return std::sqrt(dx * dx + dy * dy);
    }

    //射线与轴线的最近点参数，denominator 趋于零表示两者几乎平行
    bool ClosestAxisParameter(const vector3& origin, const vector3& direction, const vector3& pivot,
        const vector3& axis, float32& parameter)
    {
        vector3 w0 = SubtractVector(origin, pivot);
        float32 b = RenderMath::Dot(direction, axis);
        float32 d = RenderMath::Dot(direction, w0);
        float32 e = RenderMath::Dot(axis, w0);
        float32 denominator = 1.0f - b * b;
        if (std::abs(denominator) <= DegenerateEpsilon) return false;

        parameter = (e - b * d) / denominator;
        return true;
    }

    //射线与平面求交，返回交点
    bool IntersectPlane(const vector3& origin, const vector3& direction, const vector3& point,
        const vector3& normal, vector3& hit)
    {
        float32 denominator = RenderMath::Dot(normal, direction);
        if (std::abs(denominator) <= 0.000001f) return false;

        float32 distance = RenderMath::Dot(normal, SubtractVector(point, origin)) / denominator;
        if (distance <= 0.0f) return false;

        hit = AddVector(origin, ScaleVector(direction, distance));
        return true;
    }

    //屏幕位移换算成沿某个三维轴的世界位移，自带透视缩短与轴向翻转处理
    float32 CalcLineTranslation(const EditorGizmoView& view, const vector2& from, const vector2& to,
        const vector3& origin, const vector3& axis)
    {
        vector2 originScreen;
        vector2 unitScreen;
        if (!EditorGizmoHandles::ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, origin, originScreen)
            || !EditorGizmoHandles::ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize,
                AddVector(origin, axis), unitScreen))
            return 0.0f;

        float32 axisX = unitScreen.x - originScreen.x;
        float32 axisY = unitScreen.y - originScreen.y;
        float32 lengthSquared = axisX * axisX + axisY * axisY;
        if (lengthSquared <= DegenerateEpsilon) return 0.0f;

        //轴向背对相机时取反，保证拖拽方向与视觉一致
        float32 invert = RenderMath::Dot(axis, view.cameraForward) < 0.0f ? -1.0f : 1.0f;
        float32 fromX = from.x - originScreen.x;
        float32 fromY = from.y - originScreen.y;
        float32 toX = to.x - originScreen.x;
        float32 toY = to.y - originScreen.y;
        return ((toX * axisX + toY * axisY) - (fromX * axisX + fromY * axisY)) / lengthSquared * invert;
    }

    //把线性颜色转成 ImGui 颜色，可按透明度缩放
    ImU32 ToImColor(const EditorGizmoColor& color, float32 alphaScale = 1.0f)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            std::clamp(color.r, 0.0f, 1.0f),
            std::clamp(color.g, 0.0f, 1.0f),
            std::clamp(color.b, 0.0f, 1.0f),
            std::clamp(color.a * alphaScale, 0.0f, 1.0f)));
    }

    ImVec2 ToImVec2(const vector2& value) { return ImVec2(value.x, value.y); }

    //手柄轴序号转颜色索引
    int32 GetAxisIndex(EditorGizmoHandle handle)
    {
        switch (handle)
        {
        case EditorGizmoHandle::AxisX:
        case EditorGizmoHandle::PlaneYZ: return 0;
        case EditorGizmoHandle::AxisY:
        case EditorGizmoHandle::PlaneXZ: return 1;
        default: return 2;
        }
    }
}

EditorGizmoHandles::EditorGizmoHandles(EditorScene& owner)
    : scene(owner)
{
}

EditorGizmoMode EditorGizmoHandles::GetMode() const { return mode; }

void EditorGizmoHandles::SetMode(EditorGizmoMode value) { mode = value; }

EditorGizmoOrientation EditorGizmoHandles::GetOrientation() const { return orientation; }

void EditorGizmoHandles::SetOrientation(EditorGizmoOrientation value) { orientation = value; }

bool EditorGizmoHandles::IsDragging() const { return dragActive; }

bool EditorGizmoHandles::OwnsMouse() const { return dragActive || hovered != EditorGizmoHandle::None; }

bool EditorGizmoHandles::TakeEdit(EditorGizmoEdit& edit)
{
    if (pendingEdits.empty()) return false;

    edit = pendingEdits.front();
    pendingEdits.erase(pendingEdits.begin());
    return true;
}

//把世界点投影到场景视口屏幕坐标
bool EditorGizmoHandles::ProjectPoint(const matrix4x4& viewProjection, const vector2& renderPosition,
    const vector2& renderSize, const vector3& point, vector2& screen)
{
    if (renderSize.x <= 0.0f || renderSize.y <= 0.0f) return false;

    float32 x = viewProjection.m[0] * point.x + viewProjection.m[4] * point.y + viewProjection.m[8] * point.z + viewProjection.m[12];
    float32 y = viewProjection.m[1] * point.x + viewProjection.m[5] * point.y + viewProjection.m[9] * point.z + viewProjection.m[13];
    float32 z = viewProjection.m[2] * point.x + viewProjection.m[6] * point.y + viewProjection.m[10] * point.z + viewProjection.m[14];
    float32 w = viewProjection.m[3] * point.x + viewProjection.m[7] * point.y + viewProjection.m[11] * point.z + viewProjection.m[15];
    if (std::abs(w) <= DegenerateEpsilon || w < 0.0f) return false;

    float32 inverseW = 1.0f / w;
    float32 ndcZ = z * inverseW;
    if (ndcZ < -1.0f || ndcZ > 1.0f) return false;

    screen.x = renderPosition.x + (x * inverseW * 0.5f + 0.5f) * renderSize.x;
    screen.y = renderPosition.y + (1.0f - (y * inverseW * 0.5f + 0.5f)) * renderSize.y;
    return true;
}

//把场景视口屏幕坐标转换成世界射线
bool EditorGizmoHandles::BuildScreenRay(const matrix4x4& viewProjection, const vector2& renderPosition,
    const vector2& renderSize, const vector2& screenPosition, vector3& origin, vector3& direction,
    float32& distance)
{
    if (renderSize.x <= 0.0f || renderSize.y <= 0.0f) return false;

    float32 ndcX = ((screenPosition.x - renderPosition.x) / renderSize.x) * 2.0f - 1.0f;
    float32 ndcY = 1.0f - ((screenPosition.y - renderPosition.y) / renderSize.y) * 2.0f;
    matrix4x4 inverseViewProjection = RenderMath::Inverse(viewProjection);
    origin = RenderMath::TransformPoint(inverseViewProjection, { ndcX, ndcY, -1.0f });
    vector3 end = RenderMath::TransformPoint(inverseViewProjection, { ndcX, ndcY, 1.0f });
    vector3 delta = SubtractVector(end, origin);
    float32 length = LengthVector(delta);
    if (length <= DegenerateEpsilon) return false;

    direction = ScaleVector(delta, 1.0f / length);
    distance = length;
    return true;
}

//缓存单个目标的局部变换与父级世界矩阵
bool EditorGizmoHandles::CaptureTarget(World& world, EnsId ens, Target& target)
{
    Transform* transform = world.GetTransform(ens);
    if (!transform) return false;

    target.ens = ens;
    target.worldPosition = transform->worldPosition;
    target.worldRotation = transform->worldRotation;
    const vector3& localPosition = transform->GetLocalPosition();
    const quaternion& localRotation = transform->GetLocalRotation();
    const vector3& localScale = transform->GetLocalScale();
    target.localPosition = { localPosition.x, localPosition.y, localPosition.z };
    target.localRotation = { localRotation.x, localRotation.y, localRotation.z, localRotation.w };
    target.localScale = { localScale.x, localScale.y, localScale.z };

    //父级世界矩阵与父级世界旋转分别用于位置与旋转变换
    EnsId parent = transform->GetParent();
    Transform* parentTransform = parent.IsNull() ? nullptr : world.GetTransform(parent);
    if (parentTransform)
    {
        target.parentWorldMatrix = parentTransform->worldMatrix;
        target.parentWorldRotation = parentTransform->worldRotation;
    }
    else
    {
        target.parentWorldMatrix = matrix4x4();
        target.parentWorldRotation = quaternion();
    }
    return true;
}

//收集选择集并算出枢轴与三个手柄轴
bool EditorGizmoHandles::BuildContext(World& world, const EditorGizmoView& view, Context& context) const
{
    context.valid = false;
    const List<EnsId>& selection = scene.GetSelectedEnsList();
    if (selection.empty() || !view.valid) return false;

    //枢轴取选择集包围盒中心，没有网格时退化为位置平均
    bool hasBounds = false;
    bounds3 merged;
    vector3 positionSum = { 0.0f, 0.0f, 0.0f };
    int32 aliveCount = 0;
    for (EnsId ens : selection)
    {
        if (ens.IsNull() || !world.IsAlive(ens) || scene.IsTemporaryEns(ens)) continue;

        Transform* transform = world.GetTransform(ens);
        if (!transform) continue;

        ++aliveCount;
        positionSum = AddVector(positionSum, transform->worldPosition);

        StaticMeshRenderer* renderer = world.GetEns(ens) ? world.GetEns(ens)->GetComponent<StaticMeshRenderer>() : nullptr;
        Mesh* mesh = renderer ? renderer->GetRenderMesh() : nullptr;
        if (!mesh) continue;

        const bounds3& localBounds = mesh->GetLocalBounds();
        if (!localBounds.valid) continue;

        bounds3 worldBounds = RenderMath::TransformBounds(transform->worldMatrix, localBounds);
        if (!worldBounds.valid) continue;

        if (!hasBounds)
        {
            merged = worldBounds;
            hasBounds = true;
            continue;
        }

        vector3 minValue = {
            std::min(merged.center.x - merged.extents.x, worldBounds.center.x - worldBounds.extents.x),
            std::min(merged.center.y - merged.extents.y, worldBounds.center.y - worldBounds.extents.y),
            std::min(merged.center.z - merged.extents.z, worldBounds.center.z - worldBounds.extents.z) };
        vector3 maxValue = {
            std::max(merged.center.x + merged.extents.x, worldBounds.center.x + worldBounds.extents.x),
            std::max(merged.center.y + merged.extents.y, worldBounds.center.y + worldBounds.extents.y),
            std::max(merged.center.z + merged.extents.z, worldBounds.center.z + worldBounds.extents.z) };
        merged.center = ScaleVector(AddVector(minValue, maxValue), 0.5f);
        merged.extents = ScaleVector(SubtractVector(maxValue, minValue), 0.5f);
    }
    if (aliveCount == 0) return false;

    context.pivot = hasBounds ? merged.center : ScaleVector(positionSum, 1.0f / static_cast<float32>(aliveCount));

    //局部轴取主选中对象的世界旋转，与 Unity 的 active transform 语义一致
    EnsId activeEns = scene.GetSelectedEns();
    Transform* activeTransform = activeEns.IsNull() ? nullptr : world.GetTransform(activeEns);
    context.pivotRotation = (orientation == EditorGizmoOrientation::Local && activeTransform)
        ? activeTransform->worldRotation
        : quaternion();

    matrix4x4 rotationMatrix = RenderMath::Rotation(context.pivotRotation);
    for (int32 index = 0; index < 3; ++index)
        context.axes[index] = RenderMath::Normalize(RenderMath::TransformDirection(rotationMatrix, BaseAxes[index]));

    //枢轴沿相机右方向偏移一个世界单位对应的像素长度，用来把像素尺寸换算成世界尺寸
    vector2 pivotScreen;
    vector2 offsetScreen;
    if (!ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, context.pivot, pivotScreen)) return false;
    if (!ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize,
        AddVector(context.pivot, view.cameraRight), offsetScreen)) return false;

    float32 pixelsPerUnit = std::sqrt(
        (offsetScreen.x - pivotScreen.x) * (offsetScreen.x - pivotScreen.x)
        + (offsetScreen.y - pivotScreen.y) * (offsetScreen.y - pivotScreen.y));
    if (pixelsPerUnit <= DegenerateEpsilon) return false;

    context.worldPerPixel = 1.0f / pixelsPerUnit;
    context.valid = true;
    return true;
}

//屏幕空间命中测试，返回命中的子控件
EditorGizmoHandle EditorGizmoHandles::Pick(const EditorGizmoView& view, const Context& context, const vector2& mouse) const
{
    vector2 pivotScreen;
    if (!ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, context.pivot, pivotScreen))
        return EditorGizmoHandle::None;

    //中心方块（位移的屏幕空间拖拽、缩放的等比）优先于其它控件
    if (mode != EditorGizmoMode::Rotate
        && std::abs(mouse.x - pivotScreen.x) <= CenterPickPixels
        && std::abs(mouse.y - pivotScreen.y) <= CenterPickPixels)
        return EditorGizmoHandle::Center;

    if (mode == EditorGizmoMode::Rotate)
    {
        float32 bestDistance = RingPickPixels;
        EditorGizmoHandle best = EditorGizmoHandle::None;
        //外圈是屏幕空间圆，与三条圆环一起按屏幕距离竞争
        float32 outerRadius = LengthVector(SubtractVector(mouse, pivotScreen));
        if (std::abs(outerRadius - OuterRingPixels) <= bestDistance)
        {
            bestDistance = std::abs(outerRadius - OuterRingPixels);
            best = EditorGizmoHandle::Center;
        }

        for (int32 axisIndex = 0; axisIndex < 3; ++axisIndex)
        {
            vector3 u = BaseAxes[axisIndex];
            vector3 v = BaseAxes[(axisIndex + 1) % 3];
            quaternion pivotRotation = context.pivotRotation;
            matrix4x4 rotationMatrix = RenderMath::Rotation(pivotRotation);
            u = RenderMath::TransformDirection(rotationMatrix, u);
            v = RenderMath::TransformDirection(rotationMatrix, v);

            vector2 previous;
            bool hasPrevious = false;
            for (int32 segment = 0; segment <= RingSegments; ++segment)
            {
                float32 angle = 6.28318530718f * static_cast<float32>(segment) / static_cast<float32>(RingSegments);
                vector3 point = AddVector(context.pivot, ScaleVector(AddVector(
                    ScaleVector(u, std::cos(angle)), ScaleVector(v, std::sin(angle))), RingPixels * context.worldPerPixel));

                vector2 current;
                if (!ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, point, current))
                {
                    hasPrevious = false;
                    continue;
                }
                if (hasPrevious)
                {
                    float32 distance = DistanceToSegment(mouse, previous, current);
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        best = static_cast<EditorGizmoHandle>(static_cast<int32>(EditorGizmoHandle::AxisX) + axisIndex);
                    }
                }
                previous = current;
                hasPrevious = true;
            }
        }
        return best;
    }

    //缩放固定使用局部轴，与世界空间缩放无法用局部 scale 表达有关
    float32 bestAxisDistance = AxisPickPixels;
    EditorGizmoHandle best = EditorGizmoHandle::None;
    for (int32 axisIndex = 0; axisIndex < 3; ++axisIndex)
    {
        vector3 axis = mode == EditorGizmoMode::Scale
            ? RenderMath::TransformDirection(RenderMath::Rotation(context.pivotRotation), BaseAxes[axisIndex])
            : context.axes[axisIndex];

        vector2 axisStart;
        vector2 axisEnd;
        if (!ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, context.pivot, axisStart)) continue;
        if (!ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize,
            AddVector(context.pivot, ScaleVector(axis, AxisPixels * context.worldPerPixel)), axisEnd)) continue;

        float32 distance = DistanceToSegment(mouse, axisStart, axisEnd);
        if (distance < bestAxisDistance)
        {
            bestAxisDistance = distance;
            best = static_cast<EditorGizmoHandle>(static_cast<int32>(EditorGizmoHandle::AxisX) + axisIndex);
        }
    }
    if (best != EditorGizmoHandle::None || mode != EditorGizmoMode::Move) return best;

    //三个平面块：投影成四边形后做同号叉积的内点判定
    const int32 planeNormals[3] = { 2, 0, 1 };
    float32 bestPlaneArea = MinPlaneScreenArea;
    for (int32 planeIndex = 0; planeIndex < 3; ++planeIndex)
    {
        int32 normalIndex = planeNormals[planeIndex];
        vector3 normal = context.axes[normalIndex];
        if (std::abs(RenderMath::Dot(normal, view.cameraForward)) > 0.999f) continue;

        const vector3& axisA = context.axes[(normalIndex + 1) % 3];
        const vector3& axisB = context.axes[(normalIndex + 2) % 3];
        float32 offset = PlaneOffsetPixels * context.worldPerPixel;
        float32 size = PlaneSizePixels * context.worldPerPixel;

        vector2 corners[4];
        const float32 offsets[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
        bool visible = true;
        for (int32 corner = 0; corner < 4 && visible; ++corner)
        {
            vector3 point = AddVector(context.pivot, AddVector(
                ScaleVector(axisA, offset + size * offsets[corner][0]),
                ScaleVector(axisB, offset + size * offsets[corner][1])));
            visible = ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, point, corners[corner]);
        }
        if (!visible) continue;

        float32 area = std::abs((corners[1].x - corners[0].x) * (corners[3].y - corners[0].y)
            - (corners[3].x - corners[0].x) * (corners[1].y - corners[0].y));
        if (area < bestPlaneArea) continue;

        bool inside = true;
        for (int32 edge = 0; edge < 4 && inside; ++edge)
        {
            const vector2& a = corners[edge];
            const vector2& b = corners[(edge + 1) % 4];
            float32 cross = (b.x - a.x) * (mouse.y - a.y) - (b.y - a.y) * (mouse.x - a.x);
            float32 reference = (b.x - a.x) * (corners[(edge + 2) % 4].y - a.y) - (b.y - a.y) * (corners[(edge + 2) % 4].x - a.x);
            inside = (cross >= 0.0f) == (reference >= 0.0f);
        }
        if (!inside) continue;

        bestPlaneArea = area;
        best = static_cast<EditorGizmoHandle>(static_cast<int32>(EditorGizmoHandle::PlaneXY) + planeIndex);
    }
    return best;
}

//拖拽开始时缓存全部目标与起始状态
void EditorGizmoHandles::BeginDrag(World& world, const EditorGizmoView& view, const Context& context, EditorGizmoHandle handle)
{
    targets.clear();
    const List<EnsId>& selection = scene.GetSelectedEnsList();
    for (EnsId ens : selection)
    {
        if (ens.IsNull() || !world.IsAlive(ens) || scene.IsTemporaryEns(ens)) continue;

        Target target;
        if (CaptureTarget(world, ens, target)) targets.push_back(target);
    }
    if (targets.empty()) return;

    active = handle;
    dragActive = true;
    currentScaleFactor = 1.0f;
    pivotWorld = context.pivot;
    for (int32 index = 0; index < 3; ++index)
    {
        //缩放固定用局部轴，其余模式跟随当前坐标系
        startAxes[index] = mode == EditorGizmoMode::Scale
            ? RenderMath::TransformDirection(RenderMath::Rotation(context.pivotRotation), BaseAxes[index])
            : context.axes[index];
    }
    startMouse = { ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y };
    if (!ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, context.pivot, startPivotScreen))
        startPivotScreen = startMouse;

    int32 axisIndex = GetAxisIndex(handle);
    switch (mode)
    {
    case EditorGizmoMode::Move:
    {
        if (handle == EditorGizmoHandle::Center)
        {
            vector3 origin;
            vector3 direction;
            float32 rayDistance = 0.0f;
            if (BuildScreenRay(view.viewProjection, view.renderPosition, view.renderSize, startMouse, origin, direction, rayDistance))
                IntersectPlane(origin, direction, pivotWorld, ScaleVector(view.cameraForward, -1.0f), startPlaneHit);
            break;
        }
        //平面手柄的法线是被排除的那个轴
        if (handle >= EditorGizmoHandle::PlaneXY && handle <= EditorGizmoHandle::PlaneXZ)
        {
            vector3 normal = startAxes[axisIndex];
            vector3 origin;
            vector3 direction;
            float32 rayDistance = 0.0f;
            if (BuildScreenRay(view.viewProjection, view.renderPosition, view.renderSize, startMouse, origin, direction, rayDistance))
                IntersectPlane(origin, direction, pivotWorld, normal, startPlaneHit);
            break;
        }

        vector3 origin;
        vector3 direction;
        float32 rayDistance = 0.0f;
        if (BuildScreenRay(view.viewProjection, view.renderPosition, view.renderSize, startMouse, origin, direction, rayDistance))
            ClosestAxisParameter(origin, direction, pivotWorld, startAxes[axisIndex], startAxisParameter);
        break;
    }
    case EditorGizmoMode::Rotate:
    {
        //外圈没有对应的世界轴，绕视线方向转，与它画在屏幕平面上一致
        if (handle == EditorGizmoHandle::Center) startAxes[axisIndex] = view.cameraForward;
        startMouseAngle = std::atan2(startMouse.y - startPivotScreen.y, startMouse.x - startPivotScreen.x);
        previousMouseAngle = startMouseAngle;
        break;
    }
    case EditorGizmoMode::Scale:
    {
        //轴端缩放以该次拖拽的轴世界长度为基准；中心等比以起始鼠标距离为基准
        startAxisReference = AxisPixels * context.worldPerPixel;
        vector3 origin;
        vector3 direction;
        float32 rayDistance = 0.0f;
        if (BuildScreenRay(view.viewProjection, view.renderPosition, view.renderSize, startMouse, origin, direction, rayDistance))
            ClosestAxisParameter(origin, direction, pivotWorld, startAxes[axisIndex], startAxisParameter);

        startMouseDistance = LengthVector(SubtractVector(startMouse, startPivotScreen));
        break;
    }
    }
}

//按当前模式推进一次拖拽
void EditorGizmoHandles::ApplyDrag(World& world, const EditorGizmoView& view)
{
    if (!dragActive || targets.empty()) return;

    ImVec2 mousePosition = ImGui::GetIO().MousePos;
    vector2 mouse = { mousePosition.x, mousePosition.y };
    int32 axisIndex = GetAxisIndex(active);
    float32 axisParameter = startAxisParameter;
    vector3 planeHit = startPlaneHit;
    bool hasAxis = false;
    bool hasPlaneHit = false;

    vector3 origin;
    vector3 direction;
    float32 rayDistance = 0.0f;
    bool hasRay = BuildScreenRay(view.viewProjection, view.renderPosition, view.renderSize, mouse, origin, direction, rayDistance);

    if (mode == EditorGizmoMode::Move)
    {
        if (active == EditorGizmoHandle::Center)
        {
            hasPlaneHit = hasRay && IntersectPlane(origin, direction, pivotWorld, ScaleVector(view.cameraForward, -1.0f), planeHit);
        }
        else if (active >= EditorGizmoHandle::PlaneXY && active <= EditorGizmoHandle::PlaneXZ)
        {
            vector3 normal = startAxes[axisIndex];
            hasPlaneHit = hasRay && IntersectPlane(origin, direction, pivotWorld, normal, planeHit);
        }
        else
        {
            hasAxis = hasRay && ClosestAxisParameter(origin, direction, pivotWorld, startAxes[axisIndex], axisParameter);
        }

        vector3 delta = { 0.0f, 0.0f, 0.0f };
        if (hasAxis) delta = ScaleVector(startAxes[axisIndex], axisParameter - startAxisParameter);
        else if (hasPlaneHit) delta = SubtractVector(planeHit, startPlaneHit);
        else return;

        for (const Target& target : targets)
        {
            Transform* transform = world.GetTransform(target.ens);
            if (!transform) continue;

            vector3 worldPosition = AddVector(target.worldPosition, delta);
            transform->SetLocalPosition(
                RenderMath::TransformPoint(RenderMath::Inverse(target.parentWorldMatrix), worldPosition));
        }
        return;
    }

    if (mode == EditorGizmoMode::Rotate)
    {
        float32 angle = std::atan2(mouse.y - startPivotScreen.y, mouse.x - startPivotScreen.x);
        float32 step = WrapPi(angle - previousMouseAngle);
        if (RenderMath::Dot(startAxes[axisIndex], view.cameraForward) > 0.0f) step = -step;
        previousMouseAngle = angle;

        //圆心固定，用起始角度累加得到总旋转量，避免跨 ±π 跳变
        float32 accumulated = WrapPi(angle - startMouseAngle);
        quaternion rotation = AxisAngleQuaternion(startAxes[axisIndex], accumulated);

        for (const Target& target : targets)
        {
            Transform* transform = world.GetTransform(target.ens);
            if (!transform) continue;

            quaternion worldRotation = MultiplyQuaternion(rotation, target.worldRotation);
            vector3 offset = SubtractVector(target.worldPosition, pivotWorld);
            vector3 rotatedOffset = RenderMath::TransformDirection(RenderMath::Rotation(rotation), offset);
            vector3 worldPosition = AddVector(pivotWorld, rotatedOffset);

            transform->SetLocalPosition(
                RenderMath::TransformPoint(RenderMath::Inverse(target.parentWorldMatrix), worldPosition));
            transform->SetLocalRotation(NormalizeQuaternion(
                MultiplyQuaternion(ConjugateQuaternion(target.parentWorldRotation), worldRotation)));
        }
        return;
    }

    //缩放：两种子控件都在各自的分量上绕锁定枢轴散开，枢轴处保持不动
    if (active == EditorGizmoHandle::Center)
    {
        float32 distance = LengthVector(SubtractVector(mouse, startPivotScreen));
        if (startMouseDistance <= 4.0f) startMouseDistance = std::max(distance, 1.0f);
        float32 ratio = std::max(distance / startMouseDistance, 0.0001f);
        currentScaleFactor = ratio;

        for (const Target& target : targets)
        {
            Transform* transform = world.GetTransform(target.ens);
            if (!transform) continue;

            vector3 offset = SubtractVector(target.worldPosition, pivotWorld);
            vector3 worldPosition = AddVector(pivotWorld, ScaleVector(offset, ratio));
            transform->SetLocalPosition(
                RenderMath::TransformPoint(RenderMath::Inverse(target.parentWorldMatrix), worldPosition));
            transform->SetLocalScale({
                target.localScale.x * ratio,
                target.localScale.y * ratio,
                target.localScale.z * ratio });
        }
        return;
    }

    if (!hasRay || !ClosestAxisParameter(origin, direction, pivotWorld, startAxes[axisIndex], axisParameter)) return;

    float32 reference = startAxisReference > DegenerateEpsilon ? startAxisReference : 1.0f;
    float32 factor = std::max(1.0f + (axisParameter - startAxisParameter) / reference, 0.0001f);
    currentScaleFactor = factor;

    for (const Target& target : targets)
    {
        Transform* transform = world.GetTransform(target.ens);
        if (!transform) continue;

        vector3 localScale = { target.localScale.x, target.localScale.y, target.localScale.z };
        if (axisIndex == 0) localScale.x *= factor;
        else if (axisIndex == 1) localScale.y *= factor;
        else localScale.z *= factor;
        transform->SetLocalScale(localScale);

        //只有沿该轴的分量跟着缩放，与单轴缩放的视觉一致
        vector3 offset = SubtractVector(target.worldPosition, pivotWorld);
        float32 along = RenderMath::Dot(offset, startAxes[axisIndex]);
        vector3 worldPosition = AddVector(pivotWorld,
            AddVector(offset, ScaleVector(startAxes[axisIndex], along * (factor - 1.0f))));
        transform->SetLocalPosition(
            RenderMath::TransformPoint(RenderMath::Inverse(target.parentWorldMatrix), worldPosition));
    }
}

//还原拖拽起始状态
void EditorGizmoHandles::RestoreTargets(World& world) const
{
    for (const Target& target : targets)
    {
        Transform* transform = world.GetTransform(target.ens);
        if (!transform) continue;

        transform->SetLocalPosition({ target.localPosition.x, target.localPosition.y, target.localPosition.z });
        transform->SetLocalRotation({ target.localRotation.x, target.localRotation.y, target.localRotation.z, target.localRotation.w });
        transform->SetLocalScale({ target.localScale.x, target.localScale.y, target.localScale.z });
    }
}

//记录一条待提交的编辑
void EditorGizmoHandles::PushEdit(const EditorGizmoEdit& edit)
{
    pendingEdits.push_back(edit);
}

//取消拖拽并还原起始变换
void EditorGizmoHandles::CancelDrag(World& world)
{
    if (!dragActive)
    {
        hovered = EditorGizmoHandle::None;
        return;
    }

    RestoreTargets(world);
    dragActive = false;
    currentScaleFactor = 1.0f;
    active = EditorGizmoHandle::None;
    hovered = EditorGizmoHandle::None;
    targets.clear();
}

//每帧处理命中与拖拽；拖拽期间直接写入 Transform
void EditorGizmoHandles::Update(World& world, const EditorGizmoView& view, bool interactive)
{
    Context context;
    if (!BuildContext(world, view, context))
    {
        CancelDrag(world);
        return;
    }

    if (dragActive)
    {
        //拖拽中按 Esc 立即回滚，其余情况下持续按当前鼠标推进
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            CancelDrag(world);
            return;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ApplyDrag(world, view);
            return;
        }

        //没收到松开事件（失焦等）时按取消处理，避免留下半截状态
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            for (const Target& target : targets)
            {
                Transform* transform = world.GetTransform(target.ens);
                if (!transform) continue;

                const vector3& localPosition = transform->GetLocalPosition();
                const quaternion& localRotation = transform->GetLocalRotation();
                const vector3& localScale = transform->GetLocalScale();

                //真的动了才留撤销记录，纯点击不留
                bool moved = std::abs(localPosition.x - target.localPosition.x) > MoveEpsilon
                    || std::abs(localPosition.y - target.localPosition.y) > MoveEpsilon
                    || std::abs(localPosition.z - target.localPosition.z) > MoveEpsilon
                    || std::abs(localRotation.x - target.localRotation.x) > MoveEpsilon
                    || std::abs(localRotation.y - target.localRotation.y) > MoveEpsilon
                    || std::abs(localRotation.z - target.localRotation.z) > MoveEpsilon
                    || std::abs(localRotation.w - target.localRotation.w) > MoveEpsilon
                    || std::abs(localScale.x - target.localScale.x) > MoveEpsilon
                    || std::abs(localScale.y - target.localScale.y) > MoveEpsilon
                    || std::abs(localScale.z - target.localScale.z) > MoveEpsilon;
                if (!moved) continue;

                EditorGizmoEdit edit;
                edit.ens = target.ens;
                edit.mode = static_cast<int32>(mode);
                edit.startPosition = target.localPosition;
                edit.startRotation = target.localRotation;
                edit.startScale = target.localScale;
                edit.endPosition = { localPosition.x, localPosition.y, localPosition.z };
                edit.endRotation = { localRotation.x, localRotation.y, localRotation.z, localRotation.w };
                edit.endScale = { localScale.x, localScale.y, localScale.z };
                PushEdit(edit);
            }

            dragActive = false;
            active = EditorGizmoHandle::None;
            hovered = EditorGizmoHandle::None;
            targets.clear();
            return;
        }

        CancelDrag(world);
        return;
    }

    if (!interactive)
    {
        hovered = EditorGizmoHandle::None;
        return;
    }

    ImVec2 mousePosition = ImGui::GetIO().MousePos;
    hovered = Pick(view, context, { mousePosition.x, mousePosition.y });
    if (hovered == EditorGizmoHandle::None) return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;

    BeginDrag(world, view, context, hovered);
}

//在场景叠加层最上方绘制手柄
void EditorGizmoHandles::Draw(World& world, const EditorGizmoView& view) const
{
    Context context;
    if (!BuildContext(world, view, context)) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    vector2 pivotScreen;
    if (!ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, context.pivot, pivotScreen)) return;

    EditorGizmoHandle effective = dragActive ? active : hovered;

    //绘制顺序：中心与外圈、平面块、轴、圆环，占用中的最后重画一遍保证在最上层
    auto drawAxis = [&](int32 axisIndex)
    {
        vector3 axis = mode == EditorGizmoMode::Scale
            ? RenderMath::TransformDirection(RenderMath::Rotation(context.pivotRotation), BaseAxes[axisIndex])
            : context.axes[axisIndex];

        EditorGizmoHandle handle = static_cast<EditorGizmoHandle>(static_cast<int32>(EditorGizmoHandle::AxisX) + axisIndex);
        EditorGizmoColor color = effective == handle ? HotColor : AxisColors[axisIndex];
        float32 alphaScale = RenderMath::Dot(axis, view.cameraForward) > 0.15f ? 0.45f : 1.0f;
        float32 thickness = effective == handle ? HotThickness : AxisThickness;

        //缩放拖拽中轴端方块跟着比例外移，拖到哪方块就在哪
        float32 axisLength = AxisPixels * context.worldPerPixel;
        if (mode == EditorGizmoMode::Scale && dragActive) axisLength *= currentScaleFactor;

        vector2 start;
        vector2 end;
        if (!ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, context.pivot, start)) return;
        if (!ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize,
            AddVector(context.pivot, ScaleVector(axis, axisLength)), end)) return;

        vector2 direction = SubtractVector(end, start);
        float32 length = LengthVector(direction);
        if (length <= DegenerateEpsilon) return;

        vector2 unit = ScaleVector(direction, 1.0f / length);
        vector2 lineStart = AddVector(start, ScaleVector(unit, AxisGapPixels));
        drawList->AddLine(ToImVec2(lineStart), ToImVec2(end), ToImColor(color, alphaScale), thickness);

        //背对相机的轴不画端帽，避免视觉上喧宾夺主
        if (alphaScale < 1.0f) return;

        vector2 normal = { -unit.y, unit.x };
        if (mode == EditorGizmoMode::Scale)
        {
            constexpr float32 HalfPixels = 5.0f;
            drawList->AddRectFilled(
                ImVec2(end.x - HalfPixels, end.y - HalfPixels),
                ImVec2(end.x + HalfPixels, end.y + HalfPixels),
                ToImColor(color, alphaScale));
            return;
        }

        vector2 tip = AddVector(end, ScaleVector(unit, ArrowPixels));
        vector2 base = AddVector(end, ScaleVector(normal, ArrowWidthPixels * 0.5f));
        vector2 baseOther = SubtractVector(end, ScaleVector(normal, ArrowWidthPixels * 0.5f));
        drawList->AddTriangleFilled(ToImVec2(tip), ToImVec2(base), ToImVec2(baseOther), ToImColor(color, alphaScale));
    };

    //平面块（仅位移模式）
    if (mode == EditorGizmoMode::Move)
    {
        const int32 planeNormals[3] = { 2, 0, 1 };
        for (int32 planeIndex = 0; planeIndex < 3; ++planeIndex)
        {
            int32 normalIndex = planeNormals[planeIndex];
            EditorGizmoHandle handle = static_cast<EditorGizmoHandle>(static_cast<int32>(EditorGizmoHandle::PlaneXY) + planeIndex);
            vector3 normal = context.axes[normalIndex];
            if (std::abs(RenderMath::Dot(normal, view.cameraForward)) > 0.999f) continue;

            const vector3& axisA = context.axes[(normalIndex + 1) % 3];
            const vector3& axisB = context.axes[(normalIndex + 2) % 3];
            float32 offset = PlaneOffsetPixels * context.worldPerPixel;
            float32 size = PlaneSizePixels * context.worldPerPixel;
            const float32 offsets[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
            ImVec2 corners[4];
            bool visible = true;
            for (int32 corner = 0; corner < 4 && visible; ++corner)
            {
                vector3 point = AddVector(context.pivot, AddVector(
                    ScaleVector(axisA, offset + size * offsets[corner][0]),
                    ScaleVector(axisB, offset + size * offsets[corner][1])));
                vector2 screen;
                visible = ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, point, screen);
                corners[corner] = ToImVec2(screen);
            }
            if (!visible) continue;

            EditorGizmoColor color = effective == handle ? HotColor : AxisColors[normalIndex];
            drawList->AddConvexPolyFilled(corners, 4, ToImColor(color, 0.28f));
            drawList->AddPolyline(corners, 4, ToImColor(color, 0.85f), ImDrawFlags_Closed, AxisThickness);
        }
    }

    //三轴与圆环
    if (mode == EditorGizmoMode::Rotate)
    {
        quaternion pivotRotation = context.pivotRotation;
        matrix4x4 rotationMatrix = RenderMath::Rotation(pivotRotation);
        ImVec2 outerPoints[37];
        for (int32 segment = 0; segment <= 36; ++segment)
        {
            float32 angle = 6.28318530718f * static_cast<float32>(segment) / 36.0f;
            outerPoints[segment] = ImVec2(
                pivotScreen.x + std::cos(angle) * OuterRingPixels,
                pivotScreen.y + std::sin(angle) * OuterRingPixels);
        }
        drawList->AddPolyline(outerPoints, 37,
            ToImColor(effective == EditorGizmoHandle::Center ? HotColor : CenterColor), ImDrawFlags_None, AxisThickness);

        for (int32 axisIndex = 0; axisIndex < 3; ++axisIndex)
        {
            EditorGizmoHandle handle = static_cast<EditorGizmoHandle>(static_cast<int32>(EditorGizmoHandle::AxisX) + axisIndex);
            EditorGizmoColor color = effective == handle ? HotColor : AxisColors[axisIndex];
            vector3 u = RenderMath::TransformDirection(rotationMatrix, BaseAxes[axisIndex]);
            vector3 v = RenderMath::TransformDirection(rotationMatrix, BaseAxes[(axisIndex + 1) % 3]);

            ImVec2 points[RingSegments + 1];
            int32 count = 0;
            for (int32 segment = 0; segment <= RingSegments; ++segment)
            {
                float32 angle = 6.28318530718f * static_cast<float32>(segment) / static_cast<float32>(RingSegments);
                vector3 point = AddVector(context.pivot, ScaleVector(AddVector(
                    ScaleVector(u, std::cos(angle)), ScaleVector(v, std::sin(angle))), RingPixels * context.worldPerPixel));
                vector2 screen;
                if (!ProjectPoint(view.viewProjection, view.renderPosition, view.renderSize, point, screen))
                {
                    if (count > 1) drawList->AddPolyline(points, count, ToImColor(color), ImDrawFlags_None, AxisThickness);
                    count = 0;
                    continue;
                }
                points[count++] = ToImVec2(screen);
            }
            if (count > 1) drawList->AddPolyline(points, count, ToImColor(color), ImDrawFlags_None, AxisThickness);
        }
    }
    else
    {
        for (int32 axisIndex = 0; axisIndex < 3; ++axisIndex)
        {
            int32 order[3] = { 0, 1, 2 };
            //先画背对相机的轴，正面的压在更上层
            vector3 axis = mode == EditorGizmoMode::Scale
                ? RenderMath::TransformDirection(RenderMath::Rotation(context.pivotRotation), BaseAxes[axisIndex])
                : context.axes[axisIndex];
            order[axisIndex] = RenderMath::Dot(axis, view.cameraForward) > 0.0f ? 1 : 0;
            (void)order;
        }
        for (int32 axisIndex = 0; axisIndex < 3; ++axisIndex) drawAxis(axisIndex);
    }

    //中心方块最后画，占用中优先显示
    if (mode != EditorGizmoMode::Rotate)
    {
        float32 half = CenterHalfPixels;
        EditorGizmoColor color = effective == EditorGizmoHandle::Center ? HotColor : CenterColor;
        drawList->AddRectFilled(
            ImVec2(pivotScreen.x - half, pivotScreen.y - half),
            ImVec2(pivotScreen.x + half, pivotScreen.y + half),
            ToImColor(color));

        //位移的中心方块在拖拽中补一条辅助平面提示
        if (mode == EditorGizmoMode::Move && dragActive && active == EditorGizmoHandle::Center)
        {
            drawList->AddRect(
                ImVec2(startPivotScreen.x - CenterHalfPixels * 3.0f, startPivotScreen.y - CenterHalfPixels * 3.0f),
                ImVec2(startPivotScreen.x + CenterHalfPixels * 3.0f, startPivotScreen.y + CenterHalfPixels * 3.0f),
                ToImColor(CenterColor, 0.5f));
        }
    }
}
