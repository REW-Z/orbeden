#include "Editor/EditorGUI.h"

#include "Editor/EditorIcons.h"
#include "Editor/EditorScene.h"
#include "Log/Log.h"
#include "Application.h"
#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include <filesystem>
#include "Platform/GlfwWindow.h"
#include "Runtime/Native/NativeCall.h"

#include <glad/gl.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cstring>
#include <string>

EditorGUI* EditorGUI::activeInstance = nullptr;

namespace
{
    //把 0xRRGGBB 展开成引擎颜色，与托管侧的主题字面量一一对应
    constexpr color FromHex(uint32 rgb, float32 alpha = 1.0f)
    {
        return color {
            static_cast<float32>((rgb >> 16) & 0xFFu) / 255.0f,
            static_cast<float32>((rgb >> 8) & 0xFFu) / 255.0f,
            static_cast<float32>(rgb & 0xFFu) / 255.0f,
            alpha
        };
    }

    //转换为 ImGui 颜色
    ImVec4 ToImVec4(const color& value)
    {
        return ImVec4(value.r, value.g, value.b, value.a);
    }

    //按可用宽度截断 UTF-8 文本，超宽时在尾部补省略号
    std::string EllipsizeToWidth(const std::string& text, float32 maxWidth)
    {
        if (maxWidth <= 0.0f) return std::string();
        if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;

        //按字符逐个累加宽度，遇到放不下的字符就停，避免整串反复测量
        const float32 ellipsisWidth = ImGui::CalcTextSize("...").x;
        float32 width = 0.0f;
        usize length = 0;
        while (length < text.size())
        {
            usize next = length + 1;
            //UTF-8 续字节不单独成字，连同首字节一起取
            while (next < text.size() && (static_cast<uint8>(text[next]) & 0xC0) == 0x80) ++next;
            float32 characterWidth = ImGui::CalcTextSize(text.c_str() + length, text.c_str() + next).x;
            if (width + characterWidth + ellipsisWidth > maxWidth) break;
            width += characterWidth;
            length = next;
        }
        return text.substr(0, length) + "...";
    }

    //在名称前绘制图标并停在同一行，没有图标时不占位
    void DrawInlineIcon(const std::string& name, float32 size)
    {
        ImTextureID texture = EditorIcons::Get(name);
        if (texture == 0) return;

        ImGui::Image(texture, ImVec2(size, size));
        ImGui::SameLine();
    }

    struct EditorThemeData
    {
        color background = FromHex(0x242424), text = FromHex(0xE8E8E8), border = FromHex(0x505050);
        color header = FromHex(0x383838), control = FromHex(0x505050);
        color hovered = FromHex(0x385676), active = FromHex(0x9184EE);
        float32 paddingX = 6, paddingY = 6, spacingX = 6, spacingY = 4;
        float32 framePaddingX = 6, framePaddingY = 4, splitterSize = 5, cornerRadius = 6;
    };
    static_assert(sizeof(EditorThemeData) == 144);
    EditorThemeData theme;

    //接收托管主题参数
    void ORBEDEN_NATIVE_CALL EditorGuiSetTheme(const EditorThemeData* value)
    {
        if (value) theme = *value;
    }

    struct EditorDragPayload
    {
        int32 kind = 0;
        std::string key;
        std::string contentRoot;
        uint64 worldRevision = 0;
        int32 sourceObjectId = 0;
    };
    EditorDragPayload dragPayload;

    //参与跨窗口拖拽与左键状态判断的编辑器窗口
    List<GLFWwindow*> dragWindows;

    //主上下文持有的字体图集，供独立窗口的 ImGui 上下文共享
    ImFontAtlas* mainFontAtlas = nullptr;

    //验证源对象与 World 会话仍然有效
    bool HasValidDrag()
    {
        Application* app = Application::Current();
        if (!app || dragPayload.kind == 0 || dragPayload.worldRevision != app->GetWorldRevision()
            || dragPayload.contentRoot != PathDefines::GetContentRoot()) return false;
        if (dragPayload.kind == 1)
        {
            Object* source = Object::FindObjectById(dragPayload.sourceObjectId);
            return source && source->GetWorld() == &app->GetWorld() && source->GetInstanceId().GetPath() == dragPayload.key;
        }
        std::error_code error;
        return std::filesystem::exists(Utf8Path::FromUtf8(dragPayload.contentRoot) / Utf8Path::FromUtf8(dragPayload.key), error);
    }

    //从当前 GUI 项开始资源拖动
    void ORBEDEN_NATIVE_CALL EditorGuiDragSource(int32 kind, const uint8* key, int32 length)
    {
        if (!ImGui::BeginDragDropSource()) return;
        EditorGUI::SetDragPayload(kind, std::string(reinterpret_cast<const char*>(key), static_cast<usize>(length)));
        ImGui::SetDragDropPayload("EditorShared", &kind, sizeof(kind));
        ImGui::TextUnformatted(dragPayload.key.c_str());
        ImGui::EndDragDropSource();
    }

    //读取悬停目标上的共享拖拽载荷
    int32 ORBEDEN_NATIVE_CALL EditorGuiReadDrag(int32* kind, uint8* buffer, int32 capacity)
    {
        if (kind) *kind = 0;
        if (!HasValidDrag() || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) return 0;
        if (kind) *kind = dragPayload.kind;
        int32 count = static_cast<int32>(dragPayload.key.size());
        if (buffer && capacity >= count) std::memcpy(buffer, dragPayload.key.data(), count);
        return count;
    }

    //绘制接收预览并仅在鼠标释放时提交
    uint8 ORBEDEN_NATIVE_CALL EditorGuiAcceptDrag(uint8 valid, int32 placement)
    {
        if (!HasValidDrag() || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) return 0;
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImU32 color = valid ? IM_COL32(75, 180, 255, 255) : IM_COL32(220, 70, 70, 255);
        if (placement == 0) ImGui::GetWindowDrawList()->AddRect(min, max, color, 2.0f, 0, 2.0f);
        else
        {
            float32 y = placement < 0 ? min.y : max.y;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), color, 2.0f);
        }
        if (!valid || EditorGUI::IsLeftMouseDownAnywhere()) return 0;
        dragPayload = {};
        return 1;
    }


    //创建面板专属内容区并在换宿主后恢复滚动
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginPanelContent(const uint8* id, int32 length,
        uint64* host, const vector2* scroll, uint8 restoreScroll)
    {
        std::string name(reinterpret_cast<const char*>(id), static_cast<usize>(length));
        uint64 currentHost = (static_cast<uint64>(ImGui::GetID(name.c_str())) << 32)
            ^ reinterpret_cast<uint64>(ImGui::GetCurrentContext());
        if (*host != currentHost || restoreScroll) ImGui::SetNextWindowScroll(ImVec2(scroll->x, scroll->y));
        *host = currentHost;
        //内容区不铺背景也不自带边距，底色与内边距由停靠叶子或浮窗统一给出
        return ImGui::BeginChild(name.c_str(), ImVec2(0, 0),
            ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground) ? 1 : 0;
    }

    //记录面板滚动位置并结束内容区域
    void ORBEDEN_NATIVE_CALL EditorGuiEndPanelContent(vector2* scroll)
    {
        *scroll = { ImGui::GetScrollX(), ImGui::GetScrollY() };
        ImGui::EndChild();
    }

    //提交占满剩余区域的空白投放项
    int32 ORBEDEN_NATIVE_CALL EditorGuiFillRemainingArea()
    {
        ImVec2 size = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##panel_empty_area", ImVec2(std::max(size.x, 1.0f), std::max(size.y, 28.0f)));
        return (ImGui::IsItemClicked() ? 1 : 0) | (ImGui::GetIO().KeyCtrl ? 2 : 0);
    }

    //按鼠标在节点内的高度确定前后或子级投放
    int32 ORBEDEN_NATIVE_CALL EditorGuiGetDropPlacement()
    {
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        float32 ratio = (ImGui::GetIO().MousePos.y - min.y) / std::max(1.0f, max.y - min.y);
        return ratio < 0.25f ? -1 : ratio > 0.75f ? 1 : 0;
    }

    //读取 UTF-8 文本
    std::string ReadUtf8Text(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return std::string();
        return std::string(reinterpret_cast<const char*>(text), static_cast<usize>(length));
    }

    //开始可独立滚动的内容区域
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginChild(const uint8* id, int32 length, float32* width, float32 height, uint8 resizable)
    {
        ImGuiChildFlags flags = ImGuiChildFlags_Borders;
        if (resizable) flags |= ImGuiChildFlags_ResizeX;
        bool visible = ImGui::BeginChild(ReadUtf8Text(id, length).c_str(), ImVec2(*width, height), flags);
        *width = ImGui::GetWindowSize().x;
        return visible ? 1 : 0;
    }

    //结束内容区域
    void ORBEDEN_NATIVE_CALL EditorGuiEndChild() { ImGui::EndChild(); }

    //绘制目录节点并返回展开与点击状态
    int32 ORBEDEN_NATIVE_CALL EditorGuiTreeNode(const uint8* label, int32 length, uint8 options)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (options & 1) flags |= ImGuiTreeNodeFlags_Selected;
        if (options & 2) flags |= ImGuiTreeNodeFlags_Leaf;
        if (options & 4) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        bool expanded = ImGui::TreeNodeEx(ReadUtf8Text(label, length).c_str(), flags);
        return (expanded ? 1 : 0) | (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() ? 2 : 0) | (ImGui::GetIO().KeyCtrl ? 4 : 0);
    }

    //结束目录节点
    void ORBEDEN_NATIVE_CALL EditorGuiTreePop() { ImGui::TreePop(); }

    //打开确认弹窗
    void ORBEDEN_NATIVE_CALL EditorGuiOpenPopup(const uint8* id, int32 length)
    {
        ImGui::OpenPopup(ReadUtf8Text(id, length).c_str());
    }

    //开始模态确认弹窗
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginPopup(const uint8* id, int32 length)
    {
        return ImGui::BeginPopupModal(ReadUtf8Text(id, length).c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize) ? 1 : 0;
    }

    //关闭当前弹窗
    void ORBEDEN_NATIVE_CALL EditorGuiClosePopup() { ImGui::CloseCurrentPopup(); }

    //绘制文本标签
    void ORBEDEN_NATIVE_CALL EditorGuiLabel(const uint8* text, int32 length)
    {
        const char* begin = text && length > 0 ? reinterpret_cast<const char*>(text) : "";
        const char* end = begin + std::max(length, 0);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(begin, end);
        ImGui::PopTextWrapPos();
    }

    //绘制按钮
    uint8 ORBEDEN_NATIVE_CALL EditorGuiButton(const uint8* text, int32 length)
    {
        std::string value = ReadUtf8Text(text, length);
        return ImGui::Button(value.c_str()) ? 1 : 0;
    }

    //开始组件块
    void ORBEDEN_NATIVE_CALL EditorGuiBeginComponentBlock(const uint8* icon, int32 iconLength,
        const uint8* title, int32 length)
    {
        std::string iconName = ReadUtf8Text(icon, iconLength);
        std::string value = ReadUtf8Text(title, length);
        if (value.empty()) value = "Component";

        ImGui::Spacing();
        ImGui::PushID(value.c_str());
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        //组件块用表面色铺底，不能借用控件填充色，否则块内控件看不出来
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_Header));
        ImGui::BeginChild("##component",
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawInlineIcon(iconName, ImGui::GetFrameHeight());
        ImGui::TextUnformatted(value.c_str());
        ImGui::Separator();
    }

    //结束组件块
    void ORBEDEN_NATIVE_CALL EditorGuiEndComponentBlock()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::PopID();
        ImGui::Spacing();
    }

    //开始可折叠组件块
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginCollapsibleComponentBlock(const uint8* icon,
        int32 iconLength,
        const uint8* title,
        int32 titleLength,
        const uint8* id,
        int32 idLength,
        uint8 removable,
        uint8* removeRequested)
    {
        std::string iconName = ReadUtf8Text(icon, iconLength);
        std::string value = ReadUtf8Text(title, titleLength);
        std::string identity = ReadUtf8Text(id, idLength);
        if (value.empty()) value = "Component";
        if (identity.empty()) identity = value;
        if (removeRequested) *removeRequested = 0;

        ImGui::Spacing();
        ImGui::PushID(identity.c_str());
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        //组件块用表面色铺底，不能借用控件填充色，否则块内控件看不出来
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_Header));
        ImGui::BeginChild("##component",
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        bool visible = true;
        DrawInlineIcon(iconName, ImGui::GetFrameHeight());
        bool expanded = removable != 0
            ? ImGui::CollapsingHeader(value.c_str(), &visible, flags)
            : ImGui::CollapsingHeader(value.c_str(), flags);
        if (removeRequested && !visible) *removeRequested = 1;
        if (expanded) ImGui::Separator();
        return expanded ? 1 : 0;
    }

    //开始下拉选择框
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginCombo(const uint8* label,
        int32 labelLength,
        const uint8* preview,
        int32 previewLength)
    {
        std::string labelText = ReadUtf8Text(label, labelLength);
        std::string previewText = ReadUtf8Text(preview, previewLength);
        return ImGui::BeginCombo(labelText.c_str(), previewText.c_str()) ? 1 : 0;
    }

    //结束下拉选择框
    void ORBEDEN_NATIVE_CALL EditorGuiEndCombo()
    {
        ImGui::EndCombo();
    }

    //绘制选择项
    uint8 ORBEDEN_NATIVE_CALL EditorGuiSelectable(const uint8* label, int32 length, uint8 selected)
    {
        std::string value = ReadUtf8Text(label, length);
        return ImGui::Selectable(value.c_str(), selected != 0) ? 1 : 0;
    }

    //绘制布尔输入框
    uint8 ORBEDEN_NATIVE_CALL EditorGuiCheckbox(const uint8* label, int32 length, uint8* value)
    {
        if (!value) return 0;

        bool boolValue = *value != 0;
        std::string text = ReadUtf8Text(label, length);
        bool changed = ImGui::Checkbox(text.c_str(), &boolValue);
        *value = boolValue ? 1 : 0;
        return changed ? 1 : 0;
    }

    //绘制整数输入框
    uint8 ORBEDEN_NATIVE_CALL EditorGuiInputInt(const uint8* label, int32 length, int32* value)
    {
        if (!value) return 0;
        std::string text = ReadUtf8Text(label, length);
        return ImGui::InputInt(text.c_str(), value) ? 1 : 0;
    }

    //绘制浮点输入框
    uint8 ORBEDEN_NATIVE_CALL EditorGuiInputFloat(const uint8* label, int32 length, float32* value)
    {
        if (!value) return 0;
        std::string text = ReadUtf8Text(label, length);
        return ImGui::InputFloat(text.c_str(), value) ? 1 : 0;
    }

    //绘制三维向量输入框
    uint8 ORBEDEN_NATIVE_CALL EditorGuiInputVector3(const uint8* label, int32 length, vector3* value)
    {
        if (!value) return 0;

        std::string text = ReadUtf8Text(label, length);
        float32 values[3] = { value->x, value->y, value->z };
        bool changed = ImGui::InputFloat3(text.c_str(), values);
        if (changed)
        {
            value->x = values[0];
            value->y = values[1];
            value->z = values[2];
        }
        return changed ? 1 : 0;
    }

    //绘制字符串输入框
    int32 ORBEDEN_NATIVE_CALL EditorGuiInputText(const uint8* label, int32 length, uint8* buffer, int32 bufferSize)
    {
        if (!buffer || bufferSize <= 0) return -1;

        std::string text = ReadUtf8Text(label, length);
        buffer[bufferSize - 1] = 0;
        bool changed = ImGui::InputText(text.c_str(), reinterpret_cast<char*>(buffer), static_cast<usize>(bufferSize));
        return changed ? static_cast<int32>(std::strlen(reinterpret_cast<const char*>(buffer))) : -1;
    }

    //绘制对象引用框并返回操作：0 无 1 点击引用框 2 清空 3 打开选择器
    int32 ORBEDEN_NATIVE_CALL EditorGuiReferenceField(const uint8* icon, int32 iconLength,
        const uint8* text, int32 textLength, const uint8* id, int32 idLength)
    {
        std::string iconName = ReadUtf8Text(icon, iconLength);
        std::string label = ReadUtf8Text(text, textLength);
        std::string identity = ReadUtf8Text(id, idLength);

        ImGuiStyle& style = ImGui::GetStyle();
        float32 height = ImGui::GetFrameHeight();
        ImVec2 lineStart = ImGui::GetCursorScreenPos();
        //清空与选择器两个方按钮先占位，引用框只取剩余宽度，与输入框一样贴满卡片
        float32 reserved = 2.0f * (height + style.ItemSpacing.x);
        float32 width = std::max(ImGui::GetContentRegionAvail().x - reserved, height);

        ImGui::PushID(identity.c_str());
        //两个方按钮先提交，引用框最后提交：跨面板拖拽以本行最后一个条目作为投放目标
        ImGui::SetCursorScreenPos(ImVec2(lineStart.x + width + style.ItemSpacing.x, lineStart.y));
        int32 action = 0;
        if (ImGui::Button("×", ImVec2(height, height))) action = 2;
        ImGui::SameLine();
        if (ImGui::Button("...", ImVec2(height, height))) action = 3;

        ImGui::SetCursorScreenPos(lineStart);
        ImVec2 min = lineStart;
        ImVec2 max { min.x + width, min.y + height };
        bool clicked = ImGui::InvisibleButton("##reference", ImVec2(width, height));
        bool hovered = ImGui::IsItemHovered();
        bool held = ImGui::IsItemActive();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(held ? ImGuiCol_FrameBgActive
            : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), style.FrameRounding);
        if (style.FrameBorderSize > 0.0f)
            drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding);

        //图标与文本都按行高居中，文本超出引用框时只画放得下的部分
        float32 lineHeight = ImGui::GetTextLineHeight();
        float32 textLeft = min.x + style.FramePadding.x;
        ImTextureID texture = EditorIcons::Get(iconName);
        if (texture != 0)
        {
            float32 iconTop = min.y + (height - lineHeight) * 0.5f;
            drawList->AddImage(texture, ImVec2(textLeft, iconTop), ImVec2(textLeft + lineHeight, iconTop + lineHeight));
            textLeft += lineHeight + style.ItemInnerSpacing.x;
        }
        drawList->PushClipRect(min, max, true);
        drawList->AddText(ImVec2(textLeft, min.y + (height - lineHeight) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
        drawList->PopClipRect();

        if (clicked) action = 1;
        ImGui::PopID();
        return action;
    }

    //绘制资源瓦片并返回点击状态：图标在上、名称在下，整块作为一个条目
    uint8 ORBEDEN_NATIVE_CALL EditorGuiAssetTile(const uint8* icon, int32 iconLength,
        const uint8* label, int32 labelLength, const uint8* id, int32 idLength,
        float32 width, uint8 selected)
    {
        std::string iconName = ReadUtf8Text(icon, iconLength);
        std::string text = ReadUtf8Text(label, labelLength);
        std::string identity = ReadUtf8Text(id, idLength);

        ImGuiStyle& style = ImGui::GetStyle();
        //图标取原生 32px 一档，缩放后正好接近资源本身的尺寸
        float32 iconSize = ImGui::GetFrameHeight() * 1.5f;
        float32 height = style.FramePadding.y * 2.0f + iconSize + style.ItemSpacing.y + ImGui::GetTextLineHeight();

        ImGui::PushID(identity.c_str());
        ImVec2 min = ImGui::GetCursorScreenPos();
        ImVec2 max { min.x + width, min.y + height };
        bool clicked = ImGui::InvisibleButton("##asset_tile", ImVec2(width, height));
        bool hovered = ImGui::IsItemHovered();
        bool held = ImGui::IsItemActive();

        //选中与悬停共用一块表面色底，选中额外压一圈强调色边框
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (selected || hovered || held)
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered),
                style.FrameRounding);
        if (selected)
            drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_ButtonActive), style.FrameRounding);

        ImTextureID texture = EditorIcons::Get(iconName);
        if (texture != 0)
        {
            float32 iconLeft = min.x + (width - iconSize) * 0.5f;
            float32 iconTop = min.y + style.FramePadding.y;
            drawList->AddImage(texture, ImVec2(iconLeft, iconTop), ImVec2(iconLeft + iconSize, iconTop + iconSize));
        }

        //名称居中，放不下的名字截断补省略号，悬停时用提示给出全名
        float32 textTop = min.y + style.FramePadding.y + iconSize + style.ItemSpacing.y;
        float32 available = width - style.FramePadding.x * 2.0f;
        std::string display = EllipsizeToWidth(text, available);
        float32 textWidth = ImGui::CalcTextSize(display.c_str()).x;
        float32 textLeft = min.x + style.FramePadding.x + std::max(0.0f, (available - textWidth) * 0.5f);
        drawList->PushClipRect(min, max, true);
        drawList->AddText(ImVec2(textLeft, textTop), ImGui::GetColorU32(ImGuiCol_Text), display.c_str());
        drawList->PopClipRect();
        if (hovered && display != text) ImGui::SetTooltip("%s", text.c_str());

        ImGui::PopID();
        return clicked ? 1 : 0;
    }

    //绘制视图切换按钮：按钮上是当前模式的图标，点击切到另一种
    uint8 ORBEDEN_NATIVE_CALL EditorGuiViewToggleButton(const uint8* id, int32 length, uint8 gridMode)
    {
        std::string identity = ReadUtf8Text(id, length);
        float32 size = ImGui::GetFrameHeight();
        bool clicked = ImGui::Button(identity.c_str(), ImVec2(size, size));

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        float32 side = std::min(max.x - min.x, max.y - min.y);
        ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);

        //按钮不写字，用图形表示模式：四宫格是网格，三条带行首块的是列表
        if (gridMode != 0)
        {
            float32 cell = side * 0.24f;
            float32 gap = side * 0.12f;
            for (int32 row = 0; row < 2; row++)
            {
                for (int32 column = 0; column < 2; column++)
                {
                    ImVec2 cellMin(center.x - cell - gap * 0.5f + static_cast<float32>(column) * (cell + gap),
                        center.y - cell - gap * 0.5f + static_cast<float32>(row) * (cell + gap));
                    drawList->AddRectFilled(cellMin, ImVec2(cellMin.x + cell, cellMin.y + cell), color, 1.0f);
                }
            }
        }
        else
        {
            float32 rowHeight = side * 0.14f;
            float32 rowGap = side * 0.1f;
            float32 block = side * 0.14f;
            float32 bar = side * 0.42f;
            float32 left = center.x - (block + rowGap + bar) * 0.5f;
            float32 top = center.y - (rowHeight * 3.0f + rowGap * 2.0f) * 0.5f;
            for (int32 row = 0; row < 3; row++)
            {
                float32 y = top + static_cast<float32>(row) * (rowHeight + rowGap);
                drawList->AddRectFilled(ImVec2(left, y), ImVec2(left + block, y + rowHeight), color, 1.0f);
                drawList->AddRectFilled(ImVec2(left + block + rowGap, y), ImVec2(left + block + rowGap + bar, y + rowHeight), color, 1.0f);
            }
        }

        if (ImGui::IsItemHovered()) ImGui::SetTooltip(gridMode != 0 ? "Switch to list" : "Switch to grid");
        return clicked ? 1 : 0;
    }

    //绘制分隔线
    void ORBEDEN_NATIVE_CALL EditorGuiSeparator()
    {
        ImGui::Separator();
    }

    //切换到同行布局
    void ORBEDEN_NATIVE_CALL EditorGuiSameLine()
    {
        ImGui::SameLine();
    }

    //开始表格
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginTable(const uint8* id, int32 length, int32 columns)
    {
        if (columns <= 0) return 0;

        std::string value = ReadUtf8Text(id, length);
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_Resizable
            | ImGuiTableFlags_SizingStretchProp
            | ImGuiTableFlags_ScrollY;
        return ImGui::BeginTable(value.empty() ? "##editor_table" : value.c_str(),
            columns,
            flags,
            ImVec2(0.0f, 0.0f)) ? 1 : 0;
    }

    //结束表格
    void ORBEDEN_NATIVE_CALL EditorGuiEndTable()
    {
        ImGui::EndTable();
    }

    //配置表格列
    void ORBEDEN_NATIVE_CALL EditorGuiTableSetupColumn(const uint8* label, int32 length, float32 width, uint8 fixedWidth)
    {
        std::string value = ReadUtf8Text(label, length);
        ImGuiTableColumnFlags flags = fixedWidth != 0
            ? ImGuiTableColumnFlags_WidthFixed
            : ImGuiTableColumnFlags_WidthStretch;
        ImGui::TableSetupColumn(value.c_str(), flags, width);
    }

    //绘制表头
    void ORBEDEN_NATIVE_CALL EditorGuiTableHeadersRow()
    {
        ImGui::TableHeadersRow();
    }

    //前进到下一表格行
    void ORBEDEN_NATIVE_CALL EditorGuiTableNextRow()
    {
        ImGui::TableNextRow();
    }

    //切换当前表格列
    void ORBEDEN_NATIVE_CALL EditorGuiTableSetColumnIndex(int32 column)
    {
        ImGui::TableSetColumnIndex(column);
    }

    //绘制表格选择项
    uint8 ORBEDEN_NATIVE_CALL EditorGuiTableSelectable(const uint8* label,
        int32 length,
        uint8 selected,
        uint8 spanAllColumns)
    {
        std::string value = ReadUtf8Text(label, length);
        ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
        if (spanAllColumns != 0) flags |= ImGuiSelectableFlags_SpanAllColumns;
        return ImGui::Selectable(value.c_str(), selected != 0, flags) ? 1 : 0;
    }

    //判断控件双击
    uint8 ORBEDEN_NATIVE_CALL EditorGuiIsItemDoubleClicked()
    {
        return ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) ? 1 : 0;
    }

    //开始控件右键菜单
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginPopupContextItem(const uint8* id, int32 length)
    {
        std::string value = ReadUtf8Text(id, length);
        return ImGui::BeginPopupContextItem(value.empty() ? nullptr : value.c_str()) ? 1 : 0;
    }

    //开始窗口右键菜单
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginPopupContextWindow(const uint8* id, int32 length)
    {
        std::string value = ReadUtf8Text(id, length);
        constexpr ImGuiPopupFlags flags = ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems;
        return ImGui::BeginPopupContextWindow(value.empty() ? nullptr : value.c_str(), flags) ? 1 : 0;
    }

    //结束右键菜单
    void ORBEDEN_NATIVE_CALL EditorGuiEndPopup()
    {
        ImGui::EndPopup();
    }

    //绘制菜单项
    uint8 ORBEDEN_NATIVE_CALL EditorGuiMenuItem(const uint8* label, int32 length, uint8 enabled)
    {
        std::string value = ReadUtf8Text(label, length);
        return ImGui::MenuItem(value.c_str(), nullptr, false, enabled != 0) ? 1 : 0;
    }

    //写入剪贴板文本
    void ORBEDEN_NATIVE_CALL EditorGuiSetClipboardText(const uint8* text, int32 length)
    {
        std::string value = ReadUtf8Text(text, length);
        ImGui::SetClipboardText(value.c_str());
    }

    //开始禁用控件区域
    void ORBEDEN_NATIVE_CALL EditorGuiBeginDisabled(uint8 disabled)
    {
        ImGui::BeginDisabled(disabled != 0);
    }

    //结束禁用控件区域
    void ORBEDEN_NATIVE_CALL EditorGuiEndDisabled()
    {
        ImGui::EndDisabled();
    }

    //绘制 Scene 面板的原生视口，提交离屏图像并叠加轮廓与 Handles
    void ORBEDEN_NATIVE_CALL EditorGuiDrawSceneView()
    {
        EditorScene* scene = EditorScene::GetActiveScene();
        if (scene) scene->DrawSceneView();
    }

    //解析场景视口当前鼠标位置的投放点
    uint8 ORBEDEN_NATIVE_CALL EditorGuiResolveSceneDropPosition(EditorGizmoVector3* position)
    {
        EditorScene* scene = EditorScene::GetActiveScene();
        if (!scene || !position) return 0;

        vector3 resolved;
        if (!scene->ResolveSceneDropPosition(resolved)) return 0;
        position->x = resolved.x;
        position->y = resolved.y;
        position->z = resolved.z;
        return 1;
    }
}

bool EditorGUI::Initialize(IWindow* editorWindow)
{
    if (initialized) return true;

    GlfwWindow* platformWindow = dynamic_cast<GlfwWindow*>(editorWindow);
    if (!platformWindow || editorWindow->GetGraphicsApi() != WindowGraphicsApi::OpenGL
        || !platformWindow->GetGlfwWindow())
    {
        Log::Error("EditorGUI initialize failed: OpenGL GLFW window is missing.");
        return false;
    }

    window = editorWindow;
    glfwWindow = platformWindow->GetGlfwWindow();

    IMGUI_CHECKVERSION();
    context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendPlatformName = "Orbeden_EditorGUI_GLFW";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos;
    io.SetClipboardTextFn = [](void* userData, const char* text)
    {
        glfwSetClipboardString(static_cast<GLFWwindow*>(userData), text);
    };
    io.GetClipboardTextFn = [](void* userData)
    {
        return glfwGetClipboardString(static_cast<GLFWwindow*>(userData));
    };
    io.ClipboardUserData = glfwWindow;
    ImGui::GetMainViewport()->PlatformHandle = glfwWindow;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplOpenGL3_Init("#version 430"))
    {
        Log::Error("EditorGUI initialize failed: OpenGL3 backend initialize failed.");
        ImGui::DestroyContext(context);
        context = nullptr;
        window = nullptr;
        glfwWindow = nullptr;
        return false;
    }

    mouseCursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    mouseCursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    mouseCursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
    mouseCursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    mouseCursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    mouseCursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
    mouseCursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
    mouseCursors[ImGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    mouseCursors[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);

    mainFontAtlas = io.Fonts;
    RegisterDragWindow(glfwWindow);
    activeInstance = this;
    previousWindowFocusCallback = glfwSetWindowFocusCallback(glfwWindow, WindowFocusCallback);
    previousCursorEnterCallback = glfwSetCursorEnterCallback(glfwWindow, CursorEnterCallback);
    previousCursorPositionCallback = glfwSetCursorPosCallback(glfwWindow, CursorPositionCallback);
    previousMouseButtonCallback = glfwSetMouseButtonCallback(glfwWindow, MouseButtonCallback);
    previousScrollCallback = glfwSetScrollCallback(glfwWindow, ScrollCallback);
    previousKeyCallback = glfwSetKeyCallback(glfwWindow, KeyCallback);
    previousCharCallback = glfwSetCharCallback(glfwWindow, CharacterCallback);
    previousTime = glfwGetTime();
    initialized = true;
    return true;
}

void EditorGUI::Shutdown()
{
    if (!initialized) return;

    glfwSetWindowFocusCallback(glfwWindow, previousWindowFocusCallback);
    glfwSetCursorEnterCallback(glfwWindow, previousCursorEnterCallback);
    glfwSetCursorPosCallback(glfwWindow, previousCursorPositionCallback);
    glfwSetMouseButtonCallback(glfwWindow, previousMouseButtonCallback);
    glfwSetScrollCallback(glfwWindow, previousScrollCallback);
    glfwSetKeyCallback(glfwWindow, previousKeyCallback);
    glfwSetCharCallback(glfwWindow, previousCharCallback);
    UnregisterDragWindow(glfwWindow);
    mainFontAtlas = nullptr;
    activeInstance = nullptr;

    EditorIcons::Shutdown();
    ImGui::SetCurrentContext(context);
    ImGui_ImplOpenGL3_Shutdown();
    for (GLFWcursor*& cursor : mouseCursors)
    {
        if (cursor) glfwDestroyCursor(cursor);
        cursor = nullptr;
    }
    ImGui::DestroyContext(context);

    context = nullptr;
    glfwWindow = nullptr;
    window = nullptr;
    sceneMouseWheel = 0.0f;
    initialized = false;
}

//应用共享颜色与尺寸到当前上下文
void EditorGUI::ApplyTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ToImVec4(theme.background);
    style.Colors[ImGuiCol_ChildBg] = style.Colors[ImGuiCol_WindowBg];
    style.Colors[ImGuiCol_PopupBg] = style.Colors[ImGuiCol_WindowBg];
    style.Colors[ImGuiCol_Text] = ToImVec4(theme.text);
    style.Colors[ImGuiCol_Border] = ToImVec4(theme.border);
    for (ImGuiCol index : { ImGuiCol_Header, ImGuiCol_Tab, ImGuiCol_TitleBg })
        style.Colors[index] = ToImVec4(theme.header);
    //输入框与按钮必须与承载它们的卡片不同色，否则控件会退化成标签
    for (ImGuiCol index : { ImGuiCol_FrameBg, ImGuiCol_Button })
        style.Colors[index] = ToImVec4(theme.control);
    for (ImGuiCol index : { ImGuiCol_HeaderHovered, ImGuiCol_ButtonHovered, ImGuiCol_FrameBgHovered, ImGuiCol_TabHovered, ImGuiCol_SeparatorHovered })
        style.Colors[index] = ToImVec4(theme.hovered);
    for (ImGuiCol index : { ImGuiCol_HeaderActive, ImGuiCol_ButtonActive, ImGuiCol_FrameBgActive, ImGuiCol_TabSelected, ImGuiCol_TitleBgActive, ImGuiCol_SeparatorActive })
        style.Colors[index] = ToImVec4(theme.active);
    //滚动条与面板同底，滑块用边框色，否则默认深色滚动条会像贴在面板右缘的一条把手
    style.Colors[ImGuiCol_ScrollbarBg] = style.Colors[ImGuiCol_WindowBg];
    style.Colors[ImGuiCol_ScrollbarGrab] = ToImVec4(theme.border);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ToImVec4(theme.hovered);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ToImVec4(theme.active);
    style.WindowPadding = ImVec2(theme.paddingX, theme.paddingY);
    style.ItemSpacing = ImVec2(theme.spacingX, theme.spacingY);
    style.FramePadding = ImVec2(theme.framePaddingX, theme.framePaddingY);
}

//获取共享停靠分隔尺寸
float32 EditorGUI::GetSplitterSize()
{
    return std::clamp(theme.splitterSize, 1.0f, 20.0f);
}

//获取共享内容边距
ImVec2 EditorGUI::GetWindowPadding()
{
    return ImVec2(theme.paddingX, theme.paddingY);
}

//获取共享强调色，面板描边与浮窗标题栏取同一份
ImU32 EditorGUI::GetActiveColor()
{
    return ImGui::GetColorU32(ToImVec4(theme.active));
}

//获取共享面板圆角半径
float32 EditorGUI::GetPanelCornerRadius()
{
    return std::clamp(theme.cornerRadius, 0.0f, 32.0f);
}

//注册参与跨窗口拖拽判断的编辑器窗口
void EditorGUI::RegisterDragWindow(GLFWwindow* window)
{
    if (!window) return;
    if (std::find(dragWindows.begin(), dragWindows.end(), window) == dragWindows.end())
        dragWindows.push_back(window);
}

//注销参与跨窗口拖拽判断的编辑器窗口
void EditorGUI::UnregisterDragWindow(GLFWwindow* window)
{
    dragWindows.erase(std::remove(dragWindows.begin(), dragWindows.end(), window), dragWindows.end());
}

//判断任一编辑器窗口中左键是否按下
bool EditorGUI::IsLeftMouseDownAnywhere()
{
    for (GLFWwindow* window : dragWindows)
    {
        if (window && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) return true;
    }
    return false;
}

//判断鼠标是否已移出主窗口客户区
bool EditorGUI::IsCursorOutsideMainWindow()
{
    GLFWwindow* window = activeInstance ? activeInstance->glfwWindow : nullptr;
    if (!window) return false;

    double cursorX = 0.0;
    double cursorY = 0.0;
    int32 width = 0;
    int32 height = 0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    glfwGetWindowSize(window, &width, &height);
    return cursorX < 0.0 || cursorY < 0.0 || cursorX >= static_cast<double>(width) || cursorY >= static_cast<double>(height);
}

//获取鼠标在主窗口客户区中的屏幕坐标
vector2 EditorGUI::GetCursorScreenPosition()
{
    GLFWwindow* window = activeInstance ? activeInstance->glfwWindow : nullptr;
    if (!window) return { 0.0f, 0.0f };

    double cursorX = 0.0;
    double cursorY = 0.0;
    int32 windowX = 0;
    int32 windowY = 0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    glfwGetWindowPos(window, &windowX, &windowY);
    return { static_cast<float32>(cursorX) + static_cast<float32>(windowX),
        static_cast<float32>(cursorY) + static_cast<float32>(windowY) };
}

//获取主窗口 GLFW 句柄
GLFWwindow* EditorGUI::GetMainGlfwWindow()
{
    return activeInstance ? activeInstance->glfwWindow : nullptr;
}

//获取主 ImGui 上下文
ImGuiContext* EditorGUI::GetMainContext()
{
    return activeInstance ? activeInstance->context : nullptr;
}

//获取主上下文共享的字体图集
ImFontAtlas* EditorGUI::GetFontAtlas()
{
    return mainFontAtlas;
}

void EditorGUI::BeginFrame()
{
    if (!initialized) return;

    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    int32 windowWidth = 0;
    int32 windowHeight = 0;
    int32 framebufferWidth = 0;
    int32 framebufferHeight = 0;
    glfwGetWindowSize(glfwWindow, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(glfwWindow, &framebufferWidth, &framebufferHeight);
    io.DisplaySize = ImVec2(static_cast<float32>(windowWidth), static_cast<float32>(windowHeight));
    if (windowWidth > 0 && windowHeight > 0)
    {
        io.DisplayFramebufferScale = ImVec2(
            static_cast<float32>(framebufferWidth) / static_cast<float32>(windowWidth),
            static_cast<float32>(framebufferHeight) / static_cast<float32>(windowHeight));
    }

    double currentTime = glfwGetTime();
    io.DeltaTime = previousTime > 0.0 ? static_cast<float32>(currentTime - previousTime) : 1.0f / 60.0f;
    previousTime = currentTime;
    if (io.WantSetMousePos)
    {
        glfwSetCursorPos(glfwWindow, static_cast<double>(io.MousePos.x), static_cast<double>(io.MousePos.y));
    }

    ApplyTheme();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
}

//记录当前 World 会话中的拖拽源
void EditorGUI::SetDragPayload(int32 kind, const std::string& key)
{
    Application* app = Application::Current();
    if (!app || !IsLeftMouseDownAnywhere()) return;
    Object* object = kind == 1 ? Object::FindObject(StringId(key)) : nullptr;
    dragPayload = { kind, key, PathDefines::GetContentRoot(), app->GetWorldRevision(), object ? object->GetObjectId() : 0 };
}

void EditorGUI::Render()
{
    if (!initialized) return;

    ImGui::SetCurrentContext(context);
    if (!IsLeftMouseDownAnywhere() || !HasValidDrag()) dragPayload = {};
    ImGui::Render();
    UpdateMouseCursor();

    int32 framebufferWidth = 0;
    int32 framebufferHeight = 0;
    glfwGetFramebufferSize(glfwWindow, &framebufferWidth, &framebufferHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

//场景改为离屏渲染后主窗口不再被场景填充，需要按主题背景色自行清空
void EditorGUI::ClearMainFramebuffer()
{
    if (!initialized) return;

    int32 framebufferWidth = 0;
    int32 framebufferHeight = 0;
    glfwGetFramebufferSize(glfwWindow, &framebufferWidth, &framebufferHeight);

    ImVec4 background = ToImVec4(theme.background);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(background.x, background.y, background.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

EditorGuiNativeApi EditorGUI::GetNativeApi() const
{
    EditorGuiNativeApi api;
    api.label = reinterpret_cast<void*>(&EditorGuiLabel);
    api.button = reinterpret_cast<void*>(&EditorGuiButton);
    api.beginComponentBlock = reinterpret_cast<void*>(&EditorGuiBeginComponentBlock);
    api.endComponentBlock = reinterpret_cast<void*>(&EditorGuiEndComponentBlock);
    api.beginCollapsibleComponentBlock = reinterpret_cast<void*>(&EditorGuiBeginCollapsibleComponentBlock);
    api.beginCombo = reinterpret_cast<void*>(&EditorGuiBeginCombo);
    api.endCombo = reinterpret_cast<void*>(&EditorGuiEndCombo);
    api.selectable = reinterpret_cast<void*>(&EditorGuiSelectable);
    api.checkbox = reinterpret_cast<void*>(&EditorGuiCheckbox);
    api.inputInt = reinterpret_cast<void*>(&EditorGuiInputInt);
    api.inputFloat = reinterpret_cast<void*>(&EditorGuiInputFloat);
    api.inputVector3 = reinterpret_cast<void*>(&EditorGuiInputVector3);
    api.inputText = reinterpret_cast<void*>(&EditorGuiInputText);
    api.separator = reinterpret_cast<void*>(&EditorGuiSeparator);
    api.sameLine = reinterpret_cast<void*>(&EditorGuiSameLine);
    api.beginTable = reinterpret_cast<void*>(&EditorGuiBeginTable);
    api.endTable = reinterpret_cast<void*>(&EditorGuiEndTable);
    api.tableSetupColumn = reinterpret_cast<void*>(&EditorGuiTableSetupColumn);
    api.tableHeadersRow = reinterpret_cast<void*>(&EditorGuiTableHeadersRow);
    api.tableNextRow = reinterpret_cast<void*>(&EditorGuiTableNextRow);
    api.tableSetColumnIndex = reinterpret_cast<void*>(&EditorGuiTableSetColumnIndex);
    api.tableSelectable = reinterpret_cast<void*>(&EditorGuiTableSelectable);
    api.isItemDoubleClicked = reinterpret_cast<void*>(&EditorGuiIsItemDoubleClicked);
    api.beginPopupContextItem = reinterpret_cast<void*>(&EditorGuiBeginPopupContextItem);
    api.beginPopupContextWindow = reinterpret_cast<void*>(&EditorGuiBeginPopupContextWindow);
    api.endPopup = reinterpret_cast<void*>(&EditorGuiEndPopup);
    api.menuItem = reinterpret_cast<void*>(&EditorGuiMenuItem);
    api.setClipboardText = reinterpret_cast<void*>(&EditorGuiSetClipboardText);
    api.beginDisabled = reinterpret_cast<void*>(&EditorGuiBeginDisabled);
    api.endDisabled = reinterpret_cast<void*>(&EditorGuiEndDisabled);
    api.beginChild = reinterpret_cast<void*>(&EditorGuiBeginChild);
    api.endChild = reinterpret_cast<void*>(&EditorGuiEndChild);
    api.treeNode = reinterpret_cast<void*>(&EditorGuiTreeNode);
    api.treePop = reinterpret_cast<void*>(&EditorGuiTreePop);
    api.openPopup = reinterpret_cast<void*>(&EditorGuiOpenPopup);
    api.beginPopup = reinterpret_cast<void*>(&EditorGuiBeginPopup);
    api.closePopup = reinterpret_cast<void*>(&EditorGuiClosePopup);
    api.dragSource = reinterpret_cast<void*>(&EditorGuiDragSource);
    api.readDrag = reinterpret_cast<void*>(&EditorGuiReadDrag);
    api.acceptDrag = reinterpret_cast<void*>(&EditorGuiAcceptDrag);
    api.fillRemainingArea = reinterpret_cast<void*>(&EditorGuiFillRemainingArea);
    api.getDropPlacement = reinterpret_cast<void*>(&EditorGuiGetDropPlacement);
    api.setTheme = reinterpret_cast<void*>(&EditorGuiSetTheme);
    api.beginPanelContent = reinterpret_cast<void*>(&EditorGuiBeginPanelContent);
    api.endPanelContent = reinterpret_cast<void*>(&EditorGuiEndPanelContent);
    api.drawSceneView = reinterpret_cast<void*>(&EditorGuiDrawSceneView);
    api.resolveSceneDropPosition = reinterpret_cast<void*>(&EditorGuiResolveSceneDropPosition);
    api.referenceField = reinterpret_cast<void*>(&EditorGuiReferenceField);
    api.assetTile = reinterpret_cast<void*>(&EditorGuiAssetTile);
    api.viewToggleButton = reinterpret_cast<void*>(&EditorGuiViewToggleButton);
    return api;
}

float32 EditorGUI::ConsumeSceneMouseWheel()
{
    float32 value = sceneMouseWheel;
    sceneMouseWheel = 0.0f;
    return value;
}

bool EditorGUI::IsInitialized() const
{
    return initialized;
}

ImGuiKey EditorGUI::ConvertKey(int32 key)
{
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
    {
        return static_cast<ImGuiKey>(ImGuiKey_0 + key - GLFW_KEY_0);
    }
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
    {
        return static_cast<ImGuiKey>(ImGuiKey_A + key - GLFW_KEY_A);
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F24)
    {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + key - GLFW_KEY_F1);
    }
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
    {
        return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + key - GLFW_KEY_KP_0);
    }

    switch (key)
    {
    case GLFW_KEY_TAB: return ImGuiKey_Tab;
    case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
    case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
    case GLFW_KEY_UP: return ImGuiKey_UpArrow;
    case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
    case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
    case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
    case GLFW_KEY_HOME: return ImGuiKey_Home;
    case GLFW_KEY_END: return ImGuiKey_End;
    case GLFW_KEY_INSERT: return ImGuiKey_Insert;
    case GLFW_KEY_DELETE: return ImGuiKey_Delete;
    case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
    case GLFW_KEY_SPACE: return ImGuiKey_Space;
    case GLFW_KEY_ENTER: return ImGuiKey_Enter;
    case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
    case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
    case GLFW_KEY_COMMA: return ImGuiKey_Comma;
    case GLFW_KEY_MINUS: return ImGuiKey_Minus;
    case GLFW_KEY_PERIOD: return ImGuiKey_Period;
    case GLFW_KEY_SLASH: return ImGuiKey_Slash;
    case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
    case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
    case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
    case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
    case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
    case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
    case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
    case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
    case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
    case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
    case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
    case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
    case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
    case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
    case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
    case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
    case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
    case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
    case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
    case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
    case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
    case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
    case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
    case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
    case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
    case GLFW_KEY_MENU: return ImGuiKey_Menu;
    default: return ImGuiKey_None;
    }
}

void EditorGUI::UpdateKeyModifiers(ImGuiIO& io, int32 modifiers)
{
    io.AddKeyEvent(ImGuiMod_Ctrl, (modifiers & GLFW_MOD_CONTROL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (modifiers & GLFW_MOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (modifiers & GLFW_MOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (modifiers & GLFW_MOD_SUPER) != 0);
}

void EditorGUI::WindowFocusCallback(GLFWwindow* callbackWindow, int32 focused)
{
    EditorGUI* editorGUI = activeInstance;
    if (editorGUI && callbackWindow == editorGUI->glfwWindow)
    {
        ImGuiContext* previousContext = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(editorGUI->context);
        ImGui::GetIO().AddFocusEvent(focused != 0);
        ImGui::SetCurrentContext(previousContext);
    }
    if (editorGUI && editorGUI->previousWindowFocusCallback)
    {
        editorGUI->previousWindowFocusCallback(callbackWindow, focused);
    }
}

void EditorGUI::CursorEnterCallback(GLFWwindow* callbackWindow, int32 entered)
{
    EditorGUI* editorGUI = activeInstance;
    if (editorGUI && callbackWindow == editorGUI->glfwWindow && entered == 0)
    {
        ImGuiContext* previousContext = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(editorGUI->context);
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        ImGui::SetCurrentContext(previousContext);
    }
    if (editorGUI && editorGUI->previousCursorEnterCallback)
    {
        editorGUI->previousCursorEnterCallback(callbackWindow, entered);
    }
}

void EditorGUI::CursorPositionCallback(GLFWwindow* callbackWindow, double x, double y)
{
    EditorGUI* editorGUI = activeInstance;
    if (editorGUI && callbackWindow == editorGUI->glfwWindow)
    {
        ImGuiContext* previousContext = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(editorGUI->context);
        ImGui::GetIO().AddMousePosEvent(static_cast<float32>(x), static_cast<float32>(y));
        ImGui::SetCurrentContext(previousContext);
    }
    if (editorGUI && editorGUI->previousCursorPositionCallback)
    {
        editorGUI->previousCursorPositionCallback(callbackWindow, x, y);
    }
}

void EditorGUI::MouseButtonCallback(GLFWwindow* callbackWindow, int32 button, int32 action, int32 modifiers)
{
    EditorGUI* editorGUI = activeInstance;
    if (editorGUI && callbackWindow == editorGUI->glfwWindow && button >= 0 && button < ImGuiMouseButton_COUNT)
    {
        ImGuiContext* previousContext = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(editorGUI->context);
        ImGuiIO& io = ImGui::GetIO();
        UpdateKeyModifiers(io, modifiers);
        io.AddMouseButtonEvent(button, action == GLFW_PRESS);
        ImGui::SetCurrentContext(previousContext);
    }
    if (editorGUI && editorGUI->previousMouseButtonCallback)
    {
        editorGUI->previousMouseButtonCallback(callbackWindow, button, action, modifiers);
    }
}

void EditorGUI::ScrollCallback(GLFWwindow* callbackWindow, double x, double y)
{
    EditorGUI* editorGUI = activeInstance;
    if (editorGUI && callbackWindow == editorGUI->glfwWindow)
    {
        ImGuiContext* previousContext = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(editorGUI->context);
        ImGui::GetIO().AddMouseWheelEvent(static_cast<float32>(x), static_cast<float32>(y));
        ImGui::SetCurrentContext(previousContext);
        editorGUI->sceneMouseWheel += static_cast<float32>(y);
    }
    if (editorGUI && editorGUI->previousScrollCallback)
    {
        editorGUI->previousScrollCallback(callbackWindow, x, y);
    }
}

void EditorGUI::KeyCallback(GLFWwindow* callbackWindow,
    int32 key,
    int32 scanCode,
    int32 action,
    int32 modifiers)
{
    EditorGUI* editorGUI = activeInstance;
    if (editorGUI && callbackWindow == editorGUI->glfwWindow && action != GLFW_REPEAT)
    {
        ImGuiKey editorKey = ConvertKey(key);
        if (editorKey != ImGuiKey_None)
        {
            ImGuiContext* previousContext = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(editorGUI->context);
            ImGuiIO& io = ImGui::GetIO();
            UpdateKeyModifiers(io, modifiers);
            io.AddKeyEvent(editorKey, action == GLFW_PRESS);
            io.SetKeyEventNativeData(editorKey, key, scanCode);
            ImGui::SetCurrentContext(previousContext);
        }
    }
    if (editorGUI && editorGUI->previousKeyCallback)
    {
        editorGUI->previousKeyCallback(callbackWindow, key, scanCode, action, modifiers);
    }
}

void EditorGUI::CharacterCallback(GLFWwindow* callbackWindow, uint32 codePoint)
{
    EditorGUI* editorGUI = activeInstance;
    if (editorGUI && callbackWindow == editorGUI->glfwWindow)
    {
        ImGuiContext* previousContext = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(editorGUI->context);
        ImGui::GetIO().AddInputCharacter(codePoint);
        ImGui::SetCurrentContext(previousContext);
    }
    if (editorGUI && editorGUI->previousCharCallback)
    {
        editorGUI->previousCharCallback(callbackWindow, codePoint);
    }
}

void EditorGUI::UpdateMouseCursor()
{
    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) != 0) return;

    ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
    if (cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
    {
        glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        return;
    }

    glfwSetCursor(glfwWindow,
        mouseCursors[cursor] ? mouseCursors[cursor] : mouseCursors[ImGuiMouseCursor_Arrow]);
    glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}
