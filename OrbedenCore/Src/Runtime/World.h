#pragma once

#include "Runtime/ComponentStorage.h"
#include "Runtime/Gui/RetainedGuiTypes.h"
#include "Runtime/Object/Ens.h"
#include "Runtime/ITransformListener.h"
#include "Runtime/RenderSettings.h"

#include <span>
#include <string>
#include <type_traits>

//运行时世界
class World
{
    friend class WorldSerializer;
    friend class Ens;
    friend class Orbeden::Object;

private:
    //Ens ID槽位，保存版本和紧凑列表索引
    struct EnsSlot
    {
        Ens* value = nullptr;
        uint32 version = 0;
        uint32 denseIndex = EnsId::InvalidId;
    };

    bool preparing = false;
    bool runtimeActive = false;//世界已进入模拟或 Player 运行，static 约束开始生效
    bool dirty = false;//场景内容相对磁盘文件已有改动
    bool dirtyTrackingEnabled = true;//Play 期间关闭，运行时改动不污染磁盘脏标记
    int32 dirtySuppressionDepth = 0;//大于 0 表示正在写编辑器临时对象
    uint64 contentRevision = 0;//内容整体替换序号，原地换内容时渲染侧据此重新绑定
    List<std::pair<Object*, StringId>> preparedObjectPaths;
    List<EnsSlot> ensSlots;//按EnsId索引的稀疏槽位表
    List<Ens*> liveEns;//所有存活Ens指针
    List<uint32> freeEnsIds;//等待复用的EnsId槽位

    List<ComponentStorage*> componentStorages;//按TypeRuntimeId索引的组件稀疏集
	List<Object*> ownedObjects;//world拥有的运行时对象
    List<ITransformListener*> transformListeners;//变换监听器
    List<class IWorldLifecycleListener*> lifecycleListeners;//Ens生命周期监听器
    List<int32> removingComponents;
    List<EnsId> destroyingEns;
    List<EnsId> pendingEnsDestructions;

    //使用指定稳定ID创建Ens
    Ens* CreateEnsInternal(const std::string& name, const std::string& stableId);

    //查找组件稀疏集
    ComponentStorage* FindComponentStorage(Type* type) const;

    //获取或创建组件稀疏集
    ComponentStorage* GetOrCreateComponentStorage(Type* type);

    //遍历所有存活的Ens
    void VisitEns(EnsVisitorFunction visitor, void* userData) const;

    //生成Ens对象ID
    std::string AllocateEnsObjectPath();

    //生成未命名Ens的名称
    std::string GetEnsName(const std::string& name) const;

    //生成世界运行时对象ID
    std::string AllocateRuntimeObjectPath(Type* type);

    //接收世界拥有的运行时对象
    bool AddOwnedObject(Object* object);

    //摘除世界拥有的运行时对象
    bool RemoveOwnedObject(Object* object);

    //设置 Ens 的 localActive 并传播层级状态
    void SetEnsLocalActive(EnsId ens, bool active);

    //刷新指定 Ens 子树的 worldActive
    void RefreshEnsWorldActive(EnsId ens);

    //把 Ens 从当前父级摘下来，只动层级数据；销毁等内部操作走这里，不受 static 移动限制
    void UnlinkEns(EnsId child);

    //子树里是否存在 static 的 Ens
    bool HasStaticDescendant(EnsId ens) const;

    //校验 static 层级与物理约束，失败时输出第一个出问题的 Ens 与原因
    bool ValidateStaticConstraints(EnsId& outEns, std::string& outError) const;

public:
    RenderSettings renderSettings;

    World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    //判断世界是否正在准备尚未激活的内容
    bool IsPreparing() const { return preparing; }

    //设置世界是否已经进入模拟；进入后 static 约束与只读规则生效，暂停不解除
    void SetRuntimeActive(bool value) { runtimeActive = value; }

    //判断世界是否已经进入模拟
    bool IsRuntimeActive() const { return runtimeActive; }

    //读取 Ens 的 static 约束
    bool GetEnsStatic(EnsId ens) const;

    //设置 Ens 的 static 约束；失败返回 false 并写出原因。
    //置真要求所有祖先已经是 static，置假要求没有 static 后代，都不隐式修改其它 Ens。
    bool SetEnsStatic(EnsId ens, bool value, std::string& outError);

    //世界变换此刻是否允许变化：编辑态一律允许，模拟期间 static 的 Ens 不允许
    bool CanChangeTransform(EnsId ens) const;

    //获取内容整体替换序号
    uint64 GetContentRevision() const { return contentRevision; }

    //判断场景内容相对磁盘文件是否已有改动
    bool IsDirty() const { return dirty; }

    //标记场景内容已有改动，抑制期间自动忽略
    void SetDirty();

    //清除场景脏标记，保存成功后调用
    void ClearDirty() { dirty = false; }

    //开关脏标记记录，编辑器进入与退出 Play 时切换
    void SetDirtyTrackingEnabled(bool enabled) { dirtyTrackingEnabled = enabled; }

    //进入脏标记抑制区，写编辑器临时对象时使用
    void BeginDirtySuppression() { ++dirtySuppressionDepth; }

    //退出脏标记抑制区，必须与 BeginDirtySuppression 成对
    void EndDirtySuppression();

    //判断当前是否抑制脏标记：加载准备中、Play 中或处于抑制区
    bool IsDirtySuppressed() const { return preparing || !dirtyTrackingEnabled || dirtySuppressionDepth > 0; }

    //脏标记抑制区，离开作用域自动退出
    class DirtySuppressionScope
    {
    public:
        //进入抑制区
        explicit DirtySuppressionScope(World& world) : target(world) { target.BeginDirtySuppression(); }

        //离开抑制区
        ~DirtySuppressionScope() { target.EndDirtySuppression(); }

        DirtySuppressionScope(const DirtySuppressionScope&) = delete;
        DirtySuppressionScope& operator=(const DirtySuppressionScope&) = delete;

    private:
        World& target;
    };

    //复制句柄版本并准备独立加载容器
    void PrepareReplacement(const World& source);

    //接收准备完成的世界内容
    void CommitReplacement(World& prepared);

    //销毁世界及其运行时对象
    ~World();

    //获取当前活动世界
    static World* CurrentWorld();

    //设置当前活动世界
    static void SetCurrentWorld(World* world);

    //注册变换监听器
    void AddTransformListener(ITransformListener* listener);

    //注销变换监听器
    void RemoveTransformListener(ITransformListener* listener);

    //注册 Ens 生命周期监听器
    void AddLifecycleListener(class IWorldLifecycleListener* listener);

    //注销 Ens 生命周期监听器
    void RemoveLifecycleListener(class IWorldLifecycleListener* listener);

    //向生命周期监听器广播一次父级变化
    void NotifyEnsReparented(EnsId ens, EnsId parent);

    //通知指定节点及其子树的世界变换失效；派生写入不标脏场景
    void NotifyTransformChanged(EnsId ens, TransformChangeSource source = TransformChangeSource::Author);

    //批量写入布局派生的本地位置。整批在同一个脏抑制区内执行，只推进一次场景边界；
    //owner 是写入持有者，Transform 用它判定覆盖所有权。返回全部条目都被接受的条目数。
    ORBEDEN_BIND_IGNORE
    int32 ApplyDerivedPositions(uint64 owner, std::span<const UIDerivedPosition> positions);

    //创建Ens
    Ens* CreateEns(const std::string& name = "");

    //使用稳定ID创建Ens
    Ens* CreateEnsWithStableId(const std::string& stableId, const std::string& name = "");

    //清空世界运行时对象
    void Clear();

    //销毁Ens
    bool DestroyEns(EnsId ens);

    //判断Ens是否存活
    bool IsAlive(EnsId ens) const;

    //获取World持有的唯一Ens实例
    Ens* GetEns(EnsId ens);

    //获取World持有的唯一Ens实例
    const Ens* GetEns(EnsId ens) const;

    //获取变换组件
    Transform* GetTransform(EnsId ens) const;

    //设置父级
    void SetParent(EnsId child, EnsId parent);

    //移动Ens到指定父级，并插入到同级目标之前；目标为空时放到末尾
    bool MoveEns(EnsId child, EnsId parent, EnsId beforeSibling = EnsId());

    //获取父级
    Ens* GetParent(EnsId child) const;

    //添加组件
    Component* AddComponent(EnsId ens, Type* type);

    //添加同类型的独立组件实例
    Component* AddComponentInstance(EnsId ens, Type* type, const std::string& stablePath = "");

    //获取组件
    Component* GetComponent(EnsId ens, Type* type) const;

    //获取指定类型的全部组件实例
    void GetComponentInstances(EnsId ens, Type* type, List<Component*>& output) const;

    //移除组件
    bool RemoveComponent(EnsId ens, Type* type);

    //移除指定组件实例
    bool RemoveComponent(Component* component);

    //按稳定ID查找Ens
    Ens* FindEns(const StringId& id) const;

    //遍历所有存活的Ens
    template<typename TVisitor>
    void ForEachEns(TVisitor&& visitor) const
    {
        struct VisitorContext
        {
            TVisitor& callback;
        };

        VisitorContext context{ visitor };
        VisitEns([](Ens* ens, void* userData)
        {
            VisitorContext* visitorContext = static_cast<VisitorContext*>(userData);
            visitorContext->callback(*ens);
        }, &context);
    }

    //按精确类型遍历组件
    template<typename TVisitor>
    void ForEachComponent(Type* type, TVisitor&& visitor) const
    {
        ComponentStorage* storage = FindComponentStorage(type);
        if (!storage) return;

        storage->ForEach(visitor);
    }

    //按编译期精确类型遍历组件
    template<typename TComponent, typename TVisitor>
    void ForEachComponent(TVisitor&& visitor) const
    {
        static_assert(std::is_base_of_v<Component, TComponent>);

        ComponentStorage* storage = FindComponentStorage(TComponent::StaticType());
        if (!storage) return;

        storage->ForEachTyped<TComponent>(visitor);
    }
};

//接收 World 中与脚本调度有关的低频 Ens 生命周期事件。
class IWorldLifecycleListener
{
public:
    virtual ~IWorldLifecycleListener() = default;

    //Ens 的层级活动状态发生变化。
    virtual void OnEnsWorldActiveChanged(EnsId ens, bool worldActive) { (void)ens; (void)worldActive; }

    //Ens 已经创建完成并挂到世界上。
    virtual void OnEnsCreated(EnsId ens) { (void)ens; }

    //Ens 的父级发生变化；parent 为空表示移到根下。
    virtual void OnEnsReparented(EnsId ens, EnsId parent) { (void)ens; (void)parent; }

    //Ens 即将完成销毁，额外组件已经卸载。
    virtual void OnEnsDestroyed(EnsId ens) { (void)ens; }
};
