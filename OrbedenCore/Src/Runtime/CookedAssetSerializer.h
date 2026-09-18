#pragma once

#include "Runtime/Object/Object.h"

#include <string>

//打包用资源二进制（.orbo）读写。
//位置式格式：只写字段顺序与长度，不写字段名；改动资源字段后必须重新打包。
//一个文件承载一个资源对象，文件名是资源 Key 的哈希。
class CookedAssetSerializer
{
public:
    //打包清单文件名，位于内容根下
    static constexpr const char* IndexFileName = "cooked.index";

    //写出一个资源对象及其依赖边；路径必须已存在
    static bool Write(const std::string& blobPath, Object* object, const std::string& sourceKey, const List<std::string>& dependencies, std::string& error);

    //读出资源对象并注册，跨文件的引用 Key 由 externalRefs 带回
    static bool Read(const std::string& blobPath, List<std::string>& externalRefs, std::string& error);

    //写出打包清单，同时列出文件名与资源 Key
    static bool WriteIndex(const std::string& indexPath, const List<std::string>& resourceKeys, std::string& error);

    //读取内容根内的打包清单；清单不存在时返回 false
    static bool ReadIndex(List<std::string>& resourceKeys);

    //按资源 Key 计算打包文件名
    static std::string GetBlobFileName(const std::string& resourceKey);

    //按资源 Key 计算打包文件路径
    static std::string GetBlobPath(const std::string& resourceKey);
};
