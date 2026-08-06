#include "Editor/EditorGUI.h"

#include "Log/Log.h"
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
    //读取 UTF-8 文本
    std::string ReadUtf8Text(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return std::string();
        return std::string(reinterpret_cast<const char*>(text), static_cast<usize>(length));
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

    //开始组件块
    void ORBEDEN_NATIVE_CALL EditorGuiBeginComponentBlock(const uint8* title, int32 length)
    {
        std::string value = ReadUtf8Text(title, length);
        if (value.empty()) value = "Component";

        ImGui::Spacing();
        ImGui::PushID(value.c_str());
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::BeginChild("##component",
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
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
    uint8 ORBEDEN_NATIVE_CALL EditorGuiBeginCollapsibleComponentBlock(const uint8* title,
        int32 titleLength,
        const uint8* id,
        int32 idLength,
        uint8 removable,
        uint8* removeRequested)
    {
        std::string value = ReadUtf8Text(title, titleLength);
        std::string identity = ReadUtf8Text(id, idLength);
        if (value.empty()) value = "Component";
        if (identity.empty()) identity = value;
        if (removeRequested) *removeRequested = 0;

        ImGui::Spacing();
        ImGui::PushID(identity.c_str());
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::BeginChild("##component",
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        bool visible = true;
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
    activeInstance = nullptr;

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

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
}

void EditorGUI::Render()
{
    if (!initialized) return;

    ImGui::SetCurrentContext(context);
    ImGui::Render();
    UpdateMouseCursor();

    int32 framebufferWidth = 0;
    int32 framebufferHeight = 0;
    glfwGetFramebufferSize(glfwWindow, &framebufferWidth, &framebufferHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
