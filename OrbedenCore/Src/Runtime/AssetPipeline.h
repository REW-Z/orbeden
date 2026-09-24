#pragma once

#include "Runtime/Object/Object.h"

#include <string>

class Material;

//一次导入产生的复合资源集合
class AssetCollection
{
public:
    std::string sourceKey;
    List<std::string> sourceFiles;
    List<std::string> objectKeys;
    List<Object*> objects;
    List<std::string> mainKeys;
    List<std::string> warnings;
    List<std::string> errors;

    //判断导入是否成功
    bool Succeeded() const;

    //记录导入源文件
    void AddSourceFile(const std::string& path);

    //记录导入对象
    void AddObject(const std::string& key, Object* object, bool isMain = false);

    //记录警告
    void AddWarning(const std::string& warning);

    //记录错误
    void AddError(const std::string& error);
};

//资源导入器种类
enum class AssetImporter
{
    None,
    Image,
    Obj,
    Gltf,
    OrbShader,
    Glsl,
    OrbMat,
};

//资源导入管道，负责把文件输入转换为Object资源
class AssetPipeline
{
public:
    //按主文件路径判断可用导入器，无对应导入器时返回 None
    static AssetImporter SelectImporter(const std::string& sourceKey);

    //按主文件路径选择导入器
    static AssetCollection ImportSource(std::string path);

    //导入GLSL着色器源码对
    static AssetCollection Import_GLSL(std::string path);

    //导入单文件OrbShader
    static AssetCollection Import_ORBSHADER(std::string path);

    //导入图片为CPU纹理
    static AssetCollection Import_IMG(std::string path);

    /// <summary>导入独立材质资产（.orbmat）。</summary>
    static AssetCollection Import_ORBMAT(std::string path);

    /// <summary>把内存中的材质写回 .orbmat 源文件，供编辑器编辑后保存。</summary>
    static bool SaveMaterialAsset(const Material& material, const std::string& path, std::string& error);

    //导入OBJ为复合资源
    static AssetCollection Import_OBJ(std::string path);

    //导入glTF或GLB为复合资源
    static AssetCollection Import_GLTF(std::string path);

    //导入OBJ为非索引Mesh
    static AssetCollection Import_AsIndexlessMesh_OBJ(std::string path);
};
