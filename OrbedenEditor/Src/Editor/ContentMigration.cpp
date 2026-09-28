#include "Editor/ContentMigration.h"

#include "Defines/Version.h"
#include "Log/Log.h"
#include "FileSystem/Utf8Path.h"

#include <filesystem>
#include <fstream>

namespace
{
    //读入整个文本文件；失败返回 false
    bool ReadTextFile(const std::filesystem::path& path, std::string& outText)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.good()) return false;
        outText.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return true;
    }

    //以二进制写回，保持原有的行尾
    bool WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream output(path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!output.good()) return false;
        output << text;
        return output.good();
    }

    //把一处 <Field name="enableInstancing" .../> 改写为 drawStrategy：
    //true 与缺省都迁移成 Auto，false 迁移成 Individual，保持原作者的意图。
    bool RewriteInstancingField(std::string& text, int32& rewrittenFields)
    {
        const std::string marker = "<Field name=\"enableInstancing\"";
        bool changed = false;
        usize position = text.find(marker);
        while (position != std::string::npos)
        {
            usize lineEnd = text.find("\n", position);
            if (lineEnd == std::string::npos) lineEnd = text.size();
            const std::string line = text.substr(position, lineEnd - position);

            //只处理原生组件字段：托管脚本字段带 inspectorVisible，用户脚本里同名的字段不能动。
            //形状不认识也原样保留，只报出来
            const bool nativeField = line.find("inspectorVisible=") == std::string::npos;
            const bool expectedShape = line.find("type=\"bool\"") != std::string::npos;
            if (nativeField && expectedShape)
            {
                //false 迁移成 Individual，true 与缺省迁移成 Auto，保持原作者的意图。
                //type 写字段描述里的类型名，与序列化器写出的形式一致，迁移后的文件不必再存一次才稳定
                const bool disabled = line.find("value=\"false\"") != std::string::npos || line.find("value=\"0\"") != std::string::npos;
                const std::string replacement = "<Field name=\"drawStrategy\" type=\"DrawStrategy\" value=\"" +
                    std::string(disabled ? "1" : "0") + "\" />";
                text.replace(position, lineEnd - position, replacement);
                rewrittenFields += 1;
                changed = true;
                lineEnd = position + replacement.size();
            }
            else if (nativeField)
            {
                Log::Warning("Content migration left an unexpected enableInstancing field untouched.");
            }

            position = text.find(marker, lineEnd);
        }

        return changed;
    }

    //内容根相对 Key：与资产管理使用同一套正斜杠写法
    std::string ToContentKey(const std::filesystem::path& contentRoot, const std::filesystem::path& path)
    {
        std::error_code error;
        std::filesystem::path relative = std::filesystem::relative(path, contentRoot, error);
        if (error) return Utf8Path::ToUtf8(path);
        return Utf8Path::ToUtf8(relative);
    }
}

bool ContentMigration::MigrateForVersion(const std::string& contentRoot, uint32 fromVersion,
    MigrationReport& outReport, std::string& outError)
{
    outError.clear();
    outReport = MigrationReport();

    //当前版本已经不需要迁移
    if (fromVersion >= OrbedenProjectVersion) return true;

    std::filesystem::path root = Utf8Path::FromUtf8(contentRoot);
    std::error_code error;
    if (!std::filesystem::is_directory(root, error))
    {
        outError = "Content root was not found: " + contentRoot;
        return false;
    }

    for (std::filesystem::recursive_directory_iterator iterator(root, error), end; !error && iterator != end; iterator.increment(error))
    {
        if (!iterator->is_regular_file()) continue;

        const std::filesystem::path path = iterator->path();
        const std::string extension = Utf8Path::ToUtf8(path.extension());

        //26 → 27：静态网格渲染器的 enableInstancing 字段改名为 drawStrategy
        if (extension == ".world" || extension == ".prefab")
        {
            std::string text;
            if (!ReadTextFile(path, text))
            {
                outError = "Cannot read " + ToContentKey(root, path);
                return false;
            }

            int32 rewrittenFields = 0;
            if (!RewriteInstancingField(text, rewrittenFields)) continue;
            if (!WriteTextFile(path, text))
            {
                outError = "Cannot write " + ToContentKey(root, path);
                return false;
            }

            outReport.rewrittenFiles += 1;
            outReport.rewrittenFields += rewrittenFields;
            Log::Info(("Content migration rewrote " + std::to_string(rewrittenFields) + " field(s) in " +
                ToContentKey(root, path)).c_str());
            continue;
        }

        //27 起 Shader 必须接入几何接口：直接使用 u_Model 的自定义 Shader 需要作者迁移，
        //这里只列出来，不改写 GLSL。
        if (extension == ".orbshader")
        {
            std::string text;
            if (!ReadTextFile(path, text)) continue;
            if (text.find("u_Model") != std::string::npos) outReport.pendingShaderKeys.push_back(ToContentKey(root, path));
        }
    }

    if (error)
    {
        outError = "Content migration scan failed: " + error.message();
        return false;
    }

    return true;
}
