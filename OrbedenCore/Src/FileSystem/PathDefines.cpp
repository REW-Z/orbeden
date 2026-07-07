#include "FileSystem/PathDefines.h"

#include "FileSystem/FileSystem.h"

#include <filesystem>

namespace
{
    struct PathDefinesRuntime
    {
    public:
        std::string projectRoot;
        std::string resourceRoot = "Resource";
    };

    PathDefinesRuntime& GetRuntime()
    {
        static PathDefinesRuntime runtime;
        return runtime;
    }

    bool StartsWith(const std::string& text, const std::string& prefix)
    {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    std::string ToCleanPath(const std::filesystem::path& path)
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
        return FileSystem::Exist(ToCleanPath(projectFile));
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
                return ToCleanPath(current);
            }

            std::filesystem::path child = current / projectDirectoryName;
            if (IsProjectRoot(child, projectDirectoryName))
            {
                return ToCleanPath(child);
            }

            std::filesystem::path parent = current.parent_path();
            if (parent == current) break;
            current = parent;
        }

        return std::string();
    }
}

void PathDefines::SetProjectRoot(const std::string& root, const std::string& resourceRoot)
{
    PathDefinesRuntime& runtime = GetRuntime();
    runtime.projectRoot = ToCleanPath(AbsolutePath(root));
    runtime.resourceRoot = ToCleanPath(std::filesystem::path(resourceRoot.empty() ? "Resource" : resourceRoot));
}

void PathDefines::Clear()
{
    PathDefinesRuntime& runtime = GetRuntime();
    runtime.projectRoot.clear();
    runtime.resourceRoot = "Resource";
}

bool PathDefines::HasProjectRoot()
{
    return !GetRuntime().projectRoot.empty();
}

const std::string& PathDefines::GetProjectRoot()
{
    return GetRuntime().projectRoot;
}

const std::string& PathDefines::GetResourceRoot()
{
    return GetRuntime().resourceRoot;
}

std::string PathDefines::GetProjectFilePath(const std::string& path)
{
    std::filesystem::path filePath(path);
    if (filePath.is_absolute()) return ToCleanPath(filePath);

    const PathDefinesRuntime& runtime = GetRuntime();
    if (runtime.projectRoot.empty()) return ToCleanPath(filePath);

    return ToCleanPath(std::filesystem::path(runtime.projectRoot) / filePath);
}

std::string PathDefines::GetResourceFilePath(const std::string& path)
{
    std::string cleanPath = ToCleanPath(std::filesystem::path(path));
    const PathDefinesRuntime& runtime = GetRuntime();
    if (runtime.projectRoot.empty()) return cleanPath;

    if (cleanPath == runtime.resourceRoot)
    {
        return ToCleanPath(std::filesystem::path(runtime.projectRoot) / runtime.resourceRoot);
    }

    std::string resourcePrefix = runtime.resourceRoot + "/";
    if (StartsWith(cleanPath, resourcePrefix))
    {
        std::string relativeResource = cleanPath.substr(resourcePrefix.size());
        return ToCleanPath(std::filesystem::path(runtime.projectRoot) / runtime.resourceRoot / relativeResource);
    }

    return GetProjectFilePath(cleanPath);
}

std::string PathDefines::FindProjectRoot(const std::string& projectDirectoryName, const std::string& executablePath)
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
