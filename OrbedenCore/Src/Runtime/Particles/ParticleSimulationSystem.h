#pragma once

#include "Rendering/RenderTypes.h"
#include "Rendering/TransformCache.h"
#include "Runtime/Object/ParticleSystem.h"
#include "Runtime/Particles/ParticleSettings.h"

#include <array>

class PhysicsSystem;
class World;

//本步内安排的一次出生
struct ParticleScheduledBirth
{
public:
    //本步内的时间偏移，单位秒
    float32 offset = 0.0f;
    uint32 count = 0;
    //Burst 在 emission.bursts 中的下标；速率出生固定为 0
    uint32 burstIndex = 0;
    bool isBurst = false;
};

//一颗活粒子。position / velocity 处于该发射器选择的模拟空间。
struct ParticleRecord
{
public:
    uint64 birthId = 0;
    vector3 position;
    vector3 velocity;
    quaternion rotation;
    float32 age = 0.0f;
    float32 lifetime = 1.0f;
    float32 startSize = 1.0f;
    float32 startAngle = 0.0f;
    float32 angle = 0.0f;
    color startLinearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    uint32 startFrame = 0;
    uint32 trailSlot = 0xFFFFFFFFu;
    uint32 generation = 0;
    float32 inheritedSize = 1.0f;
    color inheritedColor = { 1.0f, 1.0f, 1.0f, 1.0f };
};

//拖尾采样点。位置与粒子处于同一空间。
struct ParticleTrailPoint
{
public:
    vector3 position;
    float64 time = 0.0;
    float32 width = 1.0f;
    color linearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float32 accumulatedLength = 0.0f;
};

//一条拖尾的环形点池索引
struct ParticleTrailRecord
{
public:
    uint64 trailId = 0;
    uint64 particleBirthId = 0;
    //粒子已消亡但仍保留点时置位
    bool attached = true;
    uint32 pointStart = 0;
    uint32 head = 0;
    uint32 count = 0;
};

//排队中的子发射事件。只存值，不保留粒子记录引用。
struct ParticleEvent
{
public:
    uint64 stepIndex = 0;
    float64 eventTime = 0.0;
    int32 sourceObjectId = 0;
    uint64 birthId = 0;
    ParticleSubEmitterEvent type = ParticleSubEmitterEvent::Birth;
    vector3 worldPosition;
    vector3 worldVelocity;
    quaternion worldRotation;
    color linearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float32 size = 1.0f;
    uint32 generation = 0;
    uint32 ruleIndex = 0;
};

//单个发射器的完整运行状态
struct ParticleEmitterState
{
public:
    Ref<ParticleSystem> source;
    //已经应用到状态上的配置版本
    uint64 appliedRevision = 0;
    ParticleSettings validatedSettings;
    ParticlePlaybackState state = ParticlePlaybackState::Stopped;
    //从 Paused 恢复时回到的状态
    ParticlePlaybackState resumeState = ParticlePlaybackState::Playing;
    //从 Play 起算的时间，包含 startDelay
    float64 elapsed = 0.0;
    //活跃模拟时间，暂停不增加，拖尾过期用它
    float64 simulationTime = 0.0;
    //不足一颗粒子的速率相位
    float64 rateAccumulator = 0.0;
    //预览上下文里只有根发射器自主发射，闭包内其它成员只接受子事件
    bool previewRoot = false;
    //Play 后需要单独执行一次的 t=0 时间线事件
    bool fireTimelineZero = false;
    //逐条子发射器规则的启用位，回边在重建图时被禁用
    List<uint8> ruleDisabled;
    uint64 nextBirthId = 1;
    uint64 nextTrailId = 1;
    uint32 randomState = 1;
    //概率判定使用独立随机流，避免影响出生属性
    uint32 eventRandomState = 1;
    bool initialized = false;
    List<ParticleRecord> particles;
    List<ParticleTrailRecord> trails;
    List<ParticleTrailPoint> trailPoints;
    List<uint32> freeTrailSlots;
    List<ParticleScheduledBirth> births;
    bounds3 worldBounds;
    ParticleSimulationStats stats;
};

//一套模拟上下文。runtime 与 preview 各自持有一份，互不共享粒子与随机状态。
class ParticleSimulationContext
{
private:
    friend class ParticleSimulationSystem;

    World* world = nullptr;
    uint64 contentRevision = 0;
    float64 accumulator = 0.0;
    uint64 stepIndex = 0;
    //按组件 ObjectId 升序，查找用 lower_bound
    List<ParticleEmitterState> emitters;
    List<ParticleEvent> events;
    //编辑态预览的根发射器 ObjectId
    List<int32> previewRoots;
    //是否为编辑态预览上下文
    bool preview = false;

    //本步派发的事件预算
    uint32 stepEventBudget = 0;
    //本步子出生的数量预算
    uint32 stepChildBudget = 0;
    //发射器变换读取自模拟系统持有的独立缓存
    TransformCache* transformCache = nullptr;
    //碰撞查询入口，为空时粒子不参与世界碰撞
    PhysicsSystem* physics = nullptr;

public:
    //设置读取发射器变换的缓存
    void SetTransformCache(TransformCache* value) { transformCache = value; }

    //设置碰撞查询入口
    void SetPhysicsSystem(PhysicsSystem* value) { physics = value; }

    //读取碰撞查询入口
    PhysicsSystem* GetPhysicsSystem() const { return physics; }

    //固定粒子步长
    static constexpr float64 FixedStep = 1.0 / 60.0;
    //单次 Advance 最多执行的步数
    static constexpr uint32 MaximumStepsPerAdvance = 8;

    //绑定世界；指针或内容序号变化时重置并重新收集
    bool BindWorld(World& currentWorld);

    //应用注册变化、启用状态与配置版本
    void SynchronizeEmitters();

    //按注册队列新增发射器，同 ObjectId 幂等
    void RegisterEmitter(ParticleSystem& system);

    //按 ObjectId 移除发射器状态
    void RemoveEmitter(int32 objectId);

    //推进固定的完整步
    void Advance(float32 deltaTime);

    //读取指定组件的状态，不存在时返回空
    ParticleEmitterState* FindEmitter(int32 objectId);
    const ParticleEmitterState* FindEmitter(int32 objectId) const;

    //读取发射器数量
    usize GetEmitterCount() const { return emitters.size(); }

    //读取指定下标的发射器
    ParticleEmitterState& GetEmitterAt(usize index) { return emitters[index]; }
    const ParticleEmitterState& GetEmitterAt(usize index) const { return emitters[index]; }

    //读取全部发射器
    const List<ParticleEmitterState>& GetEmitters() const { return emitters; }

    //合计统计
    ParticleSimulationStats CollectStats() const;

    //清空全部状态与绑定
    void Reset();

    //读取当前绑定的世界
    World* GetWorld() const { return world; }

    //是否为编辑预览上下文
    bool IsPreview() const { return preview; }

    //设置是否为编辑预览上下文
    void SetPreview(bool value) { preview = value; }

    //删除某个发射器已排队但未派发的事件
    void ResetEmitterEvents(int32 objectId);

    //标记预览根发射器
    void AddPreviewRoot(int32 objectId);

    //取消预览根发射器，并回收不再由任何根可达的状态
    void RemovePreviewRoot(int32 objectId);

    //读取预览根数量
    usize GetPreviewRootCount() const { return previewRoots.size(); }

    //重置预览根独占的可达状态，保留共享目标
    void ResetEmitterForPreview(int32 objectId);

private:
    //按子发射器规则展开一个根集合能到达的发射器编号，用于预览状态的归属判定
    void CollectReachableEmitters(const List<int32>& seeds, List<int32>& output) const;

    //执行一步模拟
    void Step(float32 step);

    //按时间线安排本步的出生
    void ScheduleBirths(ParticleEmitterState& state, float32 step);

    //执行一次出生。parent 非空时按子发射器规则继承。
    void SpawnParticle(ParticleEmitterState& state, float32 remainingStep, const ParticleEvent* parent, const ParticleSubEmitterRule* rule);

    //积分一颗粒子，返回是否存活
    bool IntegrateParticle(ParticleEmitterState& state, uint32 denseIndex, float32 step);

    //在世界空间做球体扫描并处理碰撞响应，返回粒子是否仍存活
    bool CollideParticle(ParticleEmitterState& state, ParticleRecord& particle, float32 radius,
        const vector3& worldOrigin, const vector3& worldProposed, float32 remainingTime);

    //更新拖尾采样、过期与槽位归还
    void UpdateTrails(ParticleEmitterState& state, float32 step);

    //释放一条拖尾并归还槽位
    void ReleaseTrail(ParticleEmitterState& state, ParticleTrailRecord& trail);

    //重建子发射器有向图，发现回边就禁用该边
    void RefreshSubEmitterGraph();

    //派发本步排队的子发射器事件
    void DispatchEvents();

    //重建发射器的世界包围盒
    void RebuildBounds(ParticleEmitterState& state);

    //重置一个发射器，keepPlaybackIntent 保留重置前的播放意图
    void ResetEmitter(ParticleEmitterState& state, bool keepPlaybackIntent);
    /// <summary>清除粒子、尾迹及待派发事件。</summary>
    void ClearParticles(ParticleEmitterState& state);
    /// <summary>复制子发射器事件快照。</summary>
    void QueueEvent(ParticleEmitterState& state, const ParticleRecord& particle, ParticleSubEmitterEvent type, float64 time);
    /// <summary>结束粒子并处理尾迹。</summary>
    void FinishParticle(ParticleEmitterState& state, const ParticleRecord& particle);

    //按配置分配拖尾点池，关闭拖尾时不分配
    void AllocateTrailPool(ParticleEmitterState& state);

    //读取发射器的世界矩阵
    matrix4x4 GetEmitterWorldMatrix(const ParticleEmitterState& state) const;
};

#include "Application.h"

//粒子模拟系统：拥有 runtime 与 preview 两套互不共享的模拟上下文，
//维护组件注册、推进固定步模拟并派发子发射器事件。
class ParticleSimulationSystem : public IEngineSystem
{
private:
    Application* app = nullptr;
    //运行时上下文：跟随应用当前 World
    ParticleSimulationContext runtimeContext;
    //编辑预览上下文：只推进显式播放的发射器
    ParticleSimulationContext previewContext;
    //两套上下文复用的变换缓存
    TransformCache transformCache;
    //等待并入上下文的注册项
    List<Ref<ParticleSystem>> pendingRegistrations;

    //把等待注册的组件并入指定上下文
    void FlushRegistrations();

    //按当前 World 更新变换缓存并同步注册
    void PrepareWorld(ParticleSimulationContext& context, World& world);

public:
    //获取当前活动的粒子模拟系统
    static ParticleSimulationSystem* Current();

    //创建上下文并记录应用
    bool OnInitialize(Application& app) override;

    //释放全部模拟状态
    void OnShutdown() override;

    //注册发射器，同 ObjectId 幂等
    void Register(ParticleSystem& system);

    //注销发射器并删除两套状态
    void Unregister(ParticleSystem& system);

    //推进运行时模拟，在全部 LateUpdate 之后由 Application 显式调用
    void AdvanceRuntime(World& world, float32 deltaTime);

    //推进编辑预览
    void AdvancePreview(World& world, float32 deltaTime);

    //执行运行时控制命令；参数含义随命令而定，见各命令说明
    bool ControlRuntime(ParticleSystem& system, ParticleControl control, bool argument);

    //运行时手工出生，返回实际接收数量
    uint32 EmitRuntime(ParticleSystem& system, uint32 count);

    //执行编辑预览命令
    bool ControlPreview(int32 objectId, ParticlePreviewAction action);

    //读取播放状态快照
    ParticlePlaybackInfo GetPlaybackInfo(int32 objectId, bool preview) const;

    //是否有正在推进的预览；只用于连续重绘门控
    bool HasRunningPreview() const;

    //预览是否仍占有画面：只要还有根，暂停期间的粒子与拖尾也要继续画
    bool HasPreviewContent() const;

    //读取指定上下文，供渲染快照使用
    const ParticleSimulationContext& GetRenderContext(bool preview) const;

    //读取模拟系统持有的变换缓存，粒子快照按它换算世界变换
    const TransformCache& GetTransformCache() const { return transformCache; }

    //清空全部上下文与绑定
    void ResetContexts();
};
