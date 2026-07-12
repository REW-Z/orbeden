#include "Editor/EditorProject.h"

#include "Application.h"
#include "Editor/ExampleWorldGenerator.h"
#include "FileSystem/PathDefines.h"
#include "Log/Log.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/ResourceManager.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
    std::string ToCleanPath(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        std::ostringstream output;
        output << input.rdbuf();
        return output.str();
    }

    //写入文本文件。
    bool WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output) return false;

        output << text;
        return true;
    }

    //转义 XML 属性文本。
    std::string EscapeXml(const std::string& text)
    {
        std::string result;
        result.reserve(text.size());
        for (char ch : text)
        {
            if (ch == '&') result += "&amp;";
            else if (ch == '<') result += "&lt;";
            else if (ch == '>') result += "&gt;";
            else if (ch == '"') result += "&quot;";
            else if (ch == '\'') result += "&apos;";
            else result += ch;
        }

        return result;
    }

    //转换浮点数为紧凑文本。
    std::string ToFloatText(float32 value)
    {
        std::ostringstream output;
        output << std::setprecision(6) << value;
        return output.str();
    }

    std::string GetAttribute(const std::string& text, const std::string& name)
    {
        std::string pattern = name + "=";
        std::size_t position = text.find(pattern);
        if (position == std::string::npos) return std::string();

        position += pattern.size();
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
        {
            position++;
        }

        if (position >= text.size() || (text[position] != '"' && text[position] != '\'')) return std::string();
        char quote = text[position++];
        std::size_t valueStart = position;
        while (position < text.size() && text[position] != quote)
        {
            position++;
        }

        return position < text.size() ? text.substr(valueStart, position - valueStart) : std::string();
    }

    //判断 XML 片段是否包含属性。
    bool HasAttribute(const std::string& text, const std::string& name)
    {
        return text.find(name + "=") != std::string::npos;
    }

    //读取浮点属性。
    float32 GetFloatAttribute(const std::string& text, const std::string& name, float32 defaultValue)
    {
        std::string value = GetAttribute(text, name);
        if (value.empty()) return defaultValue;

        char* end = nullptr;
        float result = std::strtof(value.c_str(), &end);
        return end != value.c_str() ? result : defaultValue;
    }

    //读取布尔属性。
    bool GetBoolAttribute(const std::string& text, const std::string& name, bool defaultValue)
    {
        std::string value = GetAttribute(text, name);
        if (value.empty()) return defaultValue;

        return value == "true" || value == "1";
    }

    //读取整数属性。
    int32 GetIntAttribute(const std::string& text, const std::string& name, int32 defaultValue)
    {
        std::string value = GetAttribute(text, name);
        if (value.empty()) return defaultValue;

        char* end = nullptr;
        long result = std::strtol(value.c_str(), &end, 10);
        return end != value.c_str() ? static_cast<int32>(result) : defaultValue;
    }

    //读取编辑器布局块。
    void ReadEditorLayout(const std::string& content, EditorLayoutState& layout)
    {
        layout = EditorLayoutState();

        std::size_t layoutStart = content.find("<EditorLayout");
        if (layoutStart == std::string::npos) return;

        std::size_t layoutEnd = content.find("</EditorLayout>", layoutStart);
        if (layoutEnd == std::string::npos) return;

        std::string block = content.substr(layoutStart, layoutEnd - layoutStart);
        std::size_t layoutTokenEnd = block.find('>');
        if (layoutTokenEnd != std::string::npos)
        {
            std::string layoutToken = block.substr(0, layoutTokenEnd + 1);
            layout.dockRoot = GetIntAttribute(layoutToken, "dockRoot", -1);
        }

        //读取面板布局。
        std::size_t panelPosition = 0;
        while ((panelPosition = block.find("<Panel", panelPosition)) != std::string::npos)
        {
            std::size_t panelEnd = block.find('>', panelPosition);
            if (panelEnd == std::string::npos) break;

            std::string panelToken = block.substr(panelPosition, panelEnd - panelPosition + 1);
            EditorPanelState panel;
            panel.id = GetAttribute(panelToken, "id");
            panel.visible = GetBoolAttribute(panelToken, "visible", true);
            panel.hasPosition = HasAttribute(panelToken, "x") && HasAttribute(panelToken, "y");
            panel.hasSize = HasAttribute(panelToken, "width") && HasAttribute(panelToken, "height");
            panel.position.x = GetFloatAttribute(panelToken, "x", 0.0f);
            panel.position.y = GetFloatAttribute(panelToken, "y", 0.0f);
            panel.size.x = GetFloatAttribute(panelToken, "width", 0.0f);
            panel.size.y = GetFloatAttribute(panelToken, "height", 0.0f);
            panel.dockNode = GetIntAttribute(panelToken, "dockNode", -1);
            if (!panel.id.empty())
            {
                layout.panels.push_back(panel);
            }

            panelPosition = panelEnd + 1;
        }

        //读取停靠树。
        std::size_t dockPosition = 0;
        while ((dockPosition = block.find("<DockNode", dockPosition)) != std::string::npos)
        {
            std::size_t dockEnd = block.find('>', dockPosition);
            if (dockEnd == std::string::npos) break;

            std::string dockToken = block.substr(dockPosition, dockEnd - dockPosition + 1);
            EditorDockNodeState node;
            node.id = GetIntAttribute(dockToken, "id", 0);
            node.firstChild = GetIntAttribute(dockToken, "first", -1);
            node.secondChild = GetIntAttribute(dockToken, "second", -1);
            node.vertical = GetBoolAttribute(dockToken, "vertical", true);
            node.ratio = GetFloatAttribute(dockToken, "ratio", 0.5f);
            node.workspace = GetBoolAttribute(dockToken, "workspace", false);
            node.activePanel = GetAttribute(dockToken, "active");
            if (node.id > 0) layout.dockNodes.push_back(node);
            dockPosition = dockEnd + 1;
        }

        //读取编辑器相机布局。
        std::size_t cameraPosition = block.find("<EditorCamera");
        if (cameraPosition != std::string::npos)
        {
            std::size_t cameraEnd = block.find('>', cameraPosition);
            if (cameraEnd != std::string::npos)
            {
                std::string cameraToken = block.substr(cameraPosition, cameraEnd - cameraPosition + 1);
                layout.editorCamera.hasValue = true;
                layout.editorCamera.position.x = GetFloatAttribute(cameraToken, "x", layout.editorCamera.position.x);
                layout.editorCamera.position.y = GetFloatAttribute(cameraToken, "y", layout.editorCamera.position.y);
                layout.editorCamera.position.z = GetFloatAttribute(cameraToken, "z", layout.editorCamera.position.z);
                layout.editorCamera.yaw = GetFloatAttribute(cameraToken, "yaw", layout.editorCamera.yaw);
                layout.editorCamera.pitch = GetFloatAttribute(cameraToken, "pitch", layout.editorCamera.pitch);
            }
        }
    }

    //移除旧编辑器布局块。
    std::string RemoveEditorLayoutBlock(std::string content)
    {
        std::size_t layoutStart = content.find("<EditorLayout");
        if (layoutStart == std::string::npos) return content;

        std::size_t eraseStart = layoutStart;
        while (eraseStart > 0 && (content[eraseStart - 1] == ' ' || content[eraseStart - 1] == '\t'))
        {
            eraseStart--;
        }
        if (eraseStart > 0 && content[eraseStart - 1] == '\n')
        {
            eraseStart--;
            if (eraseStart > 0 && content[eraseStart - 1] == '\r')
            {
                eraseStart--;
            }
        }

        std::size_t layoutEnd = content.find("</EditorLayout>", layoutStart);
        if (layoutEnd != std::string::npos)
        {
            layoutEnd += std::strlen("</EditorLayout>");
        }
        else
        {
            layoutEnd = content.find('>', layoutStart);
            if (layoutEnd == std::string::npos) return content;
            layoutEnd++;
        }

        while (layoutEnd < content.size() && (content[layoutEnd] == '\r' || content[layoutEnd] == '\n'))
        {
            layoutEnd++;
        }

        content.erase(eraseStart, layoutEnd - eraseStart);
        return content;
    }

    //写出编辑器布局块文本。
    std::string BuildEditorLayoutBlock(const EditorLayoutState& layout)
    {
        std::ostringstream output;
        output << "    <EditorLayout dockRoot=\"" << layout.dockRoot << "\">\n";
        for (const EditorPanelState& panel : layout.panels)
        {
            output << "        <Panel id=\"" << EscapeXml(panel.id)
                << "\" visible=\"" << (panel.visible ? "true" : "false")
                << "\" x=\"" << ToFloatText(panel.position.x)
                << "\" y=\"" << ToFloatText(panel.position.y)
                << "\" width=\"" << ToFloatText(panel.size.x)
                << "\" height=\"" << ToFloatText(panel.size.y)
                << "\" dockNode=\"" << panel.dockNode
                << "\" />\n";
        }

        for (const EditorDockNodeState& node : layout.dockNodes)
        {
            output << "        <DockNode id=\"" << node.id
                << "\" first=\"" << node.firstChild
                << "\" second=\"" << node.secondChild
                << "\" vertical=\"" << (node.vertical ? "true" : "false")
                << "\" ratio=\"" << ToFloatText(node.ratio)
                << "\" workspace=\"" << (node.workspace ? "true" : "false")
                << "\" active=\"" << EscapeXml(node.activePanel)
                << "\" />\n";
        }

        if (layout.editorCamera.hasValue)
        {
            output << "        <EditorCamera x=\"" << ToFloatText(layout.editorCamera.position.x)
                << "\" y=\"" << ToFloatText(layout.editorCamera.position.y)
                << "\" z=\"" << ToFloatText(layout.editorCamera.position.z)
                << "\" yaw=\"" << ToFloatText(layout.editorCamera.yaw)
                << "\" pitch=\"" << ToFloatText(layout.editorCamera.pitch)
                << "\" />\n";
        }

        output << "    </EditorLayout>\n";
        return output.str();
    }

    //写入编辑器布局到项目文件。
    bool WriteEditorLayoutToProjectFile(const std::filesystem::path& projectFile, const EditorLayoutState& layout)
    {
        std::string content = RemoveEditorLayoutBlock(ReadTextFile(projectFile));
        std::size_t rootStart = content.find("<OrbedenProject");
        if (rootStart == std::string::npos) return false;

        std::size_t rootEnd = content.find('>', rootStart);
        if (rootEnd == std::string::npos) return false;

        std::size_t lastRootChar = rootEnd;
        while (lastRootChar > rootStart && std::isspace(static_cast<unsigned char>(content[lastRootChar - 1])))
        {
            lastRootChar--;
        }

        std::string layoutBlock = BuildEditorLayoutBlock(layout);
        bool selfClosing = lastRootChar > rootStart && content[lastRootChar - 1] == '/';
        if (selfClosing)
        {
            content.erase(lastRootChar - 1, 1);
            rootEnd--;
            std::size_t insertPosition = rootEnd + 1;
            while (insertPosition < content.size() && (content[insertPosition] == '\r' || content[insertPosition] == '\n'))
            {
                content.erase(insertPosition, 1);
            }

            content.insert(insertPosition, "\n" + layoutBlock + "</OrbedenProject>\n");
            return WriteTextFile(projectFile, content);
        }

        std::size_t closePosition = content.rfind("</OrbedenProject>");
        if (closePosition == std::string::npos) return false;

        std::string insertText = layoutBlock;
        if (closePosition > 0 && content[closePosition - 1] != '\n')
        {
            insertText = "\n" + insertText;
        }

        content.insert(closePosition, insertText);
        return WriteTextFile(projectFile, content);
    }

    std::string FindProjectFileInFolder(const std::filesystem::path& folder)
    {
        if (!std::filesystem::is_directory(folder)) return std::string();

        std::filesystem::path expected = folder / (folder.filename().string() + ".oeproj");
        if (std::filesystem::exists(expected)) return ToCleanPath(expected);

        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error))
        {
            if (error) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".oeproj")
            {
                return ToCleanPath(entry.path());
            }
        }

        return std::string();
    }
}

EditorProject::EditorProject(Application& application)
    : app(application)
{
}

bool EditorProject::LoadProjectFolder(const std::string& folder)
{
    std::string projectFile = FindProjectFileInFolder(std::filesystem::path(folder));
    if (projectFile.empty())
    {
        lastError = "Project folder does not contain a .oeproj file: " + folder;
        Log::Error(lastError.c_str());
        return false;
    }

    return LoadProjectFile(projectFile);
}

bool EditorProject::LoadProjectFile(const std::string& projectFile)
{
    std::filesystem::path filePath(projectFile);
    if (!std::filesystem::exists(filePath))
    {
        lastError = "Project file does not exist: " + projectFile;
        Log::Error(lastError.c_str());
        return false;
    }

    std::string content = ReadTextFile(filePath);
    std::string parsedName = GetAttribute(content, "name");
    std::string parsedStartupWorld = GetAttribute(content, "startupWorld");
    std::string parsedResourceRoot = GetAttribute(content, "resourceRoot");
    std::string parsedScriptRoot = GetAttribute(content, "scriptRoot");
    std::string parsedManagedRoot = GetAttribute(content, "managedRoot");
    EditorLayoutState parsedLayout;
    ReadEditorLayout(content, parsedLayout);
    if (parsedStartupWorld.empty())
    {
        lastError = "Project file is missing startupWorld: " + projectFile;
        Log::Error(lastError.c_str());
        return false;
    }

    std::string parsedProjectRoot = ToCleanPath(std::filesystem::absolute(filePath.parent_path()));
    if (parsedName.empty()) parsedName = filePath.parent_path().filename().string();
    if (parsedResourceRoot.empty()) parsedResourceRoot = "Resource";
    if (parsedScriptRoot.empty()) parsedScriptRoot = "Script";
    if (parsedManagedRoot.empty()) parsedManagedRoot = "Managed";
    bool useExampleWorldGenerator = ExampleWorldGenerator::IsExampleProject(parsedName);
    if (useExampleWorldGenerator)
    {
        parsedStartupWorld = "World/example_world.world";
        parsedResourceRoot = "Resource";
        parsedScriptRoot = "Script";
        parsedManagedRoot = "Managed";
    }

    std::string worldPath = ToCleanPath(std::filesystem::path(parsedProjectRoot) / parsedStartupWorld);
    if (useExampleWorldGenerator && !ExampleWorldGenerator::GenerateProjectFiles(parsedProjectRoot))
    {
        lastError = "Example project generation failed: " + parsedProjectRoot;
        Log::Error(lastError.c_str());
        return false;
    }

    RenderSystem* renderSystem = app.GetRenderSystem();
    if (renderSystem)
    {
        renderSystem->PrepareProjectReload();
    }

    app.GetWorld().Clear();
    ResourceManager::Shutdown();
    PathDefines::SetProjectRoot(parsedProjectRoot, parsedResourceRoot);

    bool loaded = app.LoadWorld(worldPath);
    if (renderSystem)
    {
        renderSystem->CompleteProjectReload();
    }

    projectRoot = parsedProjectRoot;
    projectName = parsedName;
    resourceRoot = parsedResourceRoot;
    scriptRoot = parsedScriptRoot;
    managedRoot = parsedManagedRoot;
    startupWorld = parsedStartupWorld;
    projectFilePath = ToCleanPath(std::filesystem::absolute(filePath));
    editorLayout = parsedLayout;
    lastError.clear();

    if (!loaded)
    {
        lastError = "Project loaded, but startup world failed: " + worldPath;
        Log::Error(lastError.c_str());
        return false;
    }

    if (useExampleWorldGenerator)
    {
        ExampleWorldGenerator::ApplyRuntimeEnvironment(app);
    }

    Log::Info(("Project loaded: " + projectName).c_str());
    return true;
}

bool EditorProject::SaveStartupWorld()
{
    if (!HasProject())
    {
        lastError = "No project is open.";
        Log::Error(lastError.c_str());
        return false;
    }

    std::string worldPath = GetStartupWorldPath();
    if (worldPath.empty())
    {
        lastError = "Project startup world is empty.";
        Log::Error(lastError.c_str());
        return false;
    }

    if (!app.SaveWorld(worldPath))
    {
        lastError = "World save failed: " + worldPath;
        Log::Error(lastError.c_str());
        return false;
    }

    lastError.clear();
    Log::Info(("World saved: " + worldPath).c_str());
    return true;
}

//重新读取项目启动场景
bool EditorProject::ReloadStartupWorld()
{
    if (!HasProject())
    {
        lastError = "No project is open.";
        Log::Error(lastError.c_str());
        return false;
    }

    std::string worldPath = GetStartupWorldPath();
    if (worldPath.empty())
    {
        lastError = "Project startup world is empty.";
        Log::Error(lastError.c_str());
        return false;
    }

    RenderSystem* renderSystem = app.GetRenderSystem();
    if (renderSystem)
    {
        renderSystem->PrepareProjectReload();
    }

    app.GetWorld().Clear();
    ResourceManager::Shutdown();
    PathDefines::SetProjectRoot(projectRoot, resourceRoot);

    bool loaded = app.LoadWorld(worldPath);
    if (renderSystem)
    {
        renderSystem->CompleteProjectReload();
    }

    if (!loaded)
    {
        lastError = "Startup world reload failed: " + worldPath;
        Log::Error(lastError.c_str());
        return false;
    }

    if (ExampleWorldGenerator::IsExampleProject(projectName))
    {
        ExampleWorldGenerator::ApplyRuntimeEnvironment(app);
    }

    lastError.clear();
    Log::Info(("Startup world reloaded: " + worldPath).c_str());
    return true;
}

//保存编辑器布局状态到项目文件
bool EditorProject::SaveEditorLayout(const EditorLayoutState& layout)
{
    if (!HasProject() || projectFilePath.empty())
    {
        lastError = "No project is open.";
        Log::Error(lastError.c_str());
        return false;
    }

    if (!WriteEditorLayoutToProjectFile(std::filesystem::path(projectFilePath), layout))
    {
        lastError = "Editor layout save failed: " + projectFilePath;
        Log::Error(lastError.c_str());
        return false;
    }

    editorLayout = layout;
    lastError.clear();
    return true;
}

//获取编辑器布局状态
const EditorLayoutState& EditorProject::GetEditorLayout() const
{
    return editorLayout;
}

const std::string& EditorProject::GetProjectRoot() const
{
    return projectRoot;
}

const std::string& EditorProject::GetProjectName() const
{
    return projectName;
}

std::string EditorProject::GetScriptRootPath() const
{
    if (projectRoot.empty() || scriptRoot.empty()) return std::string();
    return ToCleanPath(std::filesystem::path(projectRoot) / scriptRoot);
}

std::string EditorProject::GetManagedRootPath() const
{
    if (projectRoot.empty() || managedRoot.empty()) return std::string();
    return ToCleanPath(std::filesystem::path(projectRoot) / managedRoot);
}

std::string EditorProject::GetStartupWorldPath() const
{
    if (projectRoot.empty() || startupWorld.empty()) return std::string();
    return ToCleanPath(std::filesystem::path(projectRoot) / startupWorld);
}

//获取项目文件完整路径
const std::string& EditorProject::GetProjectFilePath() const
{
    return projectFilePath;
}

const std::string& EditorProject::GetLastError() const
{
    return lastError;
}

bool EditorProject::HasProject() const
{
    return !projectRoot.empty();
}
