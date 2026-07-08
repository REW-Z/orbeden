#pragma once

#include "Runtime/EngineTypes.h"
#include "Runtime/Object/Object.h"

#include <string>

//Shader纹理维度，当前 GLSL 前端 v1 只反射 sampler2D
enum class ShaderTextureDimension
{
    Texture2D,
};

//Shader 暴露给 Material 的纹理槽
struct ShaderTextureSlot
{
public:
    std::string name;
    std::string displayName;
    ShaderTextureDimension dimension = ShaderTextureDimension::Texture2D;
};

//Shader 暴露给 Material 的颜色槽
struct ShaderColorSlot
{
public:
    std::string name;
    std::string displayName;
    color defaultValue = { 1.0f, 1.0f, 1.0f, 1.0f };
};

//Shader 暴露给 Material 的浮点槽
struct ShaderFloatSlot
{
public:
    std::string name;
    std::string displayName;
    float32 defaultValue = 0.0f;
};

//CPU着色器资源，保存源码但不编译GPU程序
class Shader : public Object
{
    OBJECT_TYPE_DECLARE(Shader)

private:
    uint64 revision = 1;

public:
    std::string name;
    std::string vertexPath;
    std::string fragmentPath;
    std::string vertexSource;
    std::string fragmentSource;
    List<ShaderTextureSlot> textureSlots;
    List<ShaderColorSlot> colorSlots;
    List<ShaderFloatSlot> floatSlots;

    //从 GLSL 源码刷新材质槽反射结果
    void ReflectSlotsFromSource();

    //替换 GLSL 源码并刷新反射结果
    void ReplaceSource(const std::string& vertex, const std::string& fragment);

    //获取 Shader 版本，用于刷新 GPU 缓存
    uint64 GetRevision() const;

    //标记 Shader 数据已修改
    void TouchRevision();
};
