#include "Editor/PlayerContentCooker.h"

#include "Editor/ProjectLayout.h"
#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/AssetPipeline.h"
#include "Runtime/CookedAssetSerializer.h"
#include "Runtime/DisplaySettings.h"
#include "Runtime/LayerSettings.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace
{
    //场景保持 XML，原样复制而不 cook。
    constexpr const char* WorldExtension = ".world";

    //GLSL 源对的两种后缀
    constexpr const char* VertexGlslSuffix = ".vert.glsl";
    constexpr const char* FragmentGlslSuffix = ".frag.glsl";

    //去掉 GLSL 源对后缀取基名，不是 GLSL 源对时返回空串
    std::string GetGlslPairKey(const std::string& key)
    {
        for (const char* suffix : { VertexGlslSuffix, FragmentGlslSuffix })
        {
            std::string text(suffix);
            if (key.size() <= text.size()) continue;
            if (key.compare(key.size() - text.size(), text.size(), text) != 0) continue;

            return key.substr(0, key.size() - text.size());
        }

        return std::string();
    }

    //收集内容根内全部常规文件的内容根相对 Key
    void CollectContentKeys(const std::filesystem::path& contentRoot, List<std::string>& keys)
    {
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator iterator(contentRoot, error), end; !error && iterator != end; iterator.increment(error))
        {
            const std::filesystem::directory_entry& entry = *iterator;
            if (!entry.is_regular_file()) continue;

            keys.push_back(ResourceManager::ToResourceKey(Utf8Path::ToUtf8(entry.path().lexically_relative(contentRoot))));
        }
    }

    //按扩展名拆出需要 cook 的源 Key、需要复制的场景与不参与打包的文件
    void ClassifyContentKeys(const List<std::string>& contentKeys, List<std::string>& sourceKeys, List<std::string>& worldKeys)
    {
        for (const std::string& key : contentKeys)
        {
            //项目级设置文件按原样复制，不参与 cook
            if (key == LayerSettings::FileName || key == DisplaySettings::FileName
                || Utf8Path::ToUtf8(Utf8Path::FromUtf8(key).extension()) == WorldExtension)
            {
                worldKeys.push_back(key);
                continue;
            }

            if (AssetPipeline::SelectImporter(key) != AssetImporter::None)
            {
                sourceKeys.push_back(key);
                continue;
            }

            //GLSL 源对以去掉 .vert/.frag.glsl 的基名作为 Key。
            std::string pairKey = GetGlslPairKey(key);
            if (!pairKey.empty() && AssetPipeline::SelectImporter(pairKey) == AssetImporter::Glsl) sourceKeys.push_back(pairKey);
        }

        std::sort(sourceKeys.begin(), sourceKeys.end());
        sourceKeys.erase(std::unique(sourceKeys.begin(), sourceKeys.end()), sourceKeys.end());
    }

    //复制场景文件并保留原目录结构
    bool CopyWorldFile(const std::filesystem::path& sourcePath, const std::filesystem::path& targetPath, std::string& error)
    {
        std::error_code code;
        if (targetPath.has_parent_path()) std::filesystem::create_directories(targetPath.parent_path(), code);

        std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::overwrite_existing, code);
        if (code)
        {
            error = "World file could not be copied while packaging: " + Utf8Path::ToUtf8(sourcePath);
            return false;
        }

        return true;
    }

    //导入一个源文件并把产生的每个对象写成独立产物
    bool CookSourceKey(const std::string& sourceKey, const std::filesystem::path& outputRoot, List<std::string>& cookedKeys, std::string& error)
    {
        AssetCollection collection = AssetPipeline::ImportSource(sourceKey);
        if (!collection.Succeeded())
        {
            error = "Asset import failed while packaging: " + sourceKey;
            return false;
        }

        for (usize index = 0; index < collection.objectKeys.size(); ++index)
        {
            const std::string& objectKey = collection.objectKeys[index];

            //依赖边由导入期登记，产物按对象只带走自己那部分。
            const ResourceManager::ResourceRecord* record = ResourceManager::FindRecord(objectKey);
            List<std::string> dependencies = record ? record->dependencies : List<std::string>();

            std::filesystem::path blobPath = outputRoot / Utf8Path::FromUtf8(CookedAssetSerializer::GetBlobFileName(objectKey));
            if (!CookedAssetSerializer::Write(Utf8Path::ToUtf8(blobPath), collection.objects[index], sourceKey, dependencies, error)) return false;

            cookedKeys.push_back(objectKey);
        }

        return true;
    }
}

//把源内容根内的资源全部 cook 到输出目录
bool PlayerContentCooker::Cook(const std::string& sourceContentRoot, const std::string& cookedOutputRoot, std::string& error)
{
    error.clear();

    std::filesystem::path contentRoot = Utf8Path::FromUtf8(sourceContentRoot);
    if (!std::filesystem::is_directory(contentRoot))
    {
        error = "Content root was not found: " + sourceContentRoot;
        return false;
    }

    std::filesystem::path outputRoot = Utf8Path::FromUtf8(cookedOutputRoot);
    //输出目录会被清空重建，只允许指向项目的资源缓存目录。
    if (outputRoot.filename() != "Player" || outputRoot.parent_path().filename() != ProjectLayout::ResourceCacheFolder)
    {
        error = "Cook output must be the project " + std::string(ProjectLayout::PlayerResourceCacheFolder) + " directory: " + cookedOutputRoot;
        return false;
    }

    std::error_code code;
    std::filesystem::remove_all(outputRoot, code);
    std::filesystem::create_directories(outputRoot, code);

    //导入靠内容根解析 Key；调用方传进来的就是 Editor 正在用的那个内容根。
    PathDefines::SetContentRoot(sourceContentRoot);

    List<std::string> contentKeys;
    CollectContentKeys(contentRoot, contentKeys);

    List<std::string> sourceKeys;
    List<std::string> worldKeys;
    ClassifyContentKeys(contentKeys, sourceKeys, worldKeys);

    for (const std::string& worldKey : worldKeys)
    {
        std::filesystem::path worldSource = contentRoot / Utf8Path::FromUtf8(worldKey);
        if (!CopyWorldFile(worldSource, outputRoot / Utf8Path::FromUtf8(worldKey), error)) return false;
    }

    List<std::string> cookedKeys;
    for (const std::string& sourceKey : sourceKeys)
    {
        if (!CookSourceKey(sourceKey, outputRoot, cookedKeys, error)) return false;
    }

    std::filesystem::path indexPath = outputRoot / Utf8Path::FromUtf8(CookedAssetSerializer::IndexFileName);
    return CookedAssetSerializer::WriteIndex(Utf8Path::ToUtf8(indexPath), cookedKeys, error);
}
