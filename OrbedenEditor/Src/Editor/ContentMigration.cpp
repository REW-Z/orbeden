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

    //迁移条目：Key 是内容根相对路径，SourcePath 是模板根相对源路径。
    //两者不能靠一条规则推出来：内联的 Key 是项目内的位置，SourcePath 是模板里的位置。
    struct MigrationFile
    {
        const char* key;
        const char* sourcePath;
    };

    //v29 新增大气功能所需的内容文件
    const MigrationFile AtmosphereNewFiles[] =
    {
        { "Builtin/atmosphere_common.orbinc", "Builtin/atmosphere_common.orbinc" },
        { "Builtin/atmosphere_sampling.orbinc", "Builtin/atmosphere_sampling.orbinc" },
        { "Builtin/Shaders/atmosphere_transmittance.orbshader", "Builtin/Shaders/atmosphere_transmittance.orbshader" },
        { "Builtin/Shaders/atmosphere_aerial.orbshader", "Builtin/Shaders/atmosphere_aerial.orbshader" },
        { "Builtin/Shaders/atmosphere_sky_lut.orbshader", "Builtin/Shaders/atmosphere_sky_lut.orbshader" },
        { "Builtin/Shaders/atmosphere_sky.orbshader", "Builtin/Shaders/atmosphere_sky.orbshader" },
    };

    //v29 接入空气透视的既有内置 Shader，基线文件与它们同名
    const MigrationFile AtmospherePatchedFiles[] =
    {
        { "Builtin/Shaders/pbs_metallic.orbshader", "Builtin/Shaders/pbs_metallic.orbshader" },
        { "Builtin/Shaders/blinn_phong.orbshader", "Builtin/Shaders/blinn_phong.orbshader" },
        { "Builtin/Shaders/transparent.orbshader", "Builtin/Shaders/transparent.orbshader" },
        { "Builtin/Shaders/particle_unlit.orbshader", "Builtin/Shaders/particle_unlit.orbshader" },
        { "Builtin/Shaders/particle_trail.orbshader", "Builtin/Shaders/particle_trail.orbshader" },
        { "Builtin/Shaders/refraction.orbshader", "Builtin/Shaders/refraction.orbshader" },
        { "Builtin/Shaders/rain_glass.orbshader", "Builtin/Shaders/rain_glass.orbshader" },
        { "Builtin/Shaders/heat_wake.orbshader", "Builtin/Shaders/heat_wake.orbshader" },
    };

    constexpr const char* AtmosphereBaselineFolder = "Migrations/28AtmosphereBaseline";

    //v30 新增浓雾 include
    const MigrationFile DenseFogNewFiles[] =
    {
        { "Builtin/dense_fog.orbinc", "Builtin/dense_fog.orbinc" },
    };

    //v30 接入浓雾的既有文件：大气 include 与两处天空绘制
    const MigrationFile DenseFogPatchedFiles[] =
    {
        { "Builtin/atmosphere_common.orbinc", "Builtin/atmosphere_common.orbinc" },
        { "Builtin/atmosphere_sampling.orbinc", "Builtin/atmosphere_sampling.orbinc" },
        { "Builtin/Shaders/atmosphere_sky.orbshader", "Builtin/Shaders/atmosphere_sky.orbshader" },
        //skybox 这时还在项目级 Shaders 下，模板来源已随 v31 移入 Builtin
        { "Shaders/skybox.orbshader", "Builtin/Shaders/skybox.orbshader" },
    };

    constexpr const char* DenseFogBaselineFolder = "Migrations/29DenseFogBaseline";

    //v31 转正到 Builtin 的两个引擎 Shader，以及它们在项目内的新旧位置
    constexpr const char* EngineShaderNames[] = { "shadow_depth.orbshader", "skybox.orbshader" };
    constexpr const char* EngineShaderNewFolder = "Builtin/Shaders";
    constexpr const char* EngineShaderOldFolder = "Shaders";
    constexpr const char* ResInfoExtension = ".resinfo";

    //去掉回车；行尾不是内容差异，比较前统一
    std::string RemoveCarriageReturns(const std::string& text)
    {
        std::string result;
        result.reserve(text.size());
        for (char ch : text)
        {
            if (ch != '\r') result += ch;
        }

        return result;
    }

    //判断文本是否使用 CRLF
    bool UsesCarriageReturns(const std::string& text)
    {
        return text.find("\r\n") != std::string::npos;
    }

    //按指定行尾风格写出，不把目标文件的换行整体翻掉
    std::string MatchLineEndings(const std::string& text, const std::string& reference)
    {
        if (!UsesCarriageReturns(reference)) return text;

        std::string result;
        result.reserve(text.size() + text.size() / 16);
        for (usize index = 0; index < text.size(); ++index)
        {
            if (text[index] == '\n' && (index == 0 || text[index - 1] != '\r')) result += '\r';
            result += text[index];
        }

        return result;
    }

    //铺入一个新增文件：内容一致跳过，同名内容不同说明作者自己写过，保留并列入报告
    bool CopyNewAssetFile(const std::filesystem::path& source, const std::filesystem::path& target,
        ContentMigration::MigrationReport& outReport, std::string& outError, const std::string& key)
    {
        std::string sourceText;
        if (!ReadTextFile(source, sourceText))
        {
            outError = "Cannot read migration source " + Utf8Path::ToUtf8(source) + " for " + key;
            return false;
        }

        std::error_code error;
        if (std::filesystem::exists(target, error) && !error)
        {
            std::string targetText;
            if (!ReadTextFile(target, targetText))
            {
                outError = "Cannot read " + key;
                return false;
            }

            if (RemoveCarriageReturns(targetText) != RemoveCarriageReturns(sourceText))
            {
                outReport.pendingShaderKeys.push_back(key);
            }

            return true;
        }

        std::error_code createError;
        if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), createError);
        if (!WriteTextFile(target, sourceText))
        {
            outError = "Cannot write " + key;
            return false;
        }

        outReport.rewrittenFiles += 1;
        return true;
    }

    //只把仍等于基线的内容换成新版本：作者改过的保留原样并列入报告
    bool UpdateAssetFile(const std::filesystem::path& source, const std::filesystem::path& baseline,
        const std::filesystem::path& target, ContentMigration::MigrationReport& outReport,
        std::string& outError, const std::string& key)
    {
        //作者删掉的内置 Shader 不再补回，也不该挡住整次升级
        std::error_code error;
        if (!std::filesystem::exists(target, error) || error) return true;

        std::string sourceText;
        std::string baselineText;
        std::string targetText;
        if (!ReadTextFile(source, sourceText) || !ReadTextFile(baseline, baselineText) || !ReadTextFile(target, targetText))
        {
            outError = "Cannot read " + key;
            return false;
        }

        //行尾不是作者的改动，比较前统一
        const std::string targetContent = RemoveCarriageReturns(targetText);
        if (targetContent == RemoveCarriageReturns(sourceText)) return true;
        if (targetContent != RemoveCarriageReturns(baselineText))
        {
            outReport.pendingShaderKeys.push_back(key);
            return true;
        }

        if (!WriteTextFile(target, MatchLineEndings(sourceText, targetText)))
        {
            outError = "Cannot write " + key;
            return false;
        }

        outReport.rewrittenFiles += 1;
        return true;
    }

    //删除内容根内的一个文件，连带它的伴生 .resinfo。文件本来就不存在时按已删除处理。
    bool RemoveAssetFile(const std::filesystem::path& target, ContentMigration::MigrationReport& outReport,
        std::string& outError, const std::string& key)
    {
        std::filesystem::path resInfo = target;
        resInfo += ResInfoExtension;
        //伴生缓存先删：源文件没了它就成了无主缓存，留着只会让下次导入读到过期清单
        std::error_code error;
        std::filesystem::remove(resInfo, error);
        error.clear();

        if (std::filesystem::remove(target, error))
        {
            outReport.removedFiles += 1;
            outReport.removedKeys.push_back(key);
            return true;
        }

        if (error)
        {
            outError = "Cannot remove " + key + ": " + error.message();
            return false;
        }

        return true;
    }

    //按表铺入新增文件，并把仍等于基线的既有文件换成当前版本
    bool MigrateAssetFiles(const std::filesystem::path& content, const std::filesystem::path& templates,
        const MigrationFile* newFiles, usize newFileCount, const MigrationFile* patchedFiles, usize patchedFileCount,
        const char* baselineFolder, ContentMigration::MigrationReport& outReport, std::string& outError)
    {
        for (usize index = 0; index < newFileCount; ++index)
        {
            const MigrationFile& entry = newFiles[index];
            const std::filesystem::path target = content / Utf8Path::FromUtf8(entry.key);
            if (!CopyNewAssetFile(templates / Utf8Path::FromUtf8(entry.sourcePath), target, outReport, outError, entry.key))
            {
                return false;
            }
        }

        for (usize index = 0; index < patchedFileCount; ++index)
        {
            const MigrationFile& entry = patchedFiles[index];
            const std::filesystem::path target = content / Utf8Path::FromUtf8(entry.key);
            const std::filesystem::path baseline = templates / baselineFolder / target.filename();
            if (!UpdateAssetFile(templates / Utf8Path::FromUtf8(entry.sourcePath), baseline, target,
                outReport, outError, entry.key))
            {
                return false;
            }
        }

        return true;
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

bool ContentMigration::MigrateAtmosphereAssets(const std::string& contentRoot, const std::string& templateRoot,
    MigrationReport& outReport, std::string& outError)
{
    outError.clear();

    std::filesystem::path content = Utf8Path::FromUtf8(contentRoot);
    std::filesystem::path templates = Utf8Path::FromUtf8(templateRoot);
    std::error_code error;
    if (!std::filesystem::is_directory(content, error))
    {
        outError = "Content root was not found: " + contentRoot;
        return false;
    }

    return MigrateAssetFiles(content, templates,
        AtmosphereNewFiles, std::size(AtmosphereNewFiles),
        AtmospherePatchedFiles, std::size(AtmospherePatchedFiles),
        AtmosphereBaselineFolder, outReport, outError);
}

bool ContentMigration::MigrateDenseFogAssets(const std::string& contentRoot, const std::string& templateRoot,
    MigrationReport& outReport, std::string& outError)
{
    outError.clear();

    std::filesystem::path content = Utf8Path::FromUtf8(contentRoot);
    std::filesystem::path templates = Utf8Path::FromUtf8(templateRoot);
    std::error_code error;
    if (!std::filesystem::is_directory(content, error))
    {
        outError = "Content root was not found: " + contentRoot;
        return false;
    }

    return MigrateAssetFiles(content, templates,
        DenseFogNewFiles, std::size(DenseFogNewFiles),
        DenseFogPatchedFiles, std::size(DenseFogPatchedFiles),
        DenseFogBaselineFolder, outReport, outError);
}

bool ContentMigration::MigrateBuiltinEngineShaders(const std::string& contentRoot, const std::string& templateRoot,
    MigrationReport& outReport, std::string& outError)
{
    outError.clear();

    std::filesystem::path content = Utf8Path::FromUtf8(contentRoot);
    std::filesystem::path templates = Utf8Path::FromUtf8(templateRoot);
    std::error_code error;
    if (!std::filesystem::is_directory(content, error))
    {
        outError = "Content root was not found: " + contentRoot;
        return false;
    }

    for (const char* name : EngineShaderNames)
    {
        const std::string newKey = std::string(EngineShaderNewFolder) + "/" + name;
        const std::string oldKey = std::string(EngineShaderOldFolder) + "/" + name;

        //新位置按新增文件铺入：项目里已经自己放过一份不同内容时保留原样并列入待迁移
        if (!CopyNewAssetFile(templates / Utf8Path::FromUtf8(EngineShaderNewFolder) / Utf8Path::FromUtf8(name),
            content / Utf8Path::FromUtf8(newKey), outReport, outError, newKey))
        {
            return false;
        }

        //旧位置一律删除：引擎按文件名在内容根内查找，同名两份会打告警并取字典序靠前的一份
        if (!RemoveAssetFile(content / Utf8Path::FromUtf8(oldKey), outReport, outError, oldKey)) return false;
    }

    return true;
}
