#include "Editor/EditorGizmoBridge.h"

#include "Runtime/Native/NativeCall.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
    EditorGizmoBridge* CurrentBridge = nullptr;

    // 把 C# UTF-8 字节读取为临时字符串。
    std::string ReadUtf8Text(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return std::string();
        return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(length));
    }

    // 把线性颜色转换为 ImGui 颜色。
    ImU32 ToImColor(const EditorGizmoColor& color)
    {
        float32 r = std::clamp(color.r, 0.0f, 1.0f);
        float32 g = std::clamp(color.g, 0.0f, 1.0f);
        float32 b = std::clamp(color.b, 0.0f, 1.0f);
        float32 a = std::clamp(color.a, 0.0f, 1.0f);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
    }

    // 把三维点投影到屏幕坐标。
    bool ProjectPoint(const matrix4x4& viewProjection, int32 width, int32 height, const EditorGizmoVector3& point, ImVec2& screen)
    {
        if (width <= 0 || height <= 0) return false;

        float32 x = viewProjection.m[0] * point.x + viewProjection.m[4] * point.y + viewProjection.m[8] * point.z + viewProjection.m[12];
        float32 y = viewProjection.m[1] * point.x + viewProjection.m[5] * point.y + viewProjection.m[9] * point.z + viewProjection.m[13];
        float32 z = viewProjection.m[2] * point.x + viewProjection.m[6] * point.y + viewProjection.m[10] * point.z + viewProjection.m[14];
        float32 w = viewProjection.m[3] * point.x + viewProjection.m[7] * point.y + viewProjection.m[11] * point.z + viewProjection.m[15];
        if (std::abs(w) <= 0.000001f || w < 0.0f) return false;

        float32 invW = 1.0f / w;
        float32 ndcX = x * invW;
        float32 ndcY = y * invW;
        float32 ndcZ = z * invW;
        if (ndcZ < -1.0f || ndcZ > 1.0f) return false;

        screen.x = (ndcX * 0.5f + 0.5f) * static_cast<float32>(width);
        screen.y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float32>(height);
        return true;
    }

    // 绘制三维线段。
    void ORBEDEN_NATIVE_CALL EditorGizmoLine3D(EditorGizmoVector3 a, EditorGizmoVector3 b, EditorGizmoColor color)
    {
        if (!CurrentBridge) return;

        ImVec2 screenA;
        ImVec2 screenB;
        if (!ProjectPoint(CurrentBridge->GetViewProjection(), CurrentBridge->GetViewportWidth(), CurrentBridge->GetViewportHeight(), a, screenA)) return;
        if (!ProjectPoint(CurrentBridge->GetViewProjection(), CurrentBridge->GetViewportWidth(), CurrentBridge->GetViewportHeight(), b, screenB)) return;

        ImGui::GetBackgroundDrawList()->AddLine(screenA, screenB, ToImColor(color), 2.0f);
    }

    // 绘制三维文本标签。
    void ORBEDEN_NATIVE_CALL EditorGizmoLabel3D(EditorGizmoVector3 position, const uint8* text, int32 length)
    {
        if (!CurrentBridge) return;

        ImVec2 screen;
        if (!ProjectPoint(CurrentBridge->GetViewProjection(), CurrentBridge->GetViewportWidth(), CurrentBridge->GetViewportHeight(), position, screen)) return;

        std::string value = ReadUtf8Text(text, length);
        ImGui::GetBackgroundDrawList()->AddText(screen, IM_COL32(255, 245, 180, 255), value.c_str());
    }
}

EditorGizmoApi EditorGizmoBridge::GetApi()
{
    EditorGizmoApi api;
    api.Line3D = reinterpret_cast<void*>(&EditorGizmoLine3D);
    api.Label3D = reinterpret_cast<void*>(&EditorGizmoLabel3D);
    return api;
}

const matrix4x4& EditorGizmoBridge::GetViewProjection() const
{
    return viewProjection;
}

int32 EditorGizmoBridge::GetViewportWidth() const
{
    return viewportWidth;
}

int32 EditorGizmoBridge::GetViewportHeight() const
{
    return viewportHeight;
}

void EditorGizmoBridge::BeginFrame(const matrix4x4& newViewProjection, int32 width, int32 height)
{
    viewProjection = newViewProjection;
    viewportWidth = width;
    viewportHeight = height;
    CurrentBridge = this;
}

void EditorGizmoBridge::EndFrame()
{
    if (CurrentBridge == this)
    {
        CurrentBridge = nullptr;
    }
}
