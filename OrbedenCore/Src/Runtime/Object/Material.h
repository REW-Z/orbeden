#pragma once

#include "Runtime/EngineTypes.h"
#include "Runtime/Object/Object.h"

#include <string>

class Shader;
class Texture2D;

//材质保存的纹理槽，name 使用 GLSL uniform 名
struct MaterialTextureSlot
{
public:
    std::string name;
    Ref<Texture2D> texture;
};

//材质保存的颜色槽，name 使用 GLSL uniform 名
struct MaterialColorSlot
{
public:
    std::string name;
    color value;
};

//材质保存的浮点槽，name 使用 GLSL uniform 名
struct MaterialFloatSlot
{
public:
    std::string name;
    float32 value = 0.0f;
};

//CPU材质资源，保存参数和贴图/Shader引用
class Material : public Object
{
    OBJECT_TYPE_DECLARE(Material)

private:
    uint64 revision = 1;

public:
    std::string name;
    List<MaterialTextureSlot> textureSlots;
    List<MaterialColorSlot> colorSlots;
    List<MaterialFloatSlot> floatSlots;
    Ref<Shader> shader;

    //设置材质纹理槽
    void SetTexture(const std::string& slotName, Texture2D* texture);

    //设置材质纹理槽
    void SetTexture(const std::string& slotName, const StringId& textureId);

    //获取材质纹理
    Texture2D* GetTexture(const std::string& slotName) const;

    //判断材质纹理槽是否已设置
    bool HasTexture(const std::string& slotName) const;

    //清除材质纹理槽
    void ClearTexture(const std::string& slotName);

    //设置材质颜色槽
    void SetColor(const std::string& slotName, const color& value);

    //获取材质颜色槽
    color GetColor(const std::string& slotName, const color& defaultValue = color{ 0.0f, 0.0f, 0.0f, 1.0f }) const;

    //判断材质颜色槽是否已设置
    bool HasColor(const std::string& slotName) const;

    //清除材质颜色槽
    void ClearColor(const std::string& slotName);

    //设置材质浮点槽
    void SetFloat(const std::string& slotName, float32 value);

    //获取材质浮点槽
    float32 GetFloat(const std::string& slotName, float32 defaultValue = 0.0f) const;

    //判断材质浮点槽是否已设置
    bool HasFloat(const std::string& slotName) const;

    //清除材质浮点槽
    void ClearFloat(const std::string& slotName);

    //获取材质版本，用于刷新 GPU 缓存
    uint64 GetRevision() const;

    //标记材质数据已修改
    void TouchRevision();
};
