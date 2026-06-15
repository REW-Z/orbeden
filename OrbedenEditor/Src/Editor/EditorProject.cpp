#include "Editor/EditorProject.h"

#include "Application.h"
#include "Editor/ExampleWorldGenerator.h"
#include "Log/Log.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/ProjectContext.h"
#include "Runtime/ResourceManager.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    std::string NormalizePath(const std::filesystem::path& path)
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

    std::string FindProjectFileInFolder(const std::filesystem::path& folder)
    {
        if (!std::filesystem::is_directory(folder)) return std::string();

        std::filesystem::path expected = folder / (folder.filename().string() + ".oeproj");
        if (std::filesystem::exists(expected)) return NormalizePath(expected);

        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error))
        {
            if (error) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".oeproj")
            {
                return NormalizePath(entry.path());
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
    if (parsedStartupWorld.empty())
    {
        lastError = "Project file is missing startupWorld: " + projectFile;
        Log::Error(lastError.c_str());
        return false;
    }

    std::string parsedProjectRoot = NormalizePath(std::filesystem::absolute(filePath.parent_path()));
    if (parsedName.empty()) parsedName = filePath.parent_path().filename().string();
    if (parsedResourceRoot.empty()) parsedResourceRoot = "Resources";
    std::string worldPath = NormalizePath(std::filesystem::path(parsedProjectRoot) / parsedStartupWorld);

    bool useExampleWorldGenerator = ExampleWorldGenerator::IsExampleProject(parsedName);
    if (useExampleWorldGenerator && !ExampleWorldGenerator::GenerateWorldFile(parsedProjectRoot, parsedStartupWorld))
    {
        lastError = "Example project world generation failed: " + worldPath;
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
    ProjectContext::SetProjectRoot(parsedProjectRoot, parsedResourceRoot);

    bool loaded = app.LoadWorld(worldPath);
    if (renderSystem)
    {
        renderSystem->CompleteProjectReload();
    }

    projectRoot = parsedProjectRoot;
    projectName = parsedName;
    resourceRoot = parsedResourceRoot;
    startupWorld = parsedStartupWorld;
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

const std::string& EditorProject::GetProjectRoot() const
{
    return projectRoot;
}

const std::string& EditorProject::GetProjectName() const
{
    return projectName;
}

std::string EditorProject::GetStartupWorldPath() const
{
    if (projectRoot.empty() || startupWorld.empty()) return std::string();
    return NormalizePath(std::filesystem::path(projectRoot) / startupWorld);
}

const std::string& EditorProject::GetLastError() const
{
    return lastError;
}

bool EditorProject::HasProject() const
{
    return !projectRoot.empty();
}
