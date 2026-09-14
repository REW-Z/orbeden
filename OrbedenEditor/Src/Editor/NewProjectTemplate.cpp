#include "Editor/NewProjectTemplate.h"

#include "Editor/ProjectLayout.h"
#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace
{
    //项目脚手架与示例内容在模板根下的子目录名。
    constexpr const char* ProjectFolder = "Project";
    constexpr const char* ExamplesFolder = "Examples";

    std::string ToCleanPath(const std::filesystem::path& path)
    {
        return Utf8Path::ToUtf8(path.lexically_normal());
    }

    /// <summary>仅已知文本格式忽略行尾差异，其余资源按原始字节比较。</summary>
    bool IsScannableTextFile(const std::filesystem::path& path)
    {
        static const std::unordered_set<std::string> TextExtensions =
        {
            ".cs", ".cpp", ".h", ".hpp", ".c", ".inl", ".txt", ".md", ".json", ".xml",
            ".world", ".oeproj", ".csproj", ".vcxproj", ".props", ".targets", ".filters",
            ".obj", ".mtl", ".orbshader", ".orbinc", ".glsl", ".vert", ".frag",
        };

        std::string extension = Utf8Path::ToUtf8(path.extension());
        for (char& character : extension) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        return TextExtensions.count(extension) != 0;
    }

    bool ReadWholeFile(const std::filesystem::path& path, std::string& outText)
    {
        std::ifstream input(path, std::ios::in | std::ios::binary);
        if (!input) return false;

        std::ostringstream buffer;
        buffer << input.rdbuf();
        outText = buffer.str();
        return true;
    }

    //逐字节比较两个文件，用来判断镜像时是否需要重写。
    bool FilesEqualBytes(const std::filesystem::path& left, const std::filesystem::path& right)
    {
        std::error_code sizeError;
        std::uintmax_t leftSize = std::filesystem::file_size(left, sizeError);
        std::uintmax_t rightSize = std::filesystem::file_size(right, sizeError);
        if (sizeError || leftSize != rightSize) return false;

        std::ifstream leftStream(left, std::ios::in | std::ios::binary);
        std::ifstream rightStream(right, std::ios::in | std::ios::binary);
        if (!leftStream || !rightStream) return false;

        constexpr std::size_t ChunkSize = 64 * 1024;
        std::string leftBuffer(ChunkSize, '\0');
        std::string rightBuffer(ChunkSize, '\0');
        while (true)
        {
            leftStream.read(leftBuffer.data(), static_cast<std::streamsize>(ChunkSize));
            rightStream.read(rightBuffer.data(), static_cast<std::streamsize>(ChunkSize));

            std::streamsize leftRead = leftStream.gcount();
            if (leftRead != rightStream.gcount()) return false;
            if (leftRead <= 0) break;
            if (leftBuffer.compare(0, static_cast<std::size_t>(leftRead),
                    rightBuffer, 0, static_cast<std::size_t>(leftRead)) != 0)
            {
                return false;
            }
        }

        return true;
    }

    //把 \r\n 折成 \n，孤立出现的 \r 保持原样。
    std::string NormalizeLineEndings(const std::string& text)
    {
        if (text.find('\r') == std::string::npos) return text;

        std::string normalized;
        normalized.reserve(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            if (text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n') continue;
            normalized.push_back(text[index]);
        }

        return normalized;
    }

    //比较两个文件的内容，用来判断镜像时是否需要重写。
    //文本文件忽略行尾差异：core.autocrlf 会在 checkout 时把 LF 换成 CRLF，
    //只比字节的话，一次 checkout 就能把整个模板报成"全都改过"。
    bool FilesEqual(const std::filesystem::path& left, const std::filesystem::path& right)
    {
        //字节完全相同时直接返回；仅已知文本格式进一步忽略行尾。
        if (FilesEqualBytes(left, right)) return true;

        if (!IsScannableTextFile(left)) return false;

        std::string leftText;
        std::string rightText;
        if (!ReadWholeFile(left, leftText) || !ReadWholeFile(right, rightText)) return false;
        return NormalizeLineEndings(leftText) == NormalizeLineEndings(rightText);
    }

    //原样复制二进制模板文件。
    bool CopyBinaryFile(const std::filesystem::path& source, const std::filesystem::path& target)
    {
        if (target.has_parent_path())
        {
            std::filesystem::create_directories(target.parent_path());
        }

        std::error_code error;
        std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
        if (error)
        {
            Log::Error(("Template file copy failed: " + ToCleanPath(target) + ": " + error.message()).c_str());
            return false;
        }

        return true;
    }

    //模板中文件名随项目名变化的映射。
    //模板内容里没有占位符，所以只有文件改名，没有内容替换。
    std::filesystem::path MapTemplateFileName(const std::filesystem::path& relativePath, const std::string& projectName)
    {
        //工程文件直接放在项目根，只有这几个名字随项目名变化。
        std::string fileName = Utf8Path::ToUtf8(relativePath);
        if (fileName == "Project.oeproj") return Utf8Path::FromUtf8(projectName + ".oeproj");
        if (fileName == "Project.csproj") return Utf8Path::FromUtf8(projectName + ".csproj");
        if (fileName == "GameNative.vcxproj") return Utf8Path::FromUtf8(projectName + "Native.vcxproj");
        return relativePath;
    }
}

//把一棵模板树复制到目标目录，逐字节复制，只按规则给工程文件改名
bool NewProjectTemplate::CopyTemplateTree(const std::string& sourceDirectory,
    const std::string& targetDirectory,
    const std::string& projectName,
    std::string& outError,
    bool preserveProjectContent)
{
    outError.clear();

    std::filesystem::path sourceRoot = Utf8Path::FromUtf8(sourceDirectory);
    if (!std::filesystem::is_directory(sourceRoot))
    {
        outError = "Template directory was not found: " + ToCleanPath(sourceRoot);
        Log::Error(outError.c_str());
        return false;
    }

    std::filesystem::path targetRoot = Utf8Path::FromUtf8(targetDirectory);
    std::error_code error;
    bool succeeded = true;
    std::filesystem::recursive_directory_iterator iterator(sourceRoot, error);
    std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end)
    {
        const std::filesystem::directory_entry& entry = *iterator;
        if (entry.is_regular_file())
        {
            std::filesystem::path relative = entry.path().lexically_relative(sourceRoot);
            std::filesystem::path target = targetRoot / MapTemplateFileName(relative, projectName);
            //升级只更新脚手架，保留项目配置和全部用户内容。
            bool preserve = preserveProjectContent
                && (relative == "Project.oeproj" || *relative.begin() == ProjectLayout::ContentFolder);
            if (!preserve) succeeded = CopyBinaryFile(entry.path(), target) && succeeded;
        }

        iterator.increment(error);
    }

    if (error)
    {
        outError = "Template copy failed: " + error.message();
        Log::Error(outError.c_str());
        return false;
    }

    if (!succeeded)
    {
        outError = "Template copy failed: " + ToCleanPath(targetRoot);
        return false;
    }

    return true;
}

//把 sourceDirectory 镜像到 targetDirectory
bool NewProjectTemplate::MirrorTree(const std::string& sourceDirectory,
    const std::string& targetDirectory,
    MirrorReport& outReport,
    std::string& outError) try
{
    outError.clear();
    outReport = MirrorReport();

    std::filesystem::path sourceRoot = Utf8Path::FromUtf8(sourceDirectory);
    if (!std::filesystem::is_directory(sourceRoot))
    {
        outError = "Mirror source directory was not found: " + ToCleanPath(sourceRoot);
        Log::Error(outError.c_str());
        return false;
    }

    std::filesystem::path targetRoot = std::filesystem::weakly_canonical(Utf8Path::FromUtf8(targetDirectory));
    sourceRoot = std::filesystem::canonical(sourceRoot);
    for (const auto& roots : { std::make_pair(sourceRoot, targetRoot), std::make_pair(targetRoot, sourceRoot) })
    {
        for (std::filesystem::path parent = roots.first; !parent.empty(); parent = parent.parent_path())
        {
            if (std::filesystem::exists(parent) && std::filesystem::exists(roots.second)
                && std::filesystem::equivalent(parent, roots.second))
            {
                outError = "Mirror source and target directories must not overlap.";
                return false;
            }
            if (parent == parent.parent_path()) break;
        }
    }

    for (const std::filesystem::path& root : { sourceRoot, targetRoot })
    {
        if (!std::filesystem::exists(root)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        {
            if (entry.is_symlink())
            {
                outError = "Mirror does not support symbolic links: " + ToCleanPath(entry.path());
                return false;
            }
        }
    }

    std::error_code error;
    List<std::filesystem::path> sourceFiles;
    for (std::filesystem::recursive_directory_iterator iterator(sourceRoot, error), end; !error && iterator != end; iterator.increment(error))
    {
        if (!iterator->is_regular_file()) continue;

        std::filesystem::path relative = iterator->path().lexically_relative(sourceRoot);
        sourceFiles.push_back(relative);
    }

    if (error)
    {
        outError = "Mirror scan failed: " + error.message();
        Log::Error(outError.c_str());
        return false;
    }

    //源为空多半是把示例删光了，而不是真想清空模板。
    if (sourceFiles.empty())
    {
        outError = "Mirror source directory is empty: " + ToCleanPath(sourceRoot);
        Log::Error(outError.c_str());
        return false;
    }

    for (const std::filesystem::path& relative : sourceFiles)
    {
        std::filesystem::path source = sourceRoot / relative;
        std::filesystem::path target = targetRoot / relative;

        std::error_code existsError;
        bool existed = std::filesystem::exists(target, existsError);
        if (existsError)
        {
            outError = "Mirror target access failed: " + existsError.message();
            return false;
        }
        //内容相同的文件不重写：报告才有意义，也避免无谓地刷新时间戳。
        if (!existed || !FilesEqual(source, target))
        {
            if (!CopyBinaryFile(source, target))
            {
                outError = "Mirror copy failed: " + ToCleanPath(target);
                return false;
            }

            if (existed) outReport.updated++;
            else outReport.added++;
        }
    }

    //目标里源已经不存在的文件要删掉，否则在示例里删掉的文件会永远留在模板里。
    if (std::filesystem::is_directory(targetRoot))
    {
        List<std::filesystem::path> stale;
        for (std::filesystem::recursive_directory_iterator iterator(targetRoot, error), end; !error && iterator != end; iterator.increment(error))
        {
            if (!iterator->is_regular_file()) continue;
            std::filesystem::path source = sourceRoot / iterator->path().lexically_relative(targetRoot);
            bool exists = std::filesystem::exists(source, error);
            if (error) break;
            if (!exists) stale.push_back(iterator->path());
        }

        if (error)
        {
            outError = "Mirror target scan failed: " + error.message();
            return false;
        }

        for (const std::filesystem::path& path : stale)
        {
            std::error_code removeError;
            if (!std::filesystem::remove(path, removeError) || removeError)
            {
                outError = "Mirror remove failed: " + ToCleanPath(path);
                return false;
            }

            outReport.removed++;
        }
    }

    return true;
}

catch (const std::filesystem::filesystem_error& error)
{
    outError = "Mirror failed: " + std::string(error.what());
    return false;
}

//把脚手架与示例铺到项目目录
bool NewProjectTemplate::GenerateProjectFiles(const std::string& projectRoot,
    const std::string& projectName,
    const std::string& templateRoot,
    std::string& outError,
    bool preserveProjectContent)
{
    outError.clear();

    std::filesystem::path root = Utf8Path::FromUtf8(templateRoot);
    if (!std::filesystem::is_directory(root))
    {
        outError = "Project template directory was not found: " + ToCleanPath(root);
        Log::Error(outError.c_str());
        return false;
    }

    std::filesystem::path projectRootPath = Utf8Path::FromUtf8(projectRoot);
    if (!CopyTemplateTree(ToCleanPath(root / ProjectFolder), projectRoot, projectName, outError, preserveProjectContent)) return false;

    //仅新建项目初始化示例；升级不覆盖用户已修改或删除的内容。
    std::filesystem::path examplesRoot = root / ExamplesFolder;
    if (!preserveProjectContent && std::filesystem::is_directory(examplesRoot))
    {
        std::filesystem::path targetRoot = projectRootPath / ProjectLayout::ContentFolder / ExamplesFolder;
        if (!CopyTemplateTree(ToCleanPath(examplesRoot), ToCleanPath(targetRoot), projectName, outError)) return false;
    }

    Log::Info(("New project template generated: " + ToCleanPath(projectRootPath)).c_str());
    return true;
}
