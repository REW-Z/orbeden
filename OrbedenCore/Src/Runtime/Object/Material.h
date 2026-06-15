#pragma once

#include "Runtime/EngineTypes.h"
#include "Runtime/Object/Object.h"

#include <string>

class MaterialShader;
class Texture2D;

//CPU材质资源，保存参数和贴图/Shader引用
class Material : public Object
{
    OBJECT_TYPE_DECLARE(Material)

public:
    std::string name;
    vector3 ambient;
    vector3 diffuse = { 1.0f, 1.0f, 1.0f };
    vector3 specular;
    vector3 emission;
    float32 shininess = 0.0f;
    bool hasDiffuseTexture = false;
    bool hasBumpTexture = false;
    Ref<Texture2D> textureDiffuse;
    Ref<Texture2D> textureBump;
    Ref<MaterialShader> shader;
};
