#pragma once

#include "Runtime/Object/Object.h"
#include "Runtime/Object/Texture2D.h"

#include <string>

class Material;

//模型源文件的坐标系上轴。引擎是 Y-up（XZ 为地面平面）。
enum class MeshUpAxis : uint32
{
    //引擎原生轴向，不做转换
    Y = 0,
    //Z-up 源（Blender、CAD 等），导入时整体转到 Y-up
    Z = 1,
};

//单次导入的选项，来自资源旁 .resinfo 的设置段。
//每个键都带"是否指定"的语义：未指定时按文件原样或按语义推断，指定时以用户值为准。
struct AssetImportSettings
{
public:
    //设置表的版本头。表格式为每行 "<源Key>\t<设置名>\t<值>"，不认识的设置名跳过，
    //便于以后追加；同一个解析器同时服务单个资源的导入和批量 Reimport。
    static constexpr const char* TableHeader = "OrbedenImport1";

    //纹理颜色空间；未指定时按语义推断（基础色 sRGB、法线等数据贴图线性）
    bool hasTextureColorSpace = false;
    TextureColorSpace textureColorSpace = TextureColorSpace::SRGB;

    //网格缩放倍率；未指定时按文件原样。统一缩放不改变法线方向。
    bool hasMeshScale = false;
    float32 meshScale = 1.0f;

    //源坐标系上轴；未指定时按 Y-up 原样
    bool hasMeshUpAxis = false;
    MeshUpAxis meshUpAxis = MeshUpAxis::Y;

    //从设置表里取出指定源文件的设置；首行是可选的版本头
    static AssetImportSettings Lookup(const std::string& table, const std::string& sourceKey);
};

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

    //按主文件路径选择导入器；settings 为空时全部按语义自动推断
    static AssetCollection ImportSource(std::string path, const AssetImportSettings& settings = {});

    //导入GLSL着色器源码对
    static AssetCollection Import_GLSL(std::string path);

    //导入单文件OrbShader
    static AssetCollection Import_ORBSHADER(std::string path);

    //导入图片为CPU纹理
    static AssetCollection Import_IMG(std::string path, const AssetImportSettings& settings = {});

    /// <summary>导入独立材质资产（.orbmat）。</summary>
    static AssetCollection Import_ORBMAT(std::string path);

    /// <summary>把内存中的材质写回 .orbmat 源文件，供编辑器编辑后保存。</summary>
    static bool SaveMaterialAsset(const Material& material, const std::string& path, std::string& error);

    //导入OBJ为复合资源；settings 里的网格缩放与坐标轴作用于产出的全部网格
    static AssetCollection Import_OBJ(std::string path, const AssetImportSettings& settings = {});

    //导入glTF或GLB为复合资源；settings 里的网格缩放与坐标轴作用于产出的全部网格
    static AssetCollection Import_GLTF(std::string path, const AssetImportSettings& settings = {});

    //导入OBJ为非索引Mesh
    static AssetCollection Import_AsIndexlessMesh_OBJ(std::string path);
};
