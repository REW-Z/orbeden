#include "Runtime/Object/Shader.h"

#include <cctype>
#include <regex>

OBJECT_TYPE_IMPLEMENT(Shader, Object)

namespace
{
    constexpr const char* MaterialSpecularColorSlot = "u_SpecularColor";
    constexpr const char* MaterialEmissionColorSlot = "u_EmissionColor";
    constexpr const char* MaterialShininessSlot = "u_Shininess";

    //判断字符串前缀
    bool StartsWith(const std::string& text, const std::string& prefix)
    {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    //判断字符串后缀
    bool EndsWith(const std::string& text, const std::string& suffix)
    {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    //移除 GLSL 注释
    std::string StripGlslComments(const std::string& source)
    {
        std::string result;
        result.reserve(source.size());
        bool inLineComment = false;
        bool inBlockComment = false;

        for (usize index = 0; index < source.size(); ++index)
        {
            char ch = source[index];
            char next = index + 1 < source.size() ? source[index + 1] : '\0';
            if (inLineComment)
            {
                if (ch == '\n')
                {
                    inLineComment = false;
                    result += ch;
                }
                else
                {
                    result += ' ';
                }
                continue;
            }

            if (inBlockComment)
            {
                if (ch == '*' && next == '/')
                {
                    inBlockComment = false;
                    result += ' ';
                    result += ' ';
                    ++index;
                }
                else
                {
                    result += ch == '\n' ? '\n' : ' ';
                }
                continue;
            }

            if (ch == '/' && next == '/')
            {
                inLineComment = true;
                result += ' ';
                result += ' ';
                ++index;
                continue;
            }

            if (ch == '/' && next == '*')
            {
                inBlockComment = true;
                result += ' ';
                result += ' ';
                ++index;
                continue;
            }

            result += ch;
        }

        return result;
    }

    //判断是否为渲染管线内置纹理
    bool IsBuiltinTextureUniform(const std::string& uniformName)
    {
        return uniformName == "u_ShadowMap"
            || uniformName == "u_SkyboxTexture"
            || uniformName == "u_CameraColorTexture"
            || uniformName == "u_CameraDepthTexture";
    }

    //判断是否为渲染管线内置 uniform
    bool IsBuiltinMaterialUniform(const std::string& uniformName)
    {
        return IsBuiltinTextureUniform(uniformName)
            || uniformName == "u_Model"
            || uniformName == "u_ViewProjection"
            || uniformName == "u_LightViewProjection"
            || uniformName == "u_CameraPosition"
            || uniformName == "u_AmbientColor"
            || uniformName == "u_LightDirection"
            || uniformName == "u_LightColor"
            || uniformName == "u_LightIntensity"
            || uniformName == "u_ShadowBias"
            || uniformName == "u_ShadowStrength"
            || uniformName == "u_UseShadowMap"
            || uniformName == "u_ReceiveShadows"
            || uniformName == "u_UseCameraTextures"
            || uniformName == "u_CameraNearPlane"
            || uniformName == "u_CameraFarPlane"
            || uniformName == "u_Time";
    }

    //从 uniform 名获取编辑器可读名
    std::string GetUniformDisplayName(const std::string& uniformName, const std::string& suffix)
    {
        std::string text = uniformName;
        if (StartsWith(text, "u_")) text.erase(0, 2);
        if (!suffix.empty() && EndsWith(text, suffix)) text.erase(text.size() - suffix.size());
        if (text.empty()) return uniformName;

        std::string result;
        result.reserve(text.size() + 4);
        for (usize index = 0; index < text.size(); ++index)
        {
            char ch = text[index];
            if (index > 0 && std::isupper(static_cast<unsigned char>(ch)) && !std::isupper(static_cast<unsigned char>(text[index - 1])))
            {
                result += ' ';
            }

            result += ch;
        }

        return result.empty() ? uniformName : result;
    }

    //判断纹理槽是否已经存在
    bool HasTextureSlot(const List<ShaderTextureSlot>& slots, const std::string& name)
    {
        for (const ShaderTextureSlot& slot : slots)
        {
            if (slot.name == name) return true;
        }

        return false;
    }

    //加入一个材质纹理槽
    void AddTextureSlot(List<ShaderTextureSlot>& slots, const std::string& uniformName)
    {
        if (uniformName.empty() || IsBuiltinTextureUniform(uniformName) || HasTextureSlot(slots, uniformName)) return;

        ShaderTextureSlot slot;
        slot.name = uniformName;
        slot.displayName = GetUniformDisplayName(uniformName, "Texture");
        slot.dimension = ShaderTextureDimension::Texture2D;
        slots.push_back(slot);
    }

    //判断颜色槽是否已经存在
    bool HasColorSlot(const List<ShaderColorSlot>& slots, const std::string& name)
    {
        for (const ShaderColorSlot& slot : slots)
        {
            if (slot.name == name) return true;
        }

        return false;
    }

    //获取颜色槽默认值
    color GetColorSlotDefault(const std::string& uniformName)
    {
        if (uniformName == MaterialSpecularColorSlot) return { 0.0f, 0.0f, 0.0f, 1.0f };
        if (uniformName == MaterialEmissionColorSlot) return { 0.0f, 0.0f, 0.0f, 1.0f };
        return { 1.0f, 1.0f, 1.0f, 1.0f };
    }

    //加入一个材质颜色槽
    void AddColorSlot(List<ShaderColorSlot>& slots, const std::string& uniformName)
    {
        if (uniformName.empty() || IsBuiltinMaterialUniform(uniformName) || HasColorSlot(slots, uniformName)) return;

        ShaderColorSlot slot;
        slot.name = uniformName;
        slot.displayName = GetUniformDisplayName(uniformName, "Color");
        slot.defaultValue = GetColorSlotDefault(uniformName);
        slots.push_back(slot);
    }

    //判断浮点槽是否已经存在
    bool HasFloatSlot(const List<ShaderFloatSlot>& slots, const std::string& name)
    {
        for (const ShaderFloatSlot& slot : slots)
        {
            if (slot.name == name) return true;
        }

        return false;
    }

    //获取浮点槽默认值
    float32 GetFloatSlotDefault(const std::string& uniformName)
    {
        if (uniformName == MaterialShininessSlot) return 1.0f;
        if (uniformName == "u_RefractionStrength") return 0.015f;
        if (uniformName == "u_DistortionStrength") return 0.01f;
        if (uniformName == "u_DropletScale") return 12.0f;
        if (uniformName == "u_NoiseScale") return 16.0f;
        if (uniformName == "u_FlowSpeed") return 0.25f;
        if (uniformName == "u_DistortionSpeed") return 0.8f;
        if (uniformName == "u_EdgeFade") return 0.12f;
        if (uniformName == "u_Opacity") return 1.0f;
        return 0.0f;
    }

    //加入一个材质浮点槽
    void AddFloatSlot(List<ShaderFloatSlot>& slots, const std::string& uniformName)
    {
        if (uniformName.empty() || IsBuiltinMaterialUniform(uniformName) || HasFloatSlot(slots, uniformName)) return;

        ShaderFloatSlot slot;
        slot.name = uniformName;
        slot.displayName = GetUniformDisplayName(uniformName, std::string());
        slot.defaultValue = GetFloatSlotDefault(uniformName);
        slots.push_back(slot);
    }
}

bool Shader::ReflectSlotsFromSource()
{
    textureSlots.clear();
    colorSlots.clear();
    floatSlots.clear();
    if (passes.empty())
    {
        ShaderPass pass;
        pass.vertexSource = vertexSource;
        pass.fragmentSource = fragmentSource;
        passes.push_back(pass);
    }

    std::regex samplerRegex("\\buniform\\s+([A-Za-z_][A-Za-z0-9_]*\\s+)*sampler2D\\s+([A-Za-z_][A-Za-z0-9_]*)");
    std::regex colorRegex("\\buniform\\s+([A-Za-z_][A-Za-z0-9_]*\\s+)*vec4\\s+([A-Za-z_][A-Za-z0-9_]*)");
    std::regex floatRegex("\\buniform\\s+([A-Za-z_][A-Za-z0-9_]*\\s+)*float\\s+([A-Za-z_][A-Za-z0-9_]*)");

    auto scanSource = [&](const std::string& source)
    {
        std::string stripped = StripGlslComments(source);
        for (std::sregex_iterator it(stripped.begin(), stripped.end(), samplerRegex), end; it != end; ++it)
        {
            AddTextureSlot(textureSlots, (*it)[2].str());
        }

        for (std::sregex_iterator it(stripped.begin(), stripped.end(), colorRegex), end; it != end; ++it)
        {
            AddColorSlot(colorSlots, (*it)[2].str());
        }

        for (std::sregex_iterator it(stripped.begin(), stripped.end(), floatRegex), end; it != end; ++it)
        {
            AddFloatSlot(floatSlots, (*it)[2].str());
        }
    };

    for (const ShaderPass& pass : passes)
    {
        scanSource(pass.vertexSource);
        scanSource(pass.fragmentSource);
    }

    //校验 Uniform 材质槽类型
    bool valid = true;
    for (const ShaderTextureSlot& texture : textureSlots)
    {
        valid &= !HasColorSlot(colorSlots, texture.name) && !HasFloatSlot(floatSlots, texture.name);
    }
    for (const ShaderColorSlot& colorSlot : colorSlots)
    {
        valid &= !HasFloatSlot(floatSlots, colorSlot.name);
    }

    MarkDirty();
    return valid;
}

void Shader::ReplaceSource(const std::string& vertex, const std::string& fragment)
{
    vertexSource = vertex;
    fragmentSource = fragment;
    passes.clear();
    ShaderPass pass;
    pass.vertexSource = vertex;
    pass.fragmentSource = fragment;
    passes.push_back(pass);
    ReflectSlotsFromSource();
}

//替换有序 Pass 并刷新材质槽
bool Shader::ReplacePasses(const List<ShaderPass>& value)
{
    if (value.empty()) return false;

    passes = value;
    vertexSource = passes[0].vertexSource;
    fragmentSource = passes[0].fragmentSource;
    return ReflectSlotsFromSource();
}

//获取 Pass 数量
uint32 Shader::GetPassCount() const
{
    return static_cast<uint32>(passes.size());
}

//获取指定 Pass
const ShaderPass* Shader::GetPass(uint32 index) const
{
    return index < passes.size() ? &passes[index] : nullptr;
}

bool Shader::IsDirty() const
{
    return gpuDirty;
}

void Shader::MarkDirty()
{
    gpuDirty = true;
}

void Shader::ClearDirty()
{
    gpuDirty = false;
}
