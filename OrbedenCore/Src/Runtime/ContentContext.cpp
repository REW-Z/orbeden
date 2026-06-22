#include "Runtime/ContentContext.h"

#include "FileSystem/FileSystem.h"

#include <algorithm>
#include <filesystem>

namespace
{
    struct ContentContextRuntime
    {
    public:
        std::string contentRoot;
        std::string resourceRoot = "Resource";
    };

    ContentContextRuntime& GetRuntime()
    {
        static ContentContextRuntime runtime;
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

void ContentContext::SetContentRoot(const std::string& root, const std::string& resourceRoot)
{
    ContentContextRuntime& runtime = GetRuntime();
    runtime.contentRoot = NormalizePath(AbsolutePath(root));
    runtime.resourceRoot = NormalizePath(std::filesystem::path(resourceRoot.empty() ? "Resource" : resourceRoot));
}

void ContentContext::Clear()
{
    ContentContextRuntime& runtime = GetRuntime();
    runtime.contentRoot.clear();
    runtime.resourceRoot = "Resource";
}

bool ContentContext::HasContentRoot()
{
    return !GetRuntime().contentRoot.empty();
}

const std::string& ContentContext::GetContentRoot()
{
    return GetRuntime().contentRoot;
}

const std::string& ContentContext::GetResourceRoot()
{
    return GetRuntime().resourceRoot;
}

std::string ContentContext::ResolveContentPath(const std::string& path)
{
    std::filesystem::path filePath(path);
    if (filePath.is_absolute()) return NormalizePath(filePath);

    const ContentContextRuntime& runtime = GetRuntime();
    if (runtime.contentRoot.empty()) return NormalizePath(filePath);

    return NormalizePath(std::filesystem::path(runtime.contentRoot) / filePath);
}

std::string ContentContext::ResolveResourcePath(const std::string& path)
{
    std::string normalizedPath = NormalizePath(std::filesystem::path(path));
    const ContentContextRuntime& runtime = GetRuntime();
    if (runtime.contentRoot.empty()) return normalizedPath;

    if (normalizedPath == runtime.resourceRoot)
    {
        return NormalizePath(std::filesystem::path(runtime.contentRoot) / runtime.resourceRoot);
    }

    std::string resourcePrefix = runtime.resourceRoot + "/";
    if (StartsWith(normalizedPath, resourcePrefix))
    {
        std::string relativeResource = normalizedPath.substr(resourcePrefix.size());
        return NormalizePath(std::filesystem::path(runtime.contentRoot) / runtime.resourceRoot / relativeResource);
    }

    return ResolveContentPath(normalizedPath);
}

std::string ContentContext::FindProjectRoot(const std::string& projectDirectoryName, const std::string& executablePath)
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
