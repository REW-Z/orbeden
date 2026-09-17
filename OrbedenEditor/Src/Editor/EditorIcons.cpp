#include "Editor/EditorIcons.h"

#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"

#include <glad/gl.h>

//编辑器与 OrbedenCore 各自持有实现，静态链接避免与核心库的同名符号冲突
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"

#include <fstream>
#include <unordered_map>
#include <vector>

namespace
{
    std::filesystem::path iconDirectory;
    std::unordered_map<std::string, ImTextureID> iconTextures;
    bool iconsLoaded = false;

    //读取整张 PNG 并上传为 RGBA 纹理
    ImTextureID UploadIcon(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return 0;
        std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        if (bytes.empty()) return 0;

        int32 width = 0;
        int32 height = 0;
        int32 channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
            static_cast<int32>(bytes.size()), &width, &height, &channels, 4);
        if (!pixels) return 0;

        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(pixels);
        return static_cast<ImTextureID>(texture);
    }

    //加载目录内全部 PNG，文件名即图标名
    void LoadIcons()
    {
        iconsLoaded = true;
        if (iconDirectory.empty() || !std::filesystem::is_directory(iconDirectory))
        {
            Log::Warning(("Editor icons directory is missing: " + Utf8Path::ToUtf8(iconDirectory)).c_str());
            return;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(iconDirectory))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".png") continue;
            ImTextureID texture = UploadIcon(entry.path());
            if (texture != 0) iconTextures.emplace(Utf8Path::ToUtf8(entry.path().stem()), texture);
        }
    }
}

void EditorIcons::SetDirectory(const std::filesystem::path& directory)
{
    iconDirectory = directory;
    iconsLoaded = false;
}

ImTextureID EditorIcons::Get(const std::string& name)
{
    if (name.empty()) return 0;
    if (!iconsLoaded) LoadIcons();

    auto found = iconTextures.find(name);
    return found == iconTextures.end() ? 0 : found->second;
}

void EditorIcons::Shutdown()
{
    for (const auto& pair : iconTextures)
    {
        GLuint texture = static_cast<GLuint>(pair.second);
        if (texture != 0) glDeleteTextures(1, &texture);
    }
    iconTextures.clear();
    iconsLoaded = false;
}
