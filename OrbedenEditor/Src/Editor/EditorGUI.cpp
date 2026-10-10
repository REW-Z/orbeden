#include "Editor/EditorGUI.h"

#include "Editor/EditorIcons.h"
#include "Editor/EditorScene.h"
#include "Log/Log.h"
#include "Profiler/Profiler.h"
#include "Application.h"
#include "FileSystem/PathDefines.h"
#include "Runtime/Native/InteropText.h"
#include <filesystem>
#include "Platform/GlfwWindow.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/Object/Texture2D.h"
#include "Runtime/Native/NativeCall.h"
//颜色字段原语在核心层，标签列宽度推给它才能跟编辑器这一侧的属性行对齐
#include "Runtime/Gui/RuntimeGuiBridge.h"

#include <glad/gl.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iterator>
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

    //确认窗的默认宽度：够放下一行完整提示，又不至于横跨整个编辑器
    constexpr float32 DefaultDialogWidth = 420.0f;

    //取浮点值最短又能原样读回的十进制写法，等价于"完整 ToString 再截掉尾零"：
    //0.5 写成 "0.5"，有效位更多的按实际位数写出来，不补零。
    //走定点写法而不是科学计数法，坐标这类字段里 "100000" 才是编辑器该显示的东西。
    std::string FormatFloatText(float32 value)
    {
        char buffer[64];
        std::to_chars_result written = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed);
        if (written.ec != std::errc())
        {
            //float 定长写法最长约 46 字符（最小次正规数），正常进不来；真写不下就退回短写法
            written = std::to_chars(buffer, buffer + sizeof(buffer), value);
        }
        return std::string(buffer, written.ptr);
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
        ImTextureID texture = EditorIcons::Get(name, size);
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
        //列表内部排序由托管控件校验文档身份和下标，不对应资源路径。
        if (dragPayload.kind == 3) return true;
        std::error_code error;
        return std::filesystem::exists(InteropText::PathFromUtf8(dragPayload.contentRoot) / InteropText::PathFromUtf8(dragPayload.key), error);
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

    //属性行标签列的宽度。来自项目级 Editor GUI 配置（.oeproj 的 <EditorGuiConfig>），
    //由托管侧经 EditorThemeData 送进来、在 ApplyTheme 里落到这里。
    //颜色字段（RuntimeGuiBridge.cpp）与对象框（托管侧 EditorObjectField.cs）用的是同一个值，
    //三处必须一致，否则不同类型的框落不到同一列。
    float32 propertyLabelWidth = 120.0f;

    //当前生效的字体配置。空 = 内置点阵字体，BuiltinVectorFont = 内置矢量字体，其余是字体文件路径。
    std::string editorFont;
    float32 editorFontSize = 13.0f;
    //内置点阵只适合 13px：字号在它上面一律按 13 算，免得放糊
    bool editorFontIsBitmap = true;
    constexpr float32 BitmapFontSize = 13.0f;

    //按当前字体配置重建图集。字号在 AddFont 时就烘进去，图集按字号重新光栅化，
    //所以换字号要重建而不是缩放。ImGui 1.92 的字体系统支持运行时重建：
    //ClearFonts 会通知所有用这张图表的上下文改绑新字体（浮动窗的上下文也在内）。
    void BuildEditorFont(ImFontAtlas& atlas)
    {
        atlas.Clear();

        if (editorFontIsBitmap)
        {
            atlas.AddFontDefaultBitmap();
            return;
        }

        ImFontConfig config;
        config.SizePixels = editorFontSize;
        if (editorFont == EditorGUI::BuiltinVectorFont)
        {
            atlas.AddFontDefaultVector(&config);
            return;
        }

        //字体文件走内存加载：ImGui 的 AddFontFromFileTTF 内部是 fopen，Windows 上按 ANSI 解释路径，
        //"C:\Windows\Fonts\微软雅黑.ttf" 这类会直接失败。自己按 UTF-8 路径读进来再交过去。
        std::ifstream input(InteropText::PathFromUtf8(editorFont), std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (bytes.empty())
        {
            Log::Warning(("Editor font could not be read, using the built-in font: " + editorFont).c_str());
            editorFontIsBitmap = true;
            atlas.AddFontDefaultBitmap();
            return;
        }

        //图集拿走这块内存的所有权（ClearFonts 时按 ImGui 的分配器释放），
        //因此必须用它的 MemAlloc，不能把 std::string 的缓冲直接交出去。
        void* data = ImGui::MemAlloc(bytes.size());
        if (!data)
        {
            editorFontIsBitmap = true;
            atlas.AddFontDefaultBitmap();
            return;
        }
        std::memcpy(data, bytes.data(), bytes.size());
        if (!atlas.AddFontFromMemoryTTF(data, static_cast<int32>(bytes.size()), editorFontSize, &config))
        {
            ImGui::MemFree(data);
            editorFontIsBitmap = true;
            Log::Warning(("Editor font could not be parsed, using the built-in font: " + editorFont).c_str());
            atlas.AddFontDefaultBitmap();
        }
    }

    //把属性标签画进左侧固定宽度的标签列，再把光标移到右列起点，返回控件该用的 label。
    //返回空串表示标签没有可见文字（只有 "##ID"）：调用方仍按老样子就地画控件——
    //列表元素、表格单元格与工具条上的搜索框都靠这条不跟着跳列。
    std::string BeginPropertyRow(const std::string& text)
    {
        const char* begin = text.c_str();
        const char* end = ImGui::FindRenderedTextEnd(begin);
        if (begin == end) return std::string();

        //标签超长时裁在列内，不挤进右边的控件区
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::PushClipRect(start, ImVec2(start.x + propertyLabelWidth - ImGui::GetStyle().ItemInnerSpacing.x,
            start.y + ImGui::GetFrameHeight()), true);
        ImGui::TextEx(begin, end);
        ImGui::PopClipRect();
        //右列用 SameLine(offset) 定位：它与托管侧对象框走的是同一个 EditorGuiSameLine，
        //两者的基准一致（都从窗口左缘起算），列位才对得上。
        //代价是这一路会带上外层 Group 的偏移——列表与向量控件内部才开 Group，
        //那两处画的是没有可见标签的控件，走不到这里
        ImGui::SameLine(propertyLabelWidth);
        //控件 ID 取 "##" 之后的部分；原标签没有 "##" 时补一个，同一次调用里仍然唯一
        return *end == '\0' ? "##" + text : std::string(end);
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

    //判断左键松开的这一下是否点在本节点上：按下点要落在这个节点自己的行内，
    //箭头区除外——箭头是按下当帧就切换展开的，松开时不该再补一次选中；叶子没有箭头，整行都能点
    bool IsReleasedOnNodeLabel(uint8 options)
    {
        if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left) || !ImGui::IsItemHovered()) return false;
        ImVec2 pressed = ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left];
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        if (pressed.x < min.x || pressed.x >= max.x || pressed.y < min.y || pressed.y >= max.y) return false;
        if (options & 2) return true;
        return pressed.x > min.x + ImGui::GetTreeNodeToLabelSpacing() + ImGui::GetStyle().TouchExtraPadding.x;
    }

    //绘制目录节点并返回展开与点击状态
    //options：1 选中、2 叶子、4 默认展开、8 强制展开、16 强制折叠、32 灰显
    //返回值：1 展开、2 按下点击、4 Ctrl、8 双击、16 Alt、32 本次刚切换、64 松开点击
    //同一个节点两种点击都报：按下点击给要即刻响应的操作，选中用松开点击——
    //拖动会跨到别的面板，按下就落定选中会让对方面板在拖到一半时换掉内容
    int32 ORBEDEN_NATIVE_CALL EditorGuiTreeNode(const uint8* label, int32 length, uint8 options, const uint8* icon, int32 iconLength)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (options & 1) flags |= ImGuiTreeNodeFlags_Selected;
        if (options & 2) flags |= ImGuiTreeNodeFlags_Leaf;
        if (options & 4) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        //DefaultOpen 只是存储里还没记录时的初值，已折叠过的节点要靠这一句才打得开
        if (options & 8) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        //强制折叠同理：递归折叠时父节点一合上，子树这一帧就不会被提交，只能等它各自被画到时再压回去
        if (options & 16) ImGui::SetNextItemOpen(false, ImGuiCond_Always);
        std::string identity = ReadUtf8Text(label, length);
        ImTextureID texture = EditorIcons::Get(ReadUtf8Text(icon, iconLength), ImGui::GetFontSize());
        ImVec2 iconPosition = ImGui::GetCursorScreenPos();
        iconPosition.x += ImGui::GetTreeNodeToLabelSpacing();
        iconPosition.y += ImGui::GetStyle().FramePadding.y;
        float32 iconSize = ImGui::GetFontSize();
        //灰显连箭头一起压暗；取色走主题的 TextDisabled，与瓦片箭头、面板标签页一致
        bool dimmed = (options & 32) != 0;
        if (dimmed) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        bool expanded;
        if (texture != 0)
        {
            std::string text = identity.substr(0, identity.find("##"));
            usize padding = static_cast<usize>((iconSize + ImGui::GetStyle().ItemInnerSpacing.x) / ImGui::CalcTextSize(" ").x) + 1;
            text.insert(0, padding, ' ');
            expanded = ImGui::TreeNodeEx(identity.c_str(), flags, "%s", text.c_str());
            ImGui::GetWindowDrawList()->AddImage(texture, iconPosition,
                ImVec2(iconPosition.x + iconSize, iconPosition.y + iconSize));
        }
        else expanded = ImGui::TreeNodeEx(identity.c_str(), flags);
        if (dimmed) ImGui::PopStyleColor();
        bool toggled = ImGui::IsItemToggledOpen();
        bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        return (expanded ? 1 : 0) | (ImGui::IsItemClicked() && !toggled ? 2 : 0)
            | (ImGui::GetIO().KeyCtrl ? 4 : 0) | (doubleClicked ? 8 : 0)
            | (ImGui::GetIO().KeyAlt ? 16 : 0) | (toggled ? 32 : 0)
            | (IsReleasedOnNodeLabel(options) ? 64 : 0);
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

    //绘制浮点滑条；宽度 <= 0 时用 ImGui 默认宽度
    uint8 ORBEDEN_NATIVE_CALL EditorGuiSliderFloat(const uint8* id, int32 length, float32* value,
        float32 minimum, float32 maximum, float32 width)
    {
        if (!value || maximum <= minimum) return 0;
        std::string text = ReadUtf8Text(id, length);
        const std::string control = BeginPropertyRow(text);
        const bool columned = control.length() != 0;
        ImGui::SetNextItemWidth(width > 0.0f ? width : columned ? -FLT_MIN : -1.0f);
        return ImGui::SliderFloat(columned ? control.c_str() : text.c_str(), value, minimum, maximum,
            "%.0f", ImGuiSliderFlags_AlwaysClamp) ? 1 : 0;
    }

    //开始一个固定宽度的模态确认窗；id 里 ### 之前是标题栏文字、之后是稳定 ID
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginDialog(const uint8* id, int32 length, float32 width)
    {
        //宽度先定死再开窗：自动宽度下首帧文本会先按极窄的宽度折行，窗体会被拉成细高条
        ImGui::SetNextWindowSize(ImVec2(width > 0.0f ? width : DefaultDialogWidth, 0.0f), ImGuiCond_Always);
        return ImGui::BeginPopupModal(ReadUtf8Text(id, length).c_str(), nullptr, 0) ? 1 : 0;
    }

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

    //量出按钮将要占用的宽度：与 EditorGuiButton 同一套文本测量与内边距，
    //供工具条在绘制前排版（## 之后的 ID 部分不计入宽度）
    float32 ORBEDEN_NATIVE_CALL EditorGuiCalcButtonWidth(const uint8* text, int32 length)
    {
        std::string value = ReadUtf8Text(text, length);
        return ImGui::CalcTextSize(value.c_str(), nullptr, true).x
            + ImGui::GetStyle().FramePadding.x * 2.0f;
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

    //在指定矩形上画一个勾选框并回传新值。
    //不能走 SetCursorScreenPos + Checkbox：那会置位 DC.IsSetPos，而 ImGui::End 只在标志仍为 true 时才做
    //ErrorCheckUsingSetCursorPosToExtendParentBoundaries 检查，光标落到已画内容之外就断言。
    //卡片折叠时正文一个条目都不提交，没人会把标志清掉，必然撞上；所以这里显式传矩形 ItemAdd，
    //光标一动不动——ImGui 给 CollapsingHeader 画关闭叉用的也是这个路子。
    //在指定矩形上画一个勾选框：返回是否被点，新值写回 value。
    //形态照 ImGui::Checkbox(label, bool*) 来，调用方读"是否被点"这个显式信号，
    //不要靠新值是真是假去反推——那样只有"取消勾选"能被识别，"重新勾上"会丢
    bool DrawOverlayCheckbox(const char* id, const ImVec2& position, bool* value)
    {
        //只用 imgui.h 导出的函数，不碰 imgui_internal.h 里的 inline 助手（GetCurrentWindow 等）：
        //那些助手直接读 GImGui，而 IMGUI_API 是空宏、ImGui 静态编在 OrbedenCore 里，
        //GImGui 不导出，一旦引用就是 LNK2001
        const float32 size = ImGui::GetFrameHeight();
        const ImRect bounds(position, ImVec2(position.x + size, position.y + size));
        const ImGuiID itemId = ImGui::GetID(id);

        //卡片窄到把勾选框切出去时它整个不参与：既不该画，也不该还能点到
        if (!ImGui::ItemAdd(bounds, itemId)) return false;
        bool hovered = false;
        bool held = false;
        const bool pressed = ImGui::ButtonBehavior(bounds, itemId, &hovered, &held);
        if (pressed) *value = !*value;

        //外观照标准 Checkbox 画：同样的取色与勾形，免得同一个面板里两种勾选框
        ImGui::RenderNavCursor(bounds, itemId);
        const ImU32 background = ImGui::GetColorU32((held && hovered) ? ImGuiCol_FrameBgActive
            : hovered ? ImGuiCol_FrameBgHovered : *value ? ImGuiCol_CheckboxSelectedBg : ImGuiCol_FrameBg);
        ImGui::RenderFrame(bounds.Min, bounds.Max, background, true, ImGui::GetStyle().FrameRounding);
        if (*value)
        {
            const float32 pad = ImMax(1.0f, static_cast<float32>(static_cast<int32>(size / 6.0f)));
            ImGui::RenderCheckMark(ImGui::GetWindowDrawList(), ImVec2(bounds.Min.x + pad, bounds.Min.y + pad),
                ImGui::GetColorU32(ImGuiCol_CheckMark), size - pad * 2.0f);
        }
        return pressed;
    }

    //开始可折叠组件块。
    //toggleRequested 非空时在标题行右缘画一个激活勾选框，并由它回传是否被点；
    //enabled 为 0 时整张卡片底色压暗一档。资产检查卡片用空指针调用，不画勾选框也不变暗。
    //defaultOpen 只管"还没有记住状态"时是展开还是折叠：记住的状态由 ImGui 按 id 存。
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginCollapsibleComponentBlock(const uint8* icon,
        int32 iconLength,
        const uint8* title,
        int32 titleLength,
        const uint8* id,
        int32 idLength,
        uint8 enabled,
        uint8 defaultOpen,
        uint8* toggleRequested)
    {
        std::string iconName = ReadUtf8Text(icon, iconLength);
        std::string value = ReadUtf8Text(title, titleLength);
        std::string identity = ReadUtf8Text(id, idLength);
        if (value.empty()) value = "Component";
        if (identity.empty()) identity = value;
        if (toggleRequested) *toggleRequested = 0;

        ImGui::Spacing();
        ImGui::PushID(identity.c_str());
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        //组件块用表面色铺底，不能借用控件填充色，否则块内控件看不出来
        ImVec4 cardColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);
        //未激活的卡片在表面色与面板底色之间取中点：暗一档，又还看得出是张卡片。
        //取中而不是乘系数，换主题时跟着主题自己的两个颜色走，不会偏色
        if (toggleRequested && enabled == 0)
        {
            const ImVec4& background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
            cardColor.x = (cardColor.x + background.x) * 0.5f;
            cardColor.y = (cardColor.y + background.y) * 0.5f;
            cardColor.z = (cardColor.z + background.z) * 0.5f;
        }
        ImGui::PushStyleColor(ImGuiCol_ChildBg, cardColor);
        ImGui::BeginChild("##component",
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        //勾选框是压在标题行上的后一个条目，标题必须让出重叠区，
        //否则一次点击会同时打中标题与勾选框（ImGui 自己画关闭叉时也是这么标记的）
        if (toggleRequested) flags |= ImGuiTreeNodeFlags_AllowOverlap;
        DrawInlineIcon(iconName, ImGui::GetFrameHeight());
        //标题自己要占一个独立 ID：CollapsingHeader 是 ImGuiTreeNodeFlags_CollapsingHeader，
        //它只画表头、不压 ID 栈，卡片正文与标题处在同一层。标题一旦和卡内某个字段同名
        //（Shape 模块里的 Shape 下拉框就是这种情况），两处算出的 ID 完全相同，
        //ImGui 的 ID 冲突调试器会报 "2 visible items with conflicting ID"。
        //"##" 之后只参与 ID 计算，画在标题行上的仍是纯名字。
        const std::string headerLabel = value + "##component_header";
        bool expanded = ImGui::CollapsingHeader(headerLabel.c_str(), flags);
        //标题行几何要在 LastItemData 还有效时取：勾选框是按这个矩形摆的，而 Separator 会顶掉它
        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();
        const float32 headerHeight = ImGui::GetItemRectSize().y;
        //标题右键菜单。菜单内容由托管侧绘制：它必须在同一个子窗、同一个 ID 栈深度上
        //用同样的字符串开弹窗（InspectorPanel 的组件菜单 id 就是 identity + "##component_menu"），
        //两边算出的 popup ID 才一致。Separator 会顶掉 LastItemData，所以这句必须排在它前面。
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup((identity + "##component_menu").c_str());
        if (expanded) ImGui::Separator();

        //勾选框贴标题行右缘。标题行已被 SpanAvailWidth 占满，只能按算出的矩形单独画一个
        if (toggleRequested)
        {
            const float32 size = ImGui::GetFrameHeight();
            const float32 right = headerMax.x - ImGui::GetStyle().FramePadding.x;
            bool active = enabled != 0;
            if (DrawOverlayCheckbox("##component_enabled",
                ImVec2(right - size, headerMin.y + (headerHeight - size) * 0.5f), &active))
                *toggleRequested = 1;
        }
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
        const std::string control = BeginPropertyRow(labelText);
        const bool columned = control.length() != 0;
        if (columned) ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::BeginCombo(columned ? control.c_str() : labelText.c_str(), previewText.c_str()) ? 1 : 0;
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
        const std::string control = BeginPropertyRow(text);
        bool changed = ImGui::Checkbox(control.length() != 0 ? control.c_str() : text.c_str(), &boolValue);
        *value = boolValue ? 1 : 0;
        return changed ? 1 : 0;
    }

    //绘制整数输入框
    uint8 ORBEDEN_NATIVE_CALL EditorGuiInputInt(const uint8* label, int32 length, int32* value)
    {
        if (!value) return 0;
        std::string text = ReadUtf8Text(label, length);
        const std::string control = BeginPropertyRow(text);
        if (control.length() != 0) ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::InputInt(control.length() != 0 ? control.c_str() : text.c_str(), value) ? 1 : 0;
    }

    //绘制浮点输入框；ImGui 的 format 对浮点只管显示（解析固定按 %f 走），所以直接把算好的文本交过去
    uint8 ORBEDEN_NATIVE_CALL EditorGuiInputFloat(const uint8* label, int32 length, float32* value)
    {
        if (!value) return 0;
        std::string text = ReadUtf8Text(label, length);
        const std::string control = BeginPropertyRow(text);
        if (control.length() != 0) ImGui::SetNextItemWidth(-FLT_MIN);
        std::string display = FormatFloatText(*value);
        return ImGui::InputFloat(control.length() != 0 ? control.c_str() : text.c_str(),
            value, 0.0f, 0.0f, display.c_str()) ? 1 : 0;
    }

    //绘制三维向量输入框；三个分量各自取最短写法，所以不能借 ImGui::InputFloat3 那种三格共用的格式
    uint8 ORBEDEN_NATIVE_CALL EditorGuiInputVector3(const uint8* label, int32 length, vector3* value)
    {
        if (!value) return 0;

        std::string text = ReadUtf8Text(label, length);
        //标签有可见文字时画进左列，三格吃满右列；否则仍把标签画在三格右侧
        const std::string control = BeginPropertyRow(text);
        const bool columned = control.length() != 0;
        float32 values[3] = { value->x, value->y, value->z };
        std::string displays[3] = { FormatFloatText(values[0]), FormatFloatText(values[1]), FormatFloatText(values[2]) };

        bool changed = false;
        ImGui::BeginGroup();
        ImGui::PushID(text.c_str());
        ImGui::PushMultiItemsWidths(3, columned ? ImGui::GetContentRegionAvail().x : ImGui::CalcItemWidth());
        for (int32 index = 0; index < 3; ++index)
        {
            ImGui::PushID(index);
            if (index > 0) ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            changed |= ImGui::InputFloat("", &values[index], 0.0f, 0.0f, displays[index].c_str()) != 0;
            ImGui::PopID();
            ImGui::PopItemWidth();
        }
        ImGui::PopID();

        if (!columned)
        {
            //标签画在三格右侧，与 ImGui::InputFloat3 的排布保持一致
            const char* labelEnd = ImGui::FindRenderedTextEnd(text.c_str());
            if (text.c_str() != labelEnd)
            {
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::TextEx(text.c_str(), labelEnd);
            }
        }
        ImGui::EndGroup();
        if (changed)
        {
            value->x = values[0];
            value->y = values[1];
            value->z = values[2];
        }
        return changed ? 1 : 0;
    }

    //绘制二维向量输入框；与三维版本一样逐分量取最短写法
    uint8 ORBEDEN_NATIVE_CALL EditorGuiInputVector2(const uint8* label, int32 length, vector2* value)
    {
        if (!value) return 0;

        std::string text = ReadUtf8Text(label, length);
        //标签有可见文字时画进左列，两格吃满右列；否则仍把标签画在两格右侧
        const std::string control = BeginPropertyRow(text);
        const bool columned = control.length() != 0;
        float32 values[2] = { value->x, value->y };
        std::string displays[2] = { FormatFloatText(values[0]), FormatFloatText(values[1]) };

        bool changed = false;
        ImGui::BeginGroup();
        ImGui::PushID(text.c_str());
        ImGui::PushMultiItemsWidths(2, columned ? ImGui::GetContentRegionAvail().x : ImGui::CalcItemWidth());
        for (int32 index = 0; index < 2; ++index)
        {
            ImGui::PushID(index);
            if (index > 0) ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            changed |= ImGui::InputFloat("", &values[index], 0.0f, 0.0f, displays[index].c_str()) != 0;
            ImGui::PopID();
            ImGui::PopItemWidth();
        }
        ImGui::PopID();

        if (!columned)
        {
            //标签画在两格右侧，与 ImGui::InputFloat2 的排布保持一致
            const char* labelEnd = ImGui::FindRenderedTextEnd(text.c_str());
            if (text.c_str() != labelEnd)
            {
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::TextEx(text.c_str(), labelEnd);
            }
        }
        ImGui::EndGroup();
        if (changed)
        {
            value->x = values[0];
            value->y = values[1];
        }
        return changed ? 1 : 0;
    }

    //绘制字符串输入框；width <= 0 时用 ImGui 默认宽度
    int32 ORBEDEN_NATIVE_CALL EditorGuiInputText(const uint8* label, int32 length, uint8* buffer, int32 bufferSize,
        float32 width, uint8 readOnly)
    {
        if (!buffer || bufferSize <= 0) return -1;

        std::string text = ReadUtf8Text(label, length);
        const std::string control = BeginPropertyRow(text);
        const bool columned = control.length() != 0;
        buffer[bufferSize - 1] = 0;
        if (width > 0.0f) ImGui::SetNextItemWidth(width);
        else if (columned) ImGui::SetNextItemWidth(-FLT_MIN);
        //只读框一律按禁用态压暗（编辑器的禁用控件就是 ImGui 那个 Alpha 乘法），
        //但走 ReadOnly 标志而不是真禁用：文字仍可选中复制，只是按下不进去
        const ImGuiInputTextFlags flags = readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
        const float32 alpha = ImGui::GetStyle().Alpha * ImGui::GetStyle().DisabledAlpha;
        if (readOnly) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        bool changed = ImGui::InputText(columned ? control.c_str() : text.c_str(),
            reinterpret_cast<char*>(buffer), static_cast<usize>(bufferSize), flags);
        if (readOnly) ImGui::PopStyleVar();
        return changed ? static_cast<int32>(std::strlen(reinterpret_cast<const char*>(buffer))) : -1;
    }

    //绘制对象引用框并返回操作：0 无 1 点击引用框 2 清空 3 打开选择器 4 双击引用框
    int32 ORBEDEN_NATIVE_CALL EditorGuiReferenceField(const uint8* icon, int32 iconLength,
        const uint8* text, int32 textLength, const uint8* id, int32 idLength)
    {
        std::string iconName = ReadUtf8Text(icon, iconLength);
        std::string label = ReadUtf8Text(text, textLength);
        std::string identity = ReadUtf8Text(id, idLength);

        ImGuiStyle& style = ImGui::GetStyle();
        float32 height = ImGui::GetFrameHeight();
        //引用框住在属性行的右列：调用方先画标签，这里自己跳到列位。
        //宽度是项目配置，只有原生这一处持有，托管侧不必跟着同步一个数。
        ImGui::SameLine(propertyLabelWidth);
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
        ImTextureID texture = EditorIcons::Get(iconName, lineHeight);
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

        //单击与双击分开上报：双击才定位，单击不把人从当前对象上带走。
        //双击必须在第二次「按下」那一帧判定——MouseClickedCount 每帧开头就被清零，
        //而 InvisibleButton 的 clicked 到「松开」才为真，那时计数已经归零了，两者凑不到一帧。
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) action = 4;
        else if (clicked) action = 1;
        ImGui::PopID();
        return action;
    }

    //瓦片的底板、选中圈与图标；只绘制不参与命中，命中由 SubmitTileHit 单独提交
    void DrawTileSurface(const std::string& iconName, const ImVec2& min, const ImVec2& max,
        bool selected, bool hovered, bool held, float32 iconSize)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (selected || hovered || held)
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered),
                style.FrameRounding);
        if (selected)
            drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_ButtonActive), style.FrameRounding);

        ImTextureID texture = EditorIcons::Get(iconName, iconSize);
        if (texture != 0)
        {
            float32 iconLeft = min.x + (max.x - min.x - iconSize) * 0.5f;
            float32 iconTop = min.y + style.FramePadding.y;
            drawList->AddImage(texture, ImVec2(iconLeft, iconTop), ImVec2(iconLeft + iconSize, iconTop + iconSize));
        }
    }

    //提交整块瓦片的命中区；后提交的控件优先，所以重命名时它排在输入框后面
    bool SubmitTileHit(const ImVec2& min, const ImVec2& max, bool& hovered, bool& held)
    {
        bool clicked = ImGui::InvisibleButton("##asset_tile", ImVec2(max.x - min.x, max.y - min.y));
        hovered = ImGui::IsItemHovered();
        held = ImGui::IsItemActive();
        return clicked;
    }

    //行内重命名输入框的结果码：0 继续编辑、1 回车、2 失焦、3 Esc
    int32 RenameInputResult(bool committed)
    {
        if (committed) return 1;
        if (!ImGui::IsItemDeactivated()) return 0;
        return ImGui::IsKeyPressed(ImGuiKey_Escape) ? 3 : 2;
    }

    //瓦片图标尺寸随瓦片宽度缩放，缩放滑条才能同时改变格子与图标。
    //0.375 让默认格子宽（96）算出的图标与原先的 GetFrameHeight() * 1.5 一致
    float32 TileIconSize(float32 width)
    {
        return std::max(width * 0.375f, ImGui::GetFrameHeight());
    }

    //重命名中的资源瓦片：图标照画，名称那一行就地换成输入框。
    //网格是一行一个 SameLine 单元格、一个单元格只能放一个控件，所以图标与输入框必须由同一次调用画出来
    int32 ORBEDEN_NATIVE_CALL EditorGuiAssetRenameTile(const uint8* icon, int32 iconLength,
        const uint8* id, int32 idLength, uint8* text, int32 capacity, uint8* focusRequested,
        float32 width, uint8 selected)
    {
        if (!text || capacity <= 0) return 0;

        ImGuiStyle& style = ImGui::GetStyle();
        std::string iconName = ReadUtf8Text(icon, iconLength);
        float32 iconSize = TileIconSize(width);
        float32 height = style.FramePadding.y * 2.0f + iconSize + style.ItemSpacing.y + ImGui::GetTextLineHeight();

        ImGui::PushID(ReadUtf8Text(id, idLength).c_str());
        ImVec2 min = ImGui::GetCursorScreenPos();
        ImVec2 max { min.x + width, min.y + height };

        //先铺瓦片底板与图标，再画输入框，输入框才不会被底色盖住（选中底色是铺满整格的）
        DrawTileSurface(iconName, min, max, selected != 0, false, false, iconSize);

        //输入框位置正是名称那一行，所以换进换出名字落在同一行上
        if (focusRequested && *focusRequested != 0)
        {
            ImGui::SetKeyboardFocusHere();
            *focusRequested = 0;
        }
        float32 textTop = min.y + style.FramePadding.y + iconSize + style.ItemSpacing.y;
        ImGui::SetCursorScreenPos(ImVec2(min.x + style.FramePadding.x, textTop));
        ImGui::SetNextItemWidth(width - style.FramePadding.x * 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 0.0f));
        bool committed = ImGui::InputText("##rename", reinterpret_cast<char*>(text),
            static_cast<usize>(capacity), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleVar();
        int32 result = RenameInputResult(committed);

        //命中区最后提交：谁后提交谁没被先提交的控件占住才拿到点击，
        //这样鼠标在输入框上时不会被整块瓦片的按钮抢走
        ImGui::SetCursorScreenPos(min);
        bool hovered = false, held = false;
        SubmitTileHit(min, max, hovered, held);

        ImGui::PopID();
        return result;
    }

    //绘制资源瓦片：options 为 1 选中、2 可展开、4 已展开；返回 1 点击、2 切换展开、4 箭头悬停
    uint8 ORBEDEN_NATIVE_CALL EditorGuiAssetTile(const uint8* icon, int32 iconLength,
        const uint8* label, int32 labelLength, const uint8* id, int32 idLength,
        float32 width, uint8 options)
    {
        std::string iconName = ReadUtf8Text(icon, iconLength);
        std::string text = ReadUtf8Text(label, labelLength);

        ImGuiStyle& style = ImGui::GetStyle();
        float32 iconSize = TileIconSize(width);
        float32 height = style.FramePadding.y * 2.0f + iconSize + style.ItemSpacing.y + ImGui::GetTextLineHeight();

        ImGui::PushID(ReadUtf8Text(id, idLength).c_str());
        ImVec2 min = ImGui::GetCursorScreenPos();
        ImVec2 max { min.x + width, min.y + height };
        bool hovered = false, held = false;
        bool clicked = SubmitTileHit(min, max, hovered, held);
        DrawTileSurface(iconName, min, max, (options & 1) != 0, hovered, held, iconSize);

        //名称居中，放不下的名字截断补省略号，悬停时用提示给出全名
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float32 textTop = min.y + style.FramePadding.y + iconSize + style.ItemSpacing.y;
        float32 available = width - style.FramePadding.x * 2.0f;
        std::string display = EllipsizeToWidth(text, available);
        float32 textWidth = ImGui::CalcTextSize(display.c_str()).x;
        float32 textLeft = min.x + style.FramePadding.x + std::max(0.0f, (available - textWidth) * 0.5f);
        drawList->PushClipRect(min, max, true);
        drawList->AddText(ImVec2(textLeft, textTop), ImGui::GetColorU32(ImGuiCol_Text), display.c_str());
        drawList->PopClipRect();
        if (hovered && display != text) ImGui::SetTooltip("%s", text.c_str());

        //绘制文件展开箭头并在同一瓦片命中区内区分点击
        bool arrowHovered = false;
        if (options & 2)
        {
            float32 side = ImGui::GetFrameHeight();
            ImVec2 arrowMin(min.x, min.y);
            ImVec2 arrowMax(min.x + side, min.y + side);
            arrowHovered = hovered && ImGui::IsMouseHoveringRect(arrowMin, arrowMax);
            ImVec2 center(min.x + side * 0.5f, min.y + side * 0.5f);
            float32 radius = ImGui::GetFontSize() * 0.22f;
            ImU32 color = ImGui::GetColorU32(arrowHovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            if (options & 4)
                drawList->AddTriangleFilled(ImVec2(center.x - radius, center.y - radius * 0.5f),
                    ImVec2(center.x + radius, center.y - radius * 0.5f), ImVec2(center.x, center.y + radius), color);
            else
                drawList->AddTriangleFilled(ImVec2(center.x - radius * 0.5f, center.y - radius),
                    ImVec2(center.x - radius * 0.5f, center.y + radius), ImVec2(center.x + radius, center.y), color);
        }
        ImGui::PopID();
        return (clicked && !arrowHovered ? 1 : 0) | (clicked && arrowHovered ? 2 : 0) | (arrowHovered ? 4 : 0);
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
    // 换到同一行；offset 大于 0 时按该偏移定位，用来把控件贴到行的右侧
    void ORBEDEN_NATIVE_CALL EditorGuiSameLine(float32 offset)
    {
        if (offset > 0.0f) ImGui::SameLine(offset);
        else ImGui::SameLine();
    }

    // 绘制开关按钮：开启时用强调色底，比勾选框更适合紧凑工具条
    uint8 ORBEDEN_NATIVE_CALL EditorGuiToggleButton(const uint8* text, int32 length, uint8 active)
    {
        std::string label = ReadUtf8Text(text, length);
        bool on = active != 0;

        ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(on ? theme.active : theme.control));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(on ? theme.active : theme.hovered));
        bool pressed = ImGui::Button(label.c_str());
        ImGui::PopStyleColor(2);
        return pressed ? 1 : 0;
    }

    struct InspectorListLayout
    {
        ImVec2 start;
        float32 width;
        bool expanded;
        bool table;
        int32 action;
    };
    thread_local List<InspectorListLayout> inspectorLists;

    /// <summary>绘制列表标题、数量和增删按钮，展开时建立元素区域。</summary>
    int32 ORBEDEN_NATIVE_CALL EditorGuiBeginList(const uint8* label, int32 length, int32* count, int32 flags)
    {
        std::string text = ReadUtf8Text(label, length);
        ImGui::PushID(text.c_str());
        ImGui::BeginGroup();
        ImVec2 start = ImGui::GetCursorScreenPos();
        float32 width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        float32 height = ImGui::GetFrameHeight() + 6.0f;
        float32 button = ImGui::GetFrameHeight();
        float32 actionsWidth = button * 2.0f + 8.0f;
        float32 sizeWidth = std::min(width * 0.3f, ImGui::GetFontSize() * 3.5f);
        ImGuiID openId = ImGui::GetID("open");
        bool expanded = ImGui::GetStateStorage()->GetBool(openId, true);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(start, ImVec2(start.x + width, start.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
        if (ImGui::InvisibleButton("header", ImVec2(std::max(1.0f, width - sizeWidth - actionsWidth - 12.0f), height)))
        {
            expanded = !expanded;
            ImGui::GetStateStorage()->SetBool(openId, expanded);
        }
        ImGui::RenderArrow(draw, ImVec2(start.x + 7.0f, start.y + (height - ImGui::GetFontSize()) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), expanded ? ImGuiDir_Down : ImGuiDir_Right, 0.8f);
        ImGui::RenderTextClipped(ImVec2(start.x + 24.0f, start.y), ImVec2(start.x + width - sizeWidth - actionsWidth - 14.0f, start.y + height),
            text.c_str(), ImGui::FindRenderedTextEnd(text.c_str()), nullptr, ImVec2(0.0f, 0.5f));
        ImGui::SetCursorScreenPos(ImVec2(start.x + width - sizeWidth - actionsWidth - 6.0f, start.y + 3.0f));
        ImGui::SetNextItemWidth(sizeWidth);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, (flags & 2) != 0);
        ImGui::InputInt("##size", count, 0, 0, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Size (read only)");
        int32 action = 0;
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        ImGui::BeginDisabled((flags & 1) == 0);
        if (ImGui::Button("+", ImVec2(button, button))) action = 1;
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::BeginDisabled((flags & 4) == 0);
        if (ImGui::Button("-", ImVec2(button, button))) action = 2;
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height));
        bool table = false;
        if (expanded)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 4.0f));
            table = ImGui::BeginTable("elements", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings, ImVec2(width, 0.0f));
            if (table)
            {
                ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.2f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                if (*count == 0)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_WindowBg));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("List is empty");
                }
            }
        }
        inspectorLists.push_back({start, width, expanded, table, action});
        return table ? 1 : 0;
    }

    /// <summary>绘制紧凑拖动柄和索引，整行使用一致底色。</summary>
    uint8 ORBEDEN_NATIVE_CALL EditorGuiListElement(int32 index, uint8 selected)
    {
        ImGui::TableNextRow();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_WindowBg));
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(index);
        ImVec2 start = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::Selectable("##handle", selected != 0, 0, ImVec2(0.0f, ImGui::GetFrameHeight()));
        float32 center = start.y + ImGui::GetFrameHeight() * 0.5f;
        ImU32 ink = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        for (int32 row = -1; row <= 1; ++row)
            for (int32 column = 0; column < 2; ++column)
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(start.x + 3.0f + column * 4.0f, center + row * 4.0f), 1.0f, ink);
        std::string number = std::to_string(index);
        ImGui::GetWindowDrawList()->AddText(ImVec2(start.x + 16.0f, center - ImGui::GetFontSize() * 0.5f), ink, number.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Element %d - drag to reorder", index);
        ImGui::PopID();
        return clicked ? 1 : 0;
    }

    /// <summary>收拢列表边框，释放绘制状态并返回标题栏操作。</summary>
    int32 ORBEDEN_NATIVE_CALL EditorGuiEndList()
    {
        InspectorListLayout layout = inspectorLists.back();
        inspectorLists.pop_back();
        if (layout.table) ImGui::EndTable();
        if (layout.expanded) ImGui::PopStyleVar();
        if (layout.table)
        {
            ImGui::SetCursorScreenPos(ImVec2(layout.start.x, ImGui::GetItemRectMax().y));
        }
        ImGui::GetWindowDrawList()->AddRect(layout.start, ImVec2(layout.start.x + layout.width, ImGui::GetCursorScreenPos().y),
            ImGui::GetColorU32(ImGuiCol_Border), 3.0f);
        ImGui::Dummy(ImVec2(layout.width, 4.0f));
        ImGui::EndGroup();
        ImGui::PopID();
        return layout.action;
    }

    /// <summary>为自定义编辑器控件建立独立身份空间。</summary>
    void ORBEDEN_NATIVE_CALL EditorGuiPushId(const uint8* text, int32 length) { ImGui::PushID(ReadUtf8Text(text, length).c_str()); }

    /// <summary>结束控件身份空间。</summary>
    void ORBEDEN_NATIVE_CALL EditorGuiPopId() { ImGui::PopID(); }

    //开始表格
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginTable(const uint8* id, int32 length, int32 columns, uint8 scroll)
    {
        if (columns <= 0) return 0;

        std::string value = ReadUtf8Text(id, length);
        ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_Resizable
            | ImGuiTableFlags_SizingStretchProp;
        if (scroll) flags |= ImGuiTableFlags_ScrollY;
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

    //开始子菜单，返回是否展开；展开时调用方必须配对 EndMenu
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginMenu(const uint8* label, int32 length, uint8 enabled)
    {
        std::string value = ReadUtf8Text(label, length);
        return ImGui::BeginMenu(value.c_str(), enabled != 0) ? 1 : 0;
    }

    //结束子菜单
    void ORBEDEN_NATIVE_CALL EditorGuiEndMenu()
    {
        ImGui::EndMenu();
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

    //批量矩形绘制单元，字段顺序与托管侧一一对应，全部拍平成 float32
    struct EditorRectPrimitive
    {
        float32 minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
        float32 r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
        float32 rounding = 0.0f;
    };
    static_assert(sizeof(EditorRectPrimitive) == 36);

    //绘制带颜色文本
    void ORBEDEN_NATIVE_CALL EditorGuiTextColored(const color* value, const uint8* text, int32 length)
    {
        std::string body = ReadUtf8Text(text, length);
        if (!value)
        {
            ImGui::TextUnformatted(body.c_str());
            return;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(*value));
        ImGui::TextUnformatted(body.c_str());
        ImGui::PopStyleColor();
    }

    //绘制自动换行文本
    void ORBEDEN_NATIVE_CALL EditorGuiTextWrapped(const uint8* text, int32 length)
    {
        std::string body = ReadUtf8Text(text, length);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(body.c_str());
        ImGui::PopTextWrapPos();
    }

    //把滚动位置移到当前光标处
    void ORBEDEN_NATIVE_CALL EditorGuiSetScrollHereY(float32 ratio) { ImGui::SetScrollHereY(ratio); }

    //读取内容区剩余空间
    void ORBEDEN_NATIVE_CALL EditorGuiGetContentRegionAvail(vector2* size)
    {
        if (!size) return;

        ImVec2 available = ImGui::GetContentRegionAvail();
        size->x = available.x;
        size->y = available.y;
    }

    //读取屏幕坐标下的光标位置
    void ORBEDEN_NATIVE_CALL EditorGuiGetCursorScreenPos(vector2* position)
    {
        if (!position) return;

        ImVec2 cursor = ImGui::GetCursorScreenPos();
        position->x = cursor.x;
        position->y = cursor.y;
    }

    //批量绘制实心矩形，绘制内容由当前窗口裁剪
    void ORBEDEN_NATIVE_CALL EditorGuiDrawRects(const EditorRectPrimitive* rects, int32 count)
    {
        if (!rects || count <= 0) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        for (int32 index = 0; index < count; index++)
        {
            const EditorRectPrimitive& rect = rects[index];
            if (rect.maxX <= rect.minX || rect.maxY <= rect.minY) continue;

            drawList->AddRectFilled(ImVec2(rect.minX, rect.minY), ImVec2(rect.maxX, rect.maxY),
                ImGui::GetColorU32(ImVec4(rect.r, rect.g, rect.b, rect.a)), rect.rounding);
        }
    }

    //绘制一条折线，坐标与裁剪矩形都在屏幕空间
    void ORBEDEN_NATIVE_CALL EditorGuiDrawPolyline(const vector2* points, int32 count, const color* tint,
        float32 thickness, const vector2* clipMin, const vector2* clipMax)
    {
        if (!points || !tint || count < 2 || count > 4096) return;
        //厚度与颜色都必须可用，否则整条线不画
        if (!std::isfinite(thickness) || thickness <= 0.0f) return;
        if (!std::isfinite(tint->r) || !std::isfinite(tint->g) || !std::isfinite(tint->b) || !std::isfinite(tint->a)) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        bool clipped = clipMin && clipMax && clipMax->x > clipMin->x && clipMax->y > clipMin->y;
        if (clipped) drawList->PushClipRect(ImVec2(clipMin->x, clipMin->y), ImVec2(clipMax->x, clipMax->y), true);

        //原生不保留指针，所有坐标在本次调用内转换
        drawList->AddPolyline(reinterpret_cast<const ImVec2*>(points), count,
            ImGui::GetColorU32(ImVec4(tint->r, tint->g, tint->b, tint->a)), ImDrawFlags_None, thickness);

        if (clipped) drawList->PopClipRect();
    }

    //当前控件是否正在被拖动
    uint8 ORBEDEN_NATIVE_CALL EditorGuiIsItemActive()
    {
        return ImGui::IsItemActive() ? 1 : 0;
    }

    //指定鼠标键是否按下，只接受 0..2
    uint8 ORBEDEN_NATIVE_CALL EditorGuiIsMouseDown(int32 button)
    {
        if (button < 0 || button > 2) return 0;
        return ImGui::IsMouseDown(static_cast<ImGuiMouseButton>(button)) ? 1 : 0;
    }

    //在屏幕位置绘制资源图标并应用指定颜色
    //画一张引擎纹理：解析对象身份，取它的 GPU 纹理句柄交给 ImGui。
    //预览输出是线性预乘纹理，ImGui 按直通 Alpha 混合，半透明处会偏暗，仅供预览定位。
    void ORBEDEN_NATIVE_CALL EditorGuiDrawTexture(int32 textureObjectId, float32 width, float32 height)
    {
        if (textureObjectId == 0 || width <= 0.0f || height <= 0.0f) return;

        Object* object = Object::FindObjectById(textureObjectId);
        Texture2D* texture = object ? object->Cast<Texture2D>() : nullptr;
        if (!texture) return;

        GpuTextureID gpu = RenderSystem::Current() ? RenderSystem::Current()->GetTextureId(texture) : GpuTextureID();
        if (!gpu.IsValid()) return;

        if (texture->IsRenderTarget())
            ImGui::Image(static_cast<ImTextureID>(gpu.id), ImVec2(width, height), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        else
            ImGui::Image(static_cast<ImTextureID>(gpu.id), ImVec2(width, height));
    }

    void ORBEDEN_NATIVE_CALL EditorGuiDrawIcon(const uint8* name, int32 length, const vector2* position,
        float32 size, const color* tint)
    {
        if (!position || !tint || !std::isfinite(size) || size <= 0.0f) return;
        ImTextureID texture = EditorIcons::Get(ReadUtf8Text(name, length), size);
        if (texture == 0) return;
        ImGui::GetWindowDrawList()->AddImage(texture, ImVec2(position->x, position->y),
            ImVec2(position->x + size, position->y + size), ImVec2(0, 0), ImVec2(1, 1),
            ImGui::GetColorU32(ToImVec4(*tint)));
    }

    //在指定位置绘制被裁剪的文本
    void ORBEDEN_NATIVE_CALL EditorGuiDrawTextClipped(const vector2* clipMin, const vector2* clipMax,
        const vector2* position, const color* value, const uint8* text, int32 length, float32 fontSize)
    {
        if (!clipMin || !clipMax || !position) return;

        std::string body = ReadUtf8Text(text, length);
        ImU32 textColor = value ? ImGui::GetColorU32(ToImVec4(*value)) : ImGui::GetColorU32(ImGuiCol_Text);

        ImGui::PushClipRect(ImVec2(clipMin->x, clipMin->y), ImVec2(clipMax->x, clipMax->y), true);
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), fontSize > 0.0f ? fontSize : ImGui::GetFontSize(),
            ImVec2(position->x, position->y), textColor, body.c_str());
        ImGui::PopClipRect();
    }

    //预留一块可交互空白区域
    uint8 ORBEDEN_NATIVE_CALL EditorGuiInvisibleButton(const uint8* id, int32 length, const vector2* size)
    {
        if (!size) return 0;

        //尺寸必须为正，否则 ImGui 断言
        ImVec2 area(std::max(size->x, 1.0f), std::max(size->y, 1.0f));
        return ImGui::InvisibleButton(ReadUtf8Text(id, length).c_str(), area) ? 1 : 0;
    }

    //把光标挪到屏幕坐标，下一个条目就落在那里
    void ORBEDEN_NATIVE_CALL EditorGuiSetCursorScreenPos(const vector2* position)
    {
        if (!position) return;
        ImGui::SetCursorScreenPos(ImVec2(position->x, position->y));
    }

    //判断上一个条目是否悬停
    uint8 ORBEDEN_NATIVE_CALL EditorGuiIsItemHovered() { return ImGui::IsItemHovered() ? 1 : 0; }

    //判断上一个条目是否被点击
    uint8 ORBEDEN_NATIVE_CALL EditorGuiIsItemClicked() { return ImGui::IsItemClicked() ? 1 : 0; }

    //读取当前鼠标位置
    void ORBEDEN_NATIVE_CALL EditorGuiGetMousePos(vector2* position)
    {
        if (!position) return;

        ImVec2 mouse = ImGui::GetIO().MousePos;
        position->x = mouse.x;
        position->y = mouse.y;
    }

    //显示单行提示
    void ORBEDEN_NATIVE_CALL EditorGuiSetTooltip(const uint8* text, int32 length)
    {
        ImGui::SetTooltip("%s", ReadUtf8Text(text, length).c_str());
    }

    //绘制只读多行文本，支持框选复制
    int32 ORBEDEN_NATIVE_CALL EditorGuiInputTextMultiline(const uint8* label, int32 labelLength,
        uint8* text, int32 capacity, float32 height, uint8 readOnly)
    {
        if (!text || capacity <= 0) return 0;

        std::string labelText = ReadUtf8Text(label, labelLength);
        const std::string control = BeginPropertyRow(labelText);
        const bool columned = control.length() != 0;
        ImGuiInputTextFlags flags = readOnly != 0 ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
        return ImGui::InputTextMultiline(columned ? control.c_str() : labelText.c_str(), reinterpret_cast<char*>(text),
            static_cast<usize>(capacity), ImVec2(columned ? -FLT_MIN : -1.0f, height), flags) ? 1 : 0;
    }

    //读取本帧的鼠标滚轮增量
    float32 ORBEDEN_NATIVE_CALL EditorGuiGetMouseWheel() { return ImGui::GetIO().MouseWheel; }

    //绘制行内重命名输入框：首帧自动聚焦并全选。
    //返回 0 继续编辑、1 回车、2 失焦、3 Esc（文本已由框架还原）。
    //回车与失焦分开报，调用方才能在名称非法时区别处理：回车留在原地改，失焦只能放弃。
    int32 ORBEDEN_NATIVE_CALL EditorGuiRenameInput(const uint8* id, int32 idLength,
        uint8* text, int32 capacity, uint8* focusRequested, float32 width)
    {
        if (!text || capacity <= 0) return 0;

        //必须先请求聚焦，再绘制输入框
        if (focusRequested && *focusRequested != 0)
        {
            ImGui::SetKeyboardFocusHere();
            *focusRequested = 0;
        }

        //宽度 <= 0 时占满本行剩余宽度：树节点与表格单元里，那就是原来名称的位置
        ImGui::SetNextItemWidth(width > 0.0f ? width : -1.0f);
        //输入框高度是 FontSize + 2*FramePadding.y，树节点与表格行只有 FontSize；
        //纵向内边距归零后两者严格等高，换进换出不会顶动整行
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
            ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
        std::string label = ReadUtf8Text(id, idLength);
        bool committed = ImGui::InputText(label.c_str(), reinterpret_cast<char*>(text),
            static_cast<usize>(capacity), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleVar();
        return RenameInputResult(committed);
    }

    //判断当前窗口是否拥有焦点。
    //必须带 ChildWindows：不加时要求 NavWindow 与当前窗口完全相等，
    //而面板里真正拿到焦点的往往是内容区里的子窗口（列表、滚动区）。
    uint8 ORBEDEN_NATIVE_CALL EditorGuiIsWindowFocused()
    {
        return ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) ? 1 : 0;
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

    //先按默认配置把图集建起来（内置点阵）；项目打开后 ApplyFontConfig 会按项目配置重建。
    //显式建一次而不是交给 ImGui 的惰性默认，编辑器启动时的字体来源才只有一处。
    mainFontAtlas = io.Fonts;
    BuildEditorFont(*mainFontAtlas);
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

    //界面字号。这一项每个上下文各写一份（浮动窗有自己的 ImGuiStyle，图集则是共享的），
    //所以放在 ApplyTheme 里而不是只在重建图集时设一次。内置点阵只按 13 算。
    style.FontSizeBase = editorFontIsBitmap ? BitmapFontSize : editorFontSize;
}

//设置属性行的标签列宽度
void EditorGUI::SetPropertyLabelWidth(float32 value)
{
    if (!(value > 0.0f) || !std::isfinite(value)) return;
    propertyLabelWidth = value;
    //同一个数也要给 OrbedenCore 的颜色字段，它的原语在那边。直接调 core 的普通函数，
    //不走任何函数表——这条路径只在编辑器进程里有意义，Player 里颜色字段保持自己的默认值。
    RuntimeGuiBridge::SetPropertyLabelWidth(value);
}

//按项目配置重建编辑器字体图集
void EditorGUI::ApplyFontConfig(const std::string& font, float32 fontSize)
{
    //先把配置记下来：还没初始化时也要留住，Initialize 建图集时才有得用
    editorFont = font;
    editorFontIsBitmap = font.empty();
    editorFontSize = editorFontIsBitmap ? BitmapFontSize : std::max(8.0f, fontSize);

    if (!initialized || !context || !mainFontAtlas) return;

    //图集属于主上下文：重建要在它的上下文里做。浮动窗的上下文共享同一张图集，
    //ImGui 会在 ClearFonts 时把它们一起改绑到新字体上。
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context);
    BuildEditorFont(*mainFontAtlas);
    if (previous && previous != context) ImGui::SetCurrentContext(previous);
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

//获取面板边框色：只有拥有焦点的面板才用强调色，其余用这个
ImU32 EditorGUI::GetBorderColor()
{
    return ImGui::GetColorU32(ToImVec4(theme.border));
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
    {
        PROFILE("Render/EditorGui/SubmitDraw");
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
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
    api.inputVector2 = reinterpret_cast<void*>(&EditorGuiInputVector2);
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
    api.textColored = reinterpret_cast<void*>(&EditorGuiTextColored);
    api.textWrapped = reinterpret_cast<void*>(&EditorGuiTextWrapped);
    api.setScrollHereY = reinterpret_cast<void*>(&EditorGuiSetScrollHereY);
    api.getContentRegionAvail = reinterpret_cast<void*>(&EditorGuiGetContentRegionAvail);
    api.getCursorScreenPos = reinterpret_cast<void*>(&EditorGuiGetCursorScreenPos);
    api.drawRects = reinterpret_cast<void*>(&EditorGuiDrawRects);
    api.drawTextClipped = reinterpret_cast<void*>(&EditorGuiDrawTextClipped);
    api.invisibleButton = reinterpret_cast<void*>(&EditorGuiInvisibleButton);
    api.isItemHovered = reinterpret_cast<void*>(&EditorGuiIsItemHovered);
    api.isItemClicked = reinterpret_cast<void*>(&EditorGuiIsItemClicked);
    api.getMousePos = reinterpret_cast<void*>(&EditorGuiGetMousePos);
    api.setTooltip = reinterpret_cast<void*>(&EditorGuiSetTooltip);
    api.inputTextMultiline = reinterpret_cast<void*>(&EditorGuiInputTextMultiline);
    api.getMouseWheel = reinterpret_cast<void*>(&EditorGuiGetMouseWheel);
    api.isWindowFocused = reinterpret_cast<void*>(&EditorGuiIsWindowFocused);
    api.toggleButton = reinterpret_cast<void*>(&EditorGuiToggleButton);
    api.renameInput = reinterpret_cast<void*>(&EditorGuiRenameInput);
    api.beginDialog = reinterpret_cast<void*>(&EditorGuiBeginDialog);
    api.assetRenameTile = reinterpret_cast<void*>(&EditorGuiAssetRenameTile);
    api.sliderFloat = reinterpret_cast<void*>(&EditorGuiSliderFloat);
    api.calcButtonWidth = reinterpret_cast<void*>(&EditorGuiCalcButtonWidth);
    api.beginMenu = reinterpret_cast<void*>(&EditorGuiBeginMenu);
    api.endMenu = reinterpret_cast<void*>(&EditorGuiEndMenu);
    api.beginList = reinterpret_cast<void*>(&EditorGuiBeginList);
    api.listElement = reinterpret_cast<void*>(&EditorGuiListElement);
    api.endList = reinterpret_cast<void*>(&EditorGuiEndList);
    api.pushId = reinterpret_cast<void*>(&EditorGuiPushId);
    api.popId = reinterpret_cast<void*>(&EditorGuiPopId);
    api.drawPolyline = reinterpret_cast<void*>(&EditorGuiDrawPolyline);
    api.isItemActive = reinterpret_cast<void*>(&EditorGuiIsItemActive);
    api.isMouseDown = reinterpret_cast<void*>(&EditorGuiIsMouseDown);
    api.drawIcon = reinterpret_cast<void*>(&EditorGuiDrawIcon);
    api.drawTexture = reinterpret_cast<void*>(&EditorGuiDrawTexture);
    api.setCursorScreenPos = reinterpret_cast<void*>(&EditorGuiSetCursorScreenPos);
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
