#pragma once

#include <string>
#include <memory>

struct WorldDocument;

#include "Runtime/World.h"

class WorldSerializer
{
public:
    //读取并校验独立的 XML 文档，不访问运行时对象
    static std::shared_ptr<WorldDocument> ReadDocument(const std::string& path, std::string& error);

    //解析内存中的 World 或 Prefab XML
    static std::shared_ptr<WorldDocument> ParseDocument(const std::string& text, std::string& error);

    //在主线程准备尚未激活的世界内容
    static std::unique_ptr<World> PrepareWorld(const World& current, const WorldDocument& document, std::string& error);

    //捕获指定 Ens 子树
    static std::string CaptureEns(Ens& ens);

    //保存独立预制体并清空外部场景引用
    static bool SavePrefab(Ens& ens, const std::string& path, std::string& error);

    //实例化预制体并重映射内部身份
    static Ens* InstantiatePrefab(World& world, const std::string& path, EnsId parent, std::string& error);

    //按快照复制子树：重映射内部身份后再恢复，用于复制仍活着的子树
    static Ens* CopyEns(World& world, const std::string& snapshot, EnsId parent, std::string& error);

    //恢复子树快照并保留稳定身份
    static Ens* RestoreEns(World& world, const std::string& snapshot, EnsId parent, std::string& error);

    //捕获组件全部持久化字段及其稳定身份。
    static std::string CaptureComponent(Component* component);

    //从完整快照恢复组件及其挂载位置。
    static Component* RestoreComponent(Ens& ens, const std::string& snapshot, int32 index);

    //获取最近一次 World 加载遇到的未注册组件类型。
    static const std::string& GetLastUnregisteredComponentType();

    //从 XML 文件反序列化 World
    static bool LoadXml(World& world, const std::string& path);

    //将 World 序列化到 XML 文件
    static bool SaveXml(const World& world, const std::string& path);

private:
    //重映射文档内全部稳定身份后恢复子树。clearExternal 决定是否清掉指向子树之外的引用：
    //独立预制体要清（资产不该硬链进场景），复制子树要留（复制一个引用别处的节点，引用应当照旧）。
    //新身份要经 Object 的私有入口生成，所以这里必须是本类的成员（Object 只 friend 了 WorldSerializer 本身）
    static Ens* RestoreWithNewIdentities(World& world, const WorldDocument& document, EnsId parent,
        bool clearExternal, std::string& error);
};
