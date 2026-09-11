#include "FileSystem/PathDefines.h"

#include "FileSystem/Utf8Path.h"

#include <filesystem>

namespace
{
    struct PathDefinesRuntime
    {
    public:
        std::string contentRoot;
    };

    PathDefinesRuntime& GetRuntime()
    {
        static PathDefinesRuntime runtime;
        return runtime;
    }

    std::string ToCleanPath(const std::filesystem::path& path)
    {
        std::string value = Utf8Path::ToUtf8(path.lexically_normal());
        while (value.size() > 1 && value.back() == '/')
        {
            value.pop_back();
        }

        return value;
    }

    std::filesystem::path AbsolutePath(const std::string& path)
    {
        std::filesystem::path filePath = Utf8Path::FromUtf8(path);
        return filePath.is_absolute() ? filePath : std::filesystem::absolute(filePath);
    }

}

void PathDefines::SetContentRoot(const std::string& root)
{
    GetRuntime().contentRoot = ToCleanPath(AbsolutePath(root));
}

void PathDefines::Clear()
{
    GetRuntime().contentRoot.clear();
}

bool PathDefines::HasContentRoot()
{
    return !GetRuntime().contentRoot.empty();
}

const std::string& PathDefines::GetContentRoot()
{
    return GetRuntime().contentRoot;
}

std::string PathDefines::GetContentFilePath(const std::string& path)
{
    std::filesystem::path filePath = Utf8Path::FromUtf8(path);
    if (filePath.is_absolute()) return ToCleanPath(filePath);

    const PathDefinesRuntime& runtime = GetRuntime();
    if (runtime.contentRoot.empty()) return ToCleanPath(filePath);

    return ToCleanPath(Utf8Path::FromUtf8(runtime.contentRoot) / filePath);
}
