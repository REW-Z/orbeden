#include "Runtime/ProjectContext.h"

#include "FileSystem/FileSystem.h"

#include <algorithm>
#include <filesystem>

namespace
{
    struct ProjectContextRuntime
    {
    public:
        std::string projectRoot;
        std::string resourceRoot = "Resources";
    };

    ProjectContextRuntime& GetRuntime()
    {
        static ProjectContextRuntime runtime;
        return runtime;
    }

    bool StartsWith(const std::string& text, const std::string& prefix)
    {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    std::string NormalizePath(const std::filesystem::path& path)
    {
        std::string value = path.lexically_normal().generic_string();
        while (value.size() > 1 && value.back() == '/')
        {
            value.pop_back();
        }

        return value;
    }

    std::filesystem::path AbsolutePath(const std::string& path)
    {
        std::filesystem::path filePath(path);
        return filePath.is_absolute() ? filePath : std::filesystem::absolute(filePath);
    }

    bool IsProjectRoot(const std::filesystem::path& path, const std::string& projectDirectoryName)
    {
        std::filesystem::path projectFile = path / (projectDirectoryName + ".oeproj");
        return FileSystem::Exist(NormalizePath(projectFile));
    }

    std::string SearchProjectRootFrom(const std::filesystem::path& start, const std::string& projectDirectoryName)
    {
        std::filesystem::path current = start;
        if (!std::filesystem::is_directory(current))
        {
            current = current.parent_path();
        }

        while (!current.empty())
        {
            if (IsProjectRoot(current, projectDirectoryName))
            {
                return NormalizePath(current);
            }

            std::filesystem::path child = current / projectDirectoryName;
            if (IsProjectRoot(child, projectDirectoryName))
            {
                return NormalizePath(child);
            }

            std::filesystem::path parent = current.parent_path();
            if (parent == current) break;
            current = parent;
        }

        return std::string();
    }
}

void ProjectContext::SetProjectRoot(const std::string& root, const std::string& resourceRoot)
{
    ProjectContextRuntime& runtime = GetRuntime();
    runtime.projectRoot = NormalizePath(AbsolutePath(root));
    runtime.resourceRoot = NormalizePath(std::filesystem::path(resourceRoot.empty() ? "Resources" : resourceRoot));
}

void ProjectContext::Clear()
{
    ProjectContextRuntime& runtime = GetRuntime();
    runtime.projectRoot.clear();
    runtime.resourceRoot = "Resources";
}

bool ProjectContext::HasProject()
{
    return !GetRuntime().projectRoot.empty();
}

const std::string& ProjectContext::GetProjectRoot()
{
    return GetRuntime().projectRoot;
}

const std::string& ProjectContext::GetResourceRoot()
{
    return GetRuntime().resourceRoot;
}

std::string ProjectContext::ResolveProjectPath(const std::string& path)
{
    std::filesystem::path filePath(path);
    if (filePath.is_absolute()) return NormalizePath(filePath);

    const ProjectContextRuntime& runtime = GetRuntime();
    if (runtime.projectRoot.empty()) return NormalizePath(filePath);

    return NormalizePath(std::filesystem::path(runtime.projectRoot) / filePath);
}

std::string ProjectContext::ResolveResourcePath(const std::string& path)
{
    std::string normalizedPath = NormalizePath(std::filesystem::path(path));
    const ProjectContextRuntime& runtime = GetRuntime();
    if (runtime.projectRoot.empty()) return normalizedPath;

    if (normalizedPath == "Resources")
    {
        return NormalizePath(std::filesystem::path(runtime.projectRoot) / runtime.resourceRoot);
    }

    if (StartsWith(normalizedPath, "Resources/"))
    {
        std::string relativeResource = normalizedPath.substr(10);
        return NormalizePath(std::filesystem::path(runtime.projectRoot) / runtime.resourceRoot / relativeResource);
    }

    return ResolveProjectPath(normalizedPath);
}

std::string ProjectContext::FindProjectRoot(const std::string& projectDirectoryName, const std::string& executablePath)
{
    if (projectDirectoryName.empty()) return std::string();

    std::string found = SearchProjectRootFrom(std::filesystem::current_path(), projectDirectoryName);
    if (!found.empty()) return found;

    if (!executablePath.empty())
    {
        found = SearchProjectRootFrom(AbsolutePath(executablePath), projectDirectoryName);
        if (!found.empty()) return found;
    }

    return std::string();
}
