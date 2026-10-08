#include "Editor/EditorIcons.h"

#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"

#include <glad/gl.h>

//编辑器与 OrbedenCore 各自持有实现，静态链接避免与核心库的同名符号冲突
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace
{
    std::filesystem::path iconDirectory;
    std::unordered_map<std::string, ImTextureID> iconTextures;

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
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(pixels);
        return static_cast<ImTextureID>(texture);
    }

}

void EditorIcons::SetDirectory(const std::filesystem::path& directory)
{
    Shutdown();
    iconDirectory = directory;
}

ImTextureID EditorIcons::Get(const std::string& name, float32 displaySize)
{
    if (name.empty() || iconDirectory.empty()) return 0;
    ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    float32 pixelSize = displaySize * std::max(scale.x, scale.y);
    std::string textureName = pixelSize > 32.0f ? "256/" + name : name;
    auto found = iconTextures.find(textureName);
    if (found != iconTextures.end()) return found->second;

    //按需上传所选尺寸，缓存缺失结果
    auto path = iconDirectory / Utf8Path::FromUtf8(textureName + ".png");
    ImTextureID texture = UploadIcon(path);
    if (texture == 0) Log::Warning(("Editor icon could not be loaded: " + Utf8Path::ToUtf8(path)).c_str());
    iconTextures.emplace(textureName, texture);
    return texture;
}

void EditorIcons::Shutdown()
{
    for (const auto& pair : iconTextures)
    {
        GLuint texture = static_cast<GLuint>(pair.second);
        if (texture != 0) glDeleteTextures(1, &texture);
    }
    iconTextures.clear();
}
