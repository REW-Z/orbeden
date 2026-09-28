#include "Runtime/Particles/ParticleSimulationSystem.h"

#include "Log/Log.h"
#include "Physics/PhysicsSystem.h"
#include "Runtime/Object/Ens.h"

#include <algorithm>

namespace
{
    ParticleSimulationSystem* currentSimulationSystem = nullptr;

    //暂停一个发射器：记下恢复时要回到的状态，已经停下的不动。
    //运行时命令与编辑预览共用这一份，避免两处各写一遍状态迁移。
    void PauseEmitterState(ParticleEmitterState& state)
    {
        if (state.state != ParticlePlaybackState::Playing && state.state != ParticlePlaybackState::Draining) return;
        state.resumeState = state.state;
        state.state = ParticlePlaybackState::Paused;
    }
}

ParticleSimulationSystem* ParticleSimulationSystem::Current()
{
    return currentSimulationSystem;
}

bool ParticleSimulationSystem::OnInitialize(Application& application)
{
    app = &application;
    runtimeContext.SetTransformCache(&transformCache);
    previewContext.SetTransformCache(&transformCache);
    runtimeContext.SetPhysicsSystem(application.GetSystem<PhysicsSystem>());
    previewContext.SetPhysicsSystem(runtimeContext.GetPhysicsSystem());
    previewContext.SetPreview(true);
    currentSimulationSystem = this;
    return true;
}


void ParticleSimulationSystem::OnShutdown()
{
    ResetContexts();
    if (currentSimulationSystem == this) currentSimulationSystem = nullptr;
    app = nullptr;
}

void ParticleSimulationSystem::ResetContexts()
{
    runtimeContext.Reset();
    previewContext.Reset();
    pendingRegistrations.clear();
    transformCache.Reset();
}

void ParticleSimulationSystem::Register(ParticleSystem& system)
{
    int32 objectId = system.GetObjectId();
    if (objectId <= 0) return;

    for (const Ref<ParticleSystem>& pending : pendingRegistrations)
    {
        ParticleSystem* existing = pending.Get();
        if (existing && existing->GetObjectId() == objectId) return;
    }

    Ref<ParticleSystem> reference;
    reference.Set(&system);
    pendingRegistrations.push_back(reference);
}

void ParticleSimulationSystem::Unregister(ParticleSystem& system)
{
    int32 objectId = system.GetObjectId();
    if (objectId <= 0) return;

    pendingRegistrations.erase(std::remove_if(pendingRegistrations.begin(), pendingRegistrations.end(),
        [objectId](const Ref<ParticleSystem>& pending)
        {
            ParticleSystem* existing = pending.Get();
            return !existing || existing->GetObjectId() == objectId;
        }), pendingRegistrations.end());

    runtimeContext.RemoveEmitter(objectId);
    previewContext.RemoveEmitter(objectId);
}

void ParticleSimulationSystem::FlushRegistrations()
{
    for (const Ref<ParticleSystem>& reference : pendingRegistrations)
    {
        ParticleSystem* source = reference.Get();
        if (!source) continue;
        if (source->GetWorld() == runtimeContext.GetWorld()) runtimeContext.RegisterEmitter(*source);
        if (source->GetWorld() == previewContext.GetWorld()) previewContext.RegisterEmitter(*source);
    }
    pendingRegistrations.clear();
}


void ParticleSimulationSystem::PrepareWorld(ParticleSimulationContext& context, World& world)
{
    bool changed = context.BindWorld(world);
    transformCache.Update(world);
    if (changed)
        world.ForEachComponent<ParticleSystem>([&](ParticleSystem* source) { if (source) context.RegisterEmitter(*source); });
    FlushRegistrations();
    context.SynchronizeEmitters();
}


void ParticleSimulationSystem::AdvanceRuntime(World& world, float32 deltaTime)
{
    PrepareWorld(runtimeContext, world);
    runtimeContext.Advance(deltaTime);
}


void ParticleSimulationSystem::AdvancePreview(World& world, float32 deltaTime)
{
    PrepareWorld(previewContext, world);
    //只同步查询场景是有开销的，没有推进中的预览就跳过
    if (HasRunningPreview())
    {
        if (PhysicsSystem* physics = previewContext.GetPhysicsSystem()) physics->SynchronizeQueries(world);
    }

    //暂停或停止时也要进 Advance：它会在没有活动发射器时清掉累积器，恢复后不会补上暂停的那段帧时间
    previewContext.Advance(deltaTime);
}


bool ParticleSimulationSystem::ControlRuntime(ParticleSystem& system, ParticleControl control, bool argument)
{
    World* world = system.GetWorld();
    Ens* ens = system.GetEns();
    if (!app || world != &app->GetWorld() || world->IsPreparing() || !ens || !system.GetEnabled() || !ens->GetWorldActive()) return false;
    PrepareWorld(runtimeContext, *world);
    ParticleEmitterState* state = runtimeContext.FindEmitter(system.GetObjectId());
    if (!state || !state->initialized) return false;

    switch (control)
    {
    case ParticleControl::Play:
        if (argument || state->state == ParticlePlaybackState::Stopped)
        {
            runtimeContext.ResetEmitter(*state, false);
            state->state = ParticlePlaybackState::Playing;
        }
        else if (state->state == ParticlePlaybackState::Paused) state->state = state->resumeState;
        else if (state->state == ParticlePlaybackState::Draining)
        {
            state->state = ParticlePlaybackState::Playing;
            state->elapsed = state->rateAccumulator = 0.0;
            state->fireTimelineZero = true;
        }
        break;
    case ParticleControl::Pause:
        PauseEmitterState(*state);
        break;
    case ParticleControl::Stop:
        if (argument)
        {
            runtimeContext.ClearParticles(*state);
            state->state = ParticlePlaybackState::Stopped;
            state->fireTimelineZero = false;
        }
        else if (state->state != ParticlePlaybackState::Stopped) state->state = ParticlePlaybackState::Draining;
        break;
    case ParticleControl::Clear:
        runtimeContext.ClearParticles(*state);
        break;
    default: return false;
    }
    return true;
}


uint32 ParticleSimulationSystem::EmitRuntime(ParticleSystem& system, uint32 count)
{
    World* world = system.GetWorld();
    Ens* ens = system.GetEns();
    if (!app || world != &app->GetWorld() || world->IsPreparing() || !system.GetEnabled() || !ens || !ens->GetWorldActive()) return 0;
    PrepareWorld(runtimeContext, *world);
    ParticleEmitterState* state = runtimeContext.FindEmitter(system.GetObjectId());
    if (!state || !state->initialized) return 0;
    uint64 before = state->stats.emittedParticles;
    uint32 attempts = std::min(count, 65536u);
    for (uint32 i = 0; i < attempts; ++i) runtimeContext.SpawnParticle(*state, 0.0f, nullptr, nullptr);
    state->stats.rejectedCapacity += count - attempts;
    AdvanceRandomState(state->randomState, static_cast<uint64>(count - attempts) * 16);
    uint32 accepted = static_cast<uint32>(state->stats.emittedParticles - before);
    if (accepted && state->state == ParticlePlaybackState::Stopped) state->state = ParticlePlaybackState::Draining;
    state->stats.aliveParticles = state->particles.size();
    return accepted;
}


bool ParticleSimulationSystem::ControlPreview(int32 objectId, ParticlePreviewAction action)
{
    if (!app || app->IsSimulationEnabled()) return false;
    PrepareWorld(previewContext, app->GetWorld());
    ParticleEmitterState* state = previewContext.FindEmitter(objectId);
    if (!state || !state->initialized) return false;
    ParticleSystem* source = state->source.Get();
    if (!source || !source->GetEnabled() || !source->GetEns()->GetWorldActive()) return false;
    switch (action)
    {
    case ParticlePreviewAction::Play:
    {
        if (!state) return false;
        //只有已经注册到这个上下文里的组件才能成为预览根
        previewContext.AddPreviewRoot(objectId);
        if (state->state == ParticlePlaybackState::Paused)
        {
            state->state = state->resumeState == ParticlePlaybackState::Draining
                ? ParticlePlaybackState::Draining : ParticlePlaybackState::Playing;
        }
        else if (state->state == ParticlePlaybackState::Stopped)
        {
            previewContext.ResetEmitter(*state, false);
            state->state = ParticlePlaybackState::Playing;
        }
        return true;
    }

    case ParticlePreviewAction::Pause:
        PauseEmitterState(*state);
        return true;

    case ParticlePreviewAction::Reset:
        if (!state) return false;
        previewContext.ResetEmitterForPreview(objectId);
        return true;

    case ParticlePreviewAction::Stop:
        previewContext.RemovePreviewRoot(objectId);
        return true;

    default:
        return false;
    }
}

ParticlePlaybackInfo ParticleSimulationSystem::GetPlaybackInfo(int32 objectId, bool preview) const
{
    const ParticleSimulationContext& context = preview ? previewContext : runtimeContext;
    ParticlePlaybackInfo info;
    const ParticleEmitterState* state = context.FindEmitter(objectId);
    if (!state) return info;

    info.state = state->state;
    info.time = static_cast<float32>(state->elapsed);
    info.aliveCount = static_cast<uint32>(state->particles.size());
    info.trailCount = static_cast<uint32>(state->trails.size());
    info.emittedCount = state->stats.emittedParticles;
    info.rejectedCount = state->stats.rejectedCapacity;
    return info;
}

bool ParticleSimulationSystem::HasRunningPreview() const
{
    if (previewContext.GetPreviewRootCount() == 0) return false;
    for (usize index = 0; index < previewContext.GetEmitterCount(); ++index)
    {
        ParticlePlaybackState state = previewContext.GetEmitterAt(index).state;
        if (state == ParticlePlaybackState::Playing || state == ParticlePlaybackState::Draining) return true;
    }

    return false;
}

bool ParticleSimulationSystem::HasPreviewContent() const
{
    return previewContext.GetPreviewRootCount() != 0;
}

const ParticleSimulationContext& ParticleSimulationSystem::GetRenderContext(bool preview) const
{
    return preview ? previewContext : runtimeContext;
}


#include "Rendering/ColorSpace.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Particles/ParticleSettings.h"
#include "Runtime/World.h"
#include "Runtime/Object/Transform.h"

#include <cmath>
#include <functional>
#include <tuple>

namespace
{
    //循环诊断的固定前缀，重建图时用它撤掉上一轮的旧警告
    constexpr const char* CycleErrorPrefix = "Sub-emitter cycle disabled: ";
    //每个候选出生固定消费的随机样本数量，模块开关不改变消费数量
    constexpr uint32 BirthSampleCount = 16;
    //每发射器每步实际尝试出生的数量上限
    constexpr uint32 MaximumBirthsPerStep = 65536u;
    //物理系统不可用时的默认重力
    const vector3 DefaultGravity = { 0.0f, -9.81f, 0.0f };
    //整步丢弃时保留的浮点误差容差
    constexpr float64 StepEpsilon = 1.0e-9;

    //向量缩放
    vector3 ScaleVector(const vector3& value, float32 scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    //按归一化参数在区间上做线性取值
    float32 LerpRange(const ParticleFloatRange& range, float32 u)
    {
        return range.min + (range.max - range.min) * u;
    }

    //归一化四元数；零四元数返回单位四元数
    quaternion NormalizeQuaternion(const quaternion& value)
    {
        float32 length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
        if (!(length > 1.0e-8f)) return quaternion();

        quaternion result;
        result.x = value.x / length;
        result.y = value.y / length;
        result.z = value.z / length;
        result.w = value.w / length;
        return result;
    }

    //把方向按旋转四元数变换，不平移
    vector3 RotateDirection(const quaternion& rotation, const vector3& direction)
    {
        matrix4x4 matrix = RenderMath::Rotation(rotation);
        return RenderMath::TransformDirection(matrix, direction);
    }
}

bool ParticleSimulationContext::BindWorld(World& currentWorld)
{
    if (world == &currentWorld && contentRevision == currentWorld.GetContentRevision()) return false;
    Reset();
    world = &currentWorld;
    contentRevision = currentWorld.GetContentRevision();
    return true;
}


void ParticleSimulationContext::RegisterEmitter(ParticleSystem& system)
{
    int32 objectId = system.GetObjectId();
    if (objectId <= 0 || system.GetWorld() != world) return;

    auto position = std::lower_bound(emitters.begin(), emitters.end(), objectId,
        [](const ParticleEmitterState& state, int32 value)
        {
            ParticleSystem* source = state.source.Get();
            return (source ? source->GetObjectId() : 0) < value;
        });
    if (position != emitters.end())
    {
        ParticleSystem* existing = position->source.Get();
        if (existing && existing->GetObjectId() == objectId) return;
    }

    ParticleEmitterState state;
    state.source.Set(&system);
    state.initialized = false;
    emitters.insert(position, std::move(state));
}

void ParticleSimulationContext::RemoveEmitter(int32 objectId)
{
    auto position = std::find_if(emitters.begin(), emitters.end(), [objectId](const ParticleEmitterState& state)
    {
        ParticleSystem* source = state.source.Get();
        return (source ? source->GetObjectId() : 0) == objectId;
    });
    if (position == emitters.end()) return;

    emitters.erase(position);
    previewRoots.erase(std::remove(previewRoots.begin(), previewRoots.end(), objectId), previewRoots.end());
    //源或目标已经消失的事件不再派发
    events.erase(std::remove_if(events.begin(), events.end(),
        [objectId](const ParticleEvent& event) { return event.sourceObjectId == objectId; }), events.end());
}

ParticleEmitterState* ParticleSimulationContext::FindEmitter(int32 objectId)
{
    auto position = std::find_if(emitters.begin(), emitters.end(), [objectId](const ParticleEmitterState& state)
    {
        ParticleSystem* source = state.source.Get();
        return (source ? source->GetObjectId() : 0) == objectId;
    });
    return position == emitters.end() ? nullptr : &*position;
}

const ParticleEmitterState* ParticleSimulationContext::FindEmitter(int32 objectId) const
{
    return const_cast<ParticleSimulationContext*>(this)->FindEmitter(objectId);
}

//合计所有发射器的统计
ParticleSimulationStats ParticleSimulationContext::CollectStats() const
{
    ParticleSimulationStats total;
    for (const ParticleEmitterState& state : emitters)
    {
        total.aliveParticles += state.stats.aliveParticles;
        total.activeTrails += state.stats.activeTrails;
        total.emittedParticles += state.stats.emittedParticles;
        total.rejectedCapacity += state.stats.rejectedCapacity;
        total.rejectedTrails += state.stats.rejectedTrails;
        total.collisionQueries += state.stats.collisionQueries;
        total.collisionHits += state.stats.collisionHits;
        total.subEmitterEvents += state.stats.subEmitterEvents;
        total.droppedSubEmitterEvents += state.stats.droppedSubEmitterEvents;
        total.invalidTargets += state.stats.invalidTargets;
        total.invalidTransforms += state.stats.invalidTransforms;
        total.droppedSimulationSeconds += state.stats.droppedSimulationSeconds;
    }

    return total;
}

void ParticleSimulationContext::Reset()
{
    emitters.clear();
    events.clear();
    previewRoots.clear();
    accumulator = 0.0;
    stepIndex = 0;
    stepEventBudget = 0;
    stepChildBudget = 0;
    world = nullptr;
    contentRevision = 0;
}

//读取发射器的世界矩阵
matrix4x4 ParticleSimulationContext::GetEmitterWorldMatrix(const ParticleEmitterState& state) const
{
    ParticleSystem* source = state.source.Get();
    if (!transformCache || !source) return matrix4x4();
    return transformCache->GetWorldMatrix(source->GetEnsId());
}

//按配置分配拖尾点池
void ParticleSimulationContext::AllocateTrailPool(ParticleEmitterState& state)
{
    state.trailPoints.clear();
    state.freeTrailSlots.clear();
    if (!state.validatedSettings.trails.enabled) return;

    //总量超过上限时只保留计数诊断，不分配池
    uint64 poolSize = static_cast<uint64>(state.validatedSettings.trails.maxTrails) *
        state.validatedSettings.trails.maxPointsPerTrail;
    if (poolSize > 4194304u) return;

    state.trailPoints.resize(static_cast<usize>(poolSize));
    state.freeTrailSlots.reserve(state.validatedSettings.trails.maxTrails);
    for (uint32 slot = state.validatedSettings.trails.maxTrails; slot > 0; --slot)
    {
        state.freeTrailSlots.push_back(slot - 1);
    }
}

//重置一个发射器
void ParticleSimulationContext::ResetEmitter(ParticleEmitterState& state, bool keepPlaybackIntent)
{
    ParticlePlaybackState previous = state.state;
    //ClearParticles 已经清掉粒子、拖尾与待派发事件并归还槽位，这里只补它不管的时钟与随机流
    ClearParticles(state);
    state.particles.reserve(state.validatedSettings.main.maxParticles);
    state.fireTimelineZero = true;
    state.elapsed = 0.0;
    state.simulationTime = 0.0;
    state.rateAccumulator = 0.0;
    state.nextBirthId = 1;
    state.nextTrailId = 1;
    state.randomState = state.validatedSettings.main.randomSeed;
    state.eventRandomState = state.validatedSettings.main.randomSeed ^ 0x9E3779B9u;
    if (state.eventRandomState == 0) state.eventRandomState = 1;
    state.worldBounds = bounds3();
    state.stats = ParticleSimulationStats();
    state.ruleDisabled.clear();
    AllocateTrailPool(state);

    if (!keepPlaybackIntent)
    {
        state.state = ParticlePlaybackState::Stopped;
        state.resumeState = ParticlePlaybackState::Playing;
        return;
    }

    //保留重置前的播放意图：暂停的仍然是暂停，其余回到播放
    if (previous == ParticlePlaybackState::Paused)
    {
        state.state = ParticlePlaybackState::Paused;
        state.resumeState = ParticlePlaybackState::Playing;
    }
    else if (previous == ParticlePlaybackState::Stopped || previous == ParticlePlaybackState::Draining)
    {
        state.state = ParticlePlaybackState::Stopped;
    }
    else
    {
        state.state = ParticlePlaybackState::Playing;
    }
}

void ParticleSimulationContext::SynchronizeEmitters()
{
    bool graphDirty = false;
    uint64 particlesReserved = 0, trailPointsReserved = 0;
    for (usize index = 0; index < emitters.size();)
    {
        ParticleEmitterState& state = emitters[index];
        ParticleSystem* source = state.source.Get();
        if (!source || source->GetWorld() != world)
        {
            emitters.erase(emitters.begin() + index);
            graphDirty = true;
            continue;
        }
        bool eligible = source->GetEnabled() && source->GetEns() && source->GetEns()->GetWorldActive();
        if (!eligible)
        {
            if (state.initialized) ClearParticles(state);
            state.state = ParticlePlaybackState::Stopped;
            state.initialized = false;
            ++index;
            continue;
        }
        bool first = !state.initialized;
        if (first || state.appliedRevision != source->GetConfigurationRevision())
        {
            ParticleSettings settings = source->GetSettings();
            uint64 points = settings.trails.enabled ? uint64(settings.trails.maxTrails) * settings.trails.maxPointsPerTrail : 0;
            if (particlesReserved + settings.main.maxParticles > 1048576 || trailPointsReserved + points > 4194304)
            {
                source->lastError = "Particle simulation capacity exceeded.";
                ClearParticles(state);
                state.state = ParticlePlaybackState::Stopped;
                state.initialized = false;
                ++index;
                continue;
            }
            ParticleSettings oldSimulation = state.validatedSettings, newSimulation = settings;
            oldSimulation.rendering = newSimulation.rendering = ParticleRenderSettings();
            bool reset = first || ParticleSettingsCodec::Encode(oldSimulation) != ParticleSettingsCodec::Encode(newSimulation);
            state.validatedSettings = std::move(settings);
            state.appliedRevision = source->GetConfigurationRevision();
            //加载旧场景或改过资源后也要重算一次，不指望用户在 Inspector 里再动一下字段
            source->RefreshResourceDiagnostic();
            if (reset) ResetEmitter(state, !first);
            state.initialized = true;
            if (first && !preview && state.validatedSettings.main.playOnAwake) state.state = ParticlePlaybackState::Playing;
            graphDirty = true;
        }
        particlesReserved += state.validatedSettings.main.maxParticles;
        trailPointsReserved += state.trailPoints.size();
        ++index;
    }
    if (graphDirty) RefreshSubEmitterGraph();
}


void ParticleSimulationContext::Advance(float32 deltaTime)
{
    //完全没有推进中的发射器时清空累积器，恢复后不会补上暂停期间的时间
    bool hasActive = false;
    for (const ParticleEmitterState& state : emitters)
    {
        if (state.state == ParticlePlaybackState::Playing || state.state == ParticlePlaybackState::Draining)
        {
            hasActive = true;
            break;
        }
    }
    if (!hasActive)
    {
        accumulator = 0.0;
        return;
    }

    float64 time = std::isfinite(deltaTime) && deltaTime > 0.0f ? static_cast<float64>(deltaTime) : 0.0;
    if (time > 0.25)
    {
        //超过单帧上限的差值立即计入丢弃统计
        for (ParticleEmitterState& state : emitters) state.stats.droppedSimulationSeconds += time - 0.25;
        time = 0.25;
    }

    accumulator += time;
    uint64 totalSteps = static_cast<uint64>(std::floor((accumulator + StepEpsilon) / FixedStep));
    uint64 executed = std::min<uint64>(totalSteps, MaximumStepsPerAdvance);
    if (totalSteps > executed)
    {
        float64 dropped = static_cast<float64>(totalSteps - executed) * FixedStep;
        for (ParticleEmitterState& state : emitters) state.stats.droppedSimulationSeconds += dropped;
    }

    for (uint64 step = 0; step < executed; ++step) Step(static_cast<float32>(FixedStep));

    accumulator -= static_cast<float64>(totalSteps) * FixedStep;
    //浮点误差造成的极小负余数归零
    if (accumulator < 0.0 && accumulator > -StepEpsilon) accumulator = 0.0;
}

void ParticleSimulationContext::Step(float32 step)
{
    stepEventBudget = 4096u;
    stepChildBudget = 16384u;

    for (ParticleEmitterState& state : emitters)
    {
        ParticleSystem* source = state.source.Get();
        if (!source || !state.initialized || (state.state != ParticlePlaybackState::Playing && state.state != ParticlePlaybackState::Draining)) continue;
        matrix4x4 matrix = GetEmitterWorldMatrix(state);
        bool finite = std::all_of(std::begin(matrix.m), std::end(matrix.m), [](float32 value) { return std::isfinite(value); });
        vector3 a{matrix.m[0], matrix.m[1], matrix.m[2]}, b{matrix.m[4], matrix.m[5], matrix.m[6]}, c{matrix.m[8], matrix.m[9], matrix.m[10]};
        if (state.validatedSettings.main.simulationSpace == ParticleSimulationSpace::Local && (!finite || std::fabs(RenderMath::Dot(a, RenderMath::Cross(b,c))) < 1e-8f))
        {
            ++state.stats.invalidTransforms;
            continue;
        }
        state.births.clear();
        //预览上下文里只有根发射器自主发射，闭包内其它成员只接受子事件
        bool autonomous = !preview || state.previewRoot;
        //排空阶段只积压已有粒子与拖尾，不再产生新的出生
        if (autonomous && state.state == ParticlePlaybackState::Playing)
        {
            ScheduleBirths(state, step);
        }
        //先积分已有粒子，再按时间表出生
        uint32 index = 0;
        while (index < state.particles.size())
        {
            if (IntegrateParticle(state, index, step)) ++index;
            else
            {
                FinishParticle(state, state.particles[index]);
                state.particles[index] = state.particles.back();
                state.particles.pop_back();
            }
        }

        uint32 birthBudget = MaximumBirthsPerStep;
        for (const ParticleScheduledBirth& birth : state.births)
        {
            uint32 attempts = std::min(birth.count, birthBudget);
            for (uint32 count = 0; count < attempts; ++count)
                SpawnParticle(state, std::max(0.0f, step - birth.offset), nullptr, nullptr);
            birthBudget -= attempts;
            state.stats.rejectedCapacity += birth.count - attempts;
            AdvanceRandomState(state.randomState, uint64(birth.count - attempts) * BirthSampleCount);
        }

        state.simulationTime += FixedStep;
        //采样积分后的端点
        UpdateTrails(state, step);
        RebuildBounds(state);

        //时间推进与排空判定
        if (state.state == ParticlePlaybackState::Playing || state.state == ParticlePlaybackState::Draining)
        {
            state.elapsed += FixedStep;
        }

        if (state.state == ParticlePlaybackState::Playing)
        {
            //非循环发射时间到 duration 后转为排空
            if (!state.validatedSettings.main.looping && state.elapsed >= state.validatedSettings.main.startDelay + state.validatedSettings.main.duration)
            {
                state.state = ParticlePlaybackState::Draining;
                state.resumeState = ParticlePlaybackState::Draining;
            }
        }
        else if (state.state == ParticlePlaybackState::Draining)
        {
            //最后一个粒子与最后一条拖尾消失后回到 Stopped
            if (state.particles.empty() && state.trails.empty())
            {
                state.state = ParticlePlaybackState::Stopped;
                state.elapsed = 0.0;
                state.rateAccumulator = 0.0;
            }
        }
    }

    //子发射器事件在本步末尾统一派发，新出生的子粒子从下一步开始积分
    DispatchEvents();
    for (ParticleEmitterState& state : emitters) RebuildBounds(state);
    ++stepIndex;
}

//按时间线安排本步的出生
void ParticleSimulationContext::ScheduleBirths(ParticleEmitterState& state, float32 step)
{
    const ParticleSettings& settings = state.validatedSettings;
    if (!settings.emission.enabled) return;

    float64 duration = settings.main.duration;
    float64 startDelay = settings.main.startDelay;
    bool looping = settings.main.looping;
    float64 oldTime = state.elapsed - startDelay;
    float64 newTime = oldTime + FixedStep;

    //连续速率：只对落在发射窗口内的部分积分，循环边界保留小数相位
    float64 activeStart = std::max(oldTime, 0.0);
    float64 activeEnd = looping ? newTime : std::min(newTime, duration);
    if (activeEnd > activeStart && settings.emission.rateOverTime > 0.0f)
    {
        float64 span = activeEnd - activeStart;
        float64 rate = settings.emission.rateOverTime;
        float64 total = state.rateAccumulator + rate * span;
        float64 count = std::floor(total + StepEpsilon);
        for (float64 index = 1.0; index <= count; index += 1.0)
        {
            ParticleScheduledBirth birth;
            birth.offset = static_cast<float32>((activeStart - oldTime) + (index - state.rateAccumulator) / rate);
            birth.count = 1;
            birth.isBurst = false;
            state.births.push_back(birth);
        }
        state.rateAccumulator = std::max(0.0, total - count);
    }

    //Burst：事件区间是 (oldTime, newTime]
    int64 firstLoop = static_cast<int64>(std::floor(std::max(oldTime, 0.0) / duration));
    int64 lastLoop = static_cast<int64>(std::floor(std::max(newTime, 0.0) / duration));
    if (!looping)
    {
        firstLoop = 0;
        lastLoop = oldTime >= duration ? -1 : 0;
    }

    for (int64 loop = firstLoop; loop <= lastLoop; ++loop)
    {
        if (!looping && loop != 0) break;
        for (usize burstIndex = 0; burstIndex < settings.emission.bursts.size(); ++burstIndex)
        {
            const ParticleBurst& burst = settings.emission.bursts[burstIndex];
            for (uint32 cycle = 0; cycle < burst.cycles; ++cycle)
            {
                float64 eventTime = static_cast<float64>(loop) * duration + burst.time +
                    static_cast<float64>(cycle) * burst.interval;
                if (!looping && eventTime >= duration) continue;
                //半开区间保证每个事件只触发一次
                bool atStart = state.fireTimelineZero && eventTime == 0.0 && oldTime <= StepEpsilon;
                if (!(eventTime <= newTime + StepEpsilon && (eventTime > oldTime + StepEpsilon || atStart))) continue;

                //每个计划事件都消费一个事件样本，容量是否足够不影响消费，所以抽样必须排在容量判断之前
                if (NextRandomSample(state.eventRandomState) >= burst.probability) continue;
                if (state.births.size() >= MaximumBirthsPerStep) continue;

                ParticleScheduledBirth birth;
                birth.offset = static_cast<float32>(eventTime - oldTime);
                birth.count = burst.count;
                birth.burstIndex = static_cast<uint32>(burstIndex);
                birth.isBurst = true;
                state.births.push_back(birth);
            }
        }
    }

    if (newTime >= 0.0) state.fireTimelineZero = false;

    //同刻 Burst 先于速率，Burst 按数组下标升序
    std::stable_sort(state.births.begin(), state.births.end(),
        [](const ParticleScheduledBirth& a, const ParticleScheduledBirth& b)
        {
            if (a.offset != b.offset) return a.offset < b.offset;
            if (a.isBurst != b.isBurst) return a.isBurst;
            return a.burstIndex < b.burstIndex;
        });
}

//执行一次出生
void ParticleSimulationContext::SpawnParticle(ParticleEmitterState& state, float32 remainingStep, const ParticleEvent* parent, const ParticleSubEmitterRule* rule)
{
    const ParticleSettings& settings = state.validatedSettings;
    ParticleSystem* sourceHolder = state.source.Get();

    //无论容量是否满都固定消费样本，模块开关不改变随机序列
    float32 samples[BirthSampleCount];
    for (uint32 index = 0; index < BirthSampleCount; ++index) samples[index] = NextRandomSample(state.randomState);

    if (state.particles.size() >= settings.main.maxParticles)
    {
        ++state.stats.rejectedCapacity;
        return;
    }

    //位置与方向按形状采样，方向固定朝局部 -Z 展开
    vector3 localPosition;
    vector3 direction = { 0.0f, 0.0f, -1.0f };
    switch (settings.shape.shape)
    {
    case ParticleShape::Sphere:
    {
        float32 z = 1.0f - 2.0f * samples[0];
        float32 phi = 6.28318530717958647692f * samples[1];
        float32 radial = std::sqrt(std::max(0.0f, 1.0f - z * z));
        direction = { radial * std::cos(phi), radial * std::sin(phi), z };
        float32 radius = settings.shape.surfaceOnly
            ? settings.shape.radius
            : settings.shape.radius * std::cbrt(samples[2]);
        localPosition = ScaleVector(direction, radius);
        break;
    }
    case ParticleShape::Cone:
    {
        float32 phi = 6.28318530717958647692f * samples[0];
        float32 ratio = settings.shape.surfaceOnly ? 1.0f : std::sqrt(samples[1]);
        localPosition = { settings.shape.radius * ratio * std::cos(phi),
            settings.shape.radius * ratio * std::sin(phi), 0.0f };
        float32 theta = settings.shape.coneAngle * 0.01745329251994329577f * ratio;
        direction = { std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), -std::cos(theta) };
        break;
    }
    case ParticleShape::Box:
    {
        vector3 extents = settings.shape.boxExtents;
        if (!settings.shape.surfaceOnly)
        {
            localPosition = { (2.0f * samples[0] - 1.0f) * extents.x,
                (2.0f * samples[1] - 1.0f) * extents.y,
                (2.0f * samples[2] - 1.0f) * extents.z };
            break;
        }

        //表面：按三组面的面积比例选轴，再选正负面，其它两轴均匀采样
        float32 areaX = extents.y * extents.z;
        float32 areaY = extents.x * extents.z;
        float32 areaZ = extents.x * extents.y;
        float32 total = areaX + areaY + areaZ;
        if (!(total > 0.0f))
        {
            //退化成一个点，与 Point 形状一致
            localPosition = { 0.0f, 0.0f, 0.0f };
            break;
        }

        float32 pick = samples[0] * total;
        float32 sign = samples[1] < 0.5f ? -1.0f : 1.0f;
        if (pick < areaX)
        {
            localPosition = { sign * extents.x, (2.0f * samples[2] - 1.0f) * extents.y, (2.0f * samples[3] - 1.0f) * extents.z };
        }
        else if (pick < areaX + areaY)
        {
            localPosition = { (2.0f * samples[2] - 1.0f) * extents.x, sign * extents.y, (2.0f * samples[3] - 1.0f) * extents.z };
        }
        else
        {
            localPosition = { (2.0f * samples[2] - 1.0f) * extents.x, (2.0f * samples[3] - 1.0f) * extents.y, sign * extents.z };
        }
        break;
    }
    default:
        break;
    }

    float32 lifetime = std::max(0.001f, LerpRange(settings.main.startLifetime, samples[6]));
    float32 speed = LerpRange(settings.main.startSpeed, samples[7]);
    float32 size = LerpRange(settings.main.startSize, samples[8]);
    float32 startRotation = LerpRange(settings.main.startRotation, samples[9]);

    matrix4x4 emitterWorld = GetEmitterWorldMatrix(state);
    vector3 a{emitterWorld.m[0],emitterWorld.m[1],emitterWorld.m[2]}, b{emitterWorld.m[4],emitterWorld.m[5],emitterWorld.m[6]}, c{emitterWorld.m[8],emitterWorld.m[9],emitterWorld.m[10]};
    if (!std::all_of(std::begin(emitterWorld.m), std::end(emitterWorld.m), [](float32 value) { return std::isfinite(value); }) ||
        std::fabs(RenderMath::Dot(a,RenderMath::Cross(b,c))) < 1e-8f)
    {
        ++state.stats.invalidTransforms;
        return;
    }
    bool local = settings.main.simulationSpace == ParticleSimulationSpace::Local;
    Transform* transform = sourceHolder ? world->GetTransform(sourceHolder->GetEnsId()) : nullptr;
    quaternion emitterRotation = transform ? transform->worldRotation : quaternion();
    quaternion rotation = RenderMath::Mul(parent ? parent->worldRotation : emitterRotation, RenderMath::RotationZ(startRotation));
    vector3 position = parent ? RotateDirection(parent->worldRotation, localPosition) : RenderMath::TransformPoint(emitterWorld, localPosition);
    if (parent) position = {position.x + parent->worldPosition.x, position.y + parent->worldPosition.y, position.z + parent->worldPosition.z};
    vector3 velocity = ScaleVector(RotateDirection(parent ? parent->worldRotation : emitterRotation, direction), speed);
    if (parent && rule && rule->inheritVelocity)
        velocity = {velocity.x+parent->worldVelocity.x,velocity.y+parent->worldVelocity.y,velocity.z+parent->worldVelocity.z};
    float32 inheritedSize = parent && rule && rule->inheritSize ? parent->size : 1.0f;
    color inheritedColor = parent && rule && rule->inheritColor ? parent->linearColor : color{1,1,1,1};
    if (local)
    {
        matrix4x4 inverse = RenderMath::Inverse(emitterWorld);
        position = RenderMath::TransformPoint(inverse, position);
        velocity = parent ? RenderMath::TransformDirection(inverse, velocity) : ScaleVector(direction, speed);
        quaternion inverseRotation{-emitterRotation.x,-emitterRotation.y,-emitterRotation.z,emitterRotation.w};
        rotation = RenderMath::Mul(inverseRotation, rotation);
        if (parent) size /= RenderMath::GetMaximumBasisLength(emitterWorld);
    }
    else if (!parent) size *= RenderMath::GetMaximumBasisLength(emitterWorld);

    ParticleRecord record;
    record.birthId = state.nextBirthId++;
    if (state.nextBirthId == 0)
    {
        //出生编号溢出后停止该发射器，避免重复键
        state.state = ParticlePlaybackState::Stopped;
        return;
    }
    record.position = position;
    record.velocity = velocity;
    record.rotation = rotation;
    record.age = 0.0f;
    record.lifetime = lifetime;
    record.startSize = size;
    record.startAngle = startRotation;
    record.angle = startRotation;
    record.startFrame = settings.rendering.randomStartFrame
        ? static_cast<uint32>(std::floor(samples[10] * static_cast<float32>(settings.rendering.tilesX * settings.rendering.tilesY)))
        : 0u;
    record.generation = parent ? parent->generation + 1 : 0u;
    record.inheritedSize = inheritedSize;
    record.inheritedColor = inheritedColor;

    //颜色在出生时统一转线性，之后只与渐变相乘
    record.startLinearColor = ColorSpace::SrgbToLinear(settings.main.startColor);
    //拖尾槽位按需分配，池满时只记拒绝数，不影响粒子出生
    if (settings.trails.enabled)
    {
        if (!state.freeTrailSlots.empty())
        {
            uint32 slot = state.freeTrailSlots.back();
            state.freeTrailSlots.pop_back();

            ParticleTrailRecord trail;
            trail.trailId = state.nextTrailId++;
            trail.particleBirthId = record.birthId;
            trail.attached = true;
            trail.pointStart = slot * settings.trails.maxPointsPerTrail;
            trail.head = 1;
            trail.count = 1;

            ParticleTrailPoint point;
            point.position = record.position;
            point.time = state.simulationTime;
            point.width = settings.trails.width * size;
            point.linearColor = record.startLinearColor;
            point.accumulatedLength = 0.0f;
            state.trailPoints[trail.pointStart] = point;

            record.trailSlot = slot;
            state.trails.push_back(trail);
        }
        else
        {
            ++state.stats.rejectedTrails;
        }
    }

    QueueEvent(state, record, ParticleSubEmitterEvent::Birth, stepIndex * FixedStep + (remainingStep > 0 ? FixedStep - remainingStep : 0));

    state.particles.push_back(record);
    ++state.stats.emittedParticles;

    //根出生只积分本步剩余时间，子出生从下一整步开始
    if (remainingStep > 0.0f)
    {
        if (!IntegrateParticle(state, static_cast<uint32>(state.particles.size() - 1), remainingStep))
        {
            FinishParticle(state, state.particles.back());
            state.particles.pop_back();
        }
    }
}

//积分一颗粒子，返回是否存活
bool ParticleSimulationContext::IntegrateParticle(ParticleEmitterState& state, uint32 denseIndex, float32 step)
{
    if (denseIndex >= state.particles.size()) return false;

    const ParticleSettings& settings = state.validatedSettings;
    ParticleRecord& particle = state.particles[denseIndex];

    float32 aliveTime = std::min(step, particle.lifetime - particle.age);
    if (!(aliveTime > 0.0f))
    {
        particle.age = particle.lifetime;
        return false;
    }

    //加速度 = 重力 * 倍率 + 世界加速度；局部空间要先用发射器逆 3x3 变换回去
    vector3 gravity = physics ? physics->GetGravity() : DefaultGravity;
    vector3 acceleration = {
        gravity.x * settings.motion.gravityMultiplier + settings.motion.acceleration.x,
        gravity.y * settings.motion.gravityMultiplier + settings.motion.acceleration.y,
        gravity.z * settings.motion.gravityMultiplier + settings.motion.acceleration.z,
    };
    if (settings.main.simulationSpace == ParticleSimulationSpace::Local)
    {
        matrix4x4 inverse = RenderMath::Inverse(GetEmitterWorldMatrix(state));
        acceleration = RenderMath::TransformDirection(inverse, acceleration);
    }

    particle.velocity.x += acceleration.x * aliveTime;
    particle.velocity.y += acceleration.y * aliveTime;
    particle.velocity.z += acceleration.z * aliveTime;

    //阻力按指数衰减，与积分步长无关
    if (settings.motion.drag > 0.0f)
    {
        float32 damping = std::exp(-settings.motion.drag * aliveTime);
        particle.velocity.x *= damping;
        particle.velocity.y *= damping;
        particle.velocity.z *= damping;
    }

    vector3 proposed = { particle.position.x + particle.velocity.x * aliveTime,
        particle.position.y + particle.velocity.y * aliveTime,
        particle.position.z + particle.velocity.z * aliveTime };

    //世界碰撞：局部空间的粒子先把首末位置换算到世界再扫描
    if (settings.collision.enabled && physics)
    {
        float32 normalized = particle.lifetime > 0.0f
            ? std::clamp((particle.age + aliveTime * 0.5f) / particle.lifetime, 0.0f, 1.0f) : 0.0f;
        float32 evaluatedSize = particle.startSize * particle.inheritedSize *
            std::max(0.0f, EvaluateCurve(settings.motion.sizeOverLifetime, normalized));
        //碰撞球半径取配置比例，局部空间再乘发射器的世界尺度
        float32 radius = std::max(1.0e-4f, evaluatedSize * settings.collision.radiusScale);
        matrix4x4 emitterWorld = GetEmitterWorldMatrix(state);
        if (settings.main.simulationSpace == ParticleSimulationSpace::Local)
        {
            float32 maximumBasis = RenderMath::GetMaximumBasisLength(emitterWorld);
            radius *= maximumBasis;
        }

        //尺寸为 0 的粒子不参与碰撞
        if (evaluatedSize > 0.0f)
        {
            bool localSpace = settings.main.simulationSpace == ParticleSimulationSpace::Local;
            vector3 worldOrigin = localSpace ? RenderMath::TransformPoint(emitterWorld, particle.position) : particle.position;
            vector3 worldProposed = localSpace ? RenderMath::TransformPoint(emitterWorld, proposed) : proposed;
            if (!CollideParticle(state, particle, radius, worldOrigin, worldProposed, aliveTime))
            {
                //碰撞致死的位置就是接触点
                particle.age = particle.lifetime;
                return false;
            }
            proposed = particle.position;
        }
    }

    particle.position = proposed;

    //角速度取本积分段中点处的归一化寿命
    float32 normalized = particle.lifetime > 0.0f
        ? std::clamp((particle.age + aliveTime * 0.5f) / particle.lifetime, 0.0f, 1.0f) : 0.0f;
    particle.angle += EvaluateCurve(settings.motion.angularVelocityOverLifetime, normalized) * aliveTime;

    particle.age += aliveTime;
    if (particle.age >= particle.lifetime)
    {
        particle.age = particle.lifetime;
        return false;
    }

    return true;
}

//删除某个发射器已排队但未派发的事件
void ParticleSimulationContext::ResetEmitterEvents(int32 objectId)
{
    events.erase(std::remove_if(events.begin(), events.end(),
        [objectId](const ParticleEvent& event) { return event.sourceObjectId == objectId; }), events.end());
}

//标记预览根发射器
void ParticleSimulationContext::AddPreviewRoot(int32 objectId)
{
    if (std::find(previewRoots.begin(), previewRoots.end(), objectId) != previewRoots.end()) return;
    previewRoots.push_back(objectId);

    ParticleEmitterState* state = FindEmitter(objectId);
    if (state) state->previewRoot = true;
}

//取消预览根发射器
void ParticleSimulationContext::RemovePreviewRoot(int32 objectId)
{
    previewRoots.erase(std::remove(previewRoots.begin(), previewRoots.end(), objectId), previewRoots.end());
    if (ParticleEmitterState* state = FindEmitter(objectId)) state->previewRoot = false;

    //只清理失去全部根可达性的状态：仍被另一个根共享的子发射器要留在原样
    List<int32> reachable;
    CollectReachableEmitters(previewRoots, reachable);
    for (ParticleEmitterState& candidate : emitters)
    {
        ParticleSystem* source = candidate.source.Get();
        if (!source || candidate.previewRoot) continue;
        if (std::find(reachable.begin(), reachable.end(), source->GetObjectId()) != reachable.end()) continue;
        //从未被任何预览带动过的状态保持不动，免得连带清掉它的统计
        bool idle = candidate.state == ParticlePlaybackState::Stopped &&
            candidate.particles.empty() && candidate.trails.empty();
        if (idle) continue;
        ResetEmitter(candidate, false);
    }
}

//重置预览根独占的可达状态
void ParticleSimulationContext::ResetEmitterForPreview(int32 objectId)
{
    ParticleEmitterState* state = FindEmitter(objectId);
    if (!state) return;

    //该根独占的可达状态一起清；仍被其它根共享的目标保留，避免重置另一个根的特效
    List<int32> exclusive;
    List<int32> seeds{ objectId };
    CollectReachableEmitters(seeds, exclusive);
    List<int32> otherRoots;
    for (int32 root : previewRoots) if (root != objectId) otherRoots.push_back(root);
    List<int32> shared;
    CollectReachableEmitters(otherRoots, shared);

    //保留播放与暂停意图：播放中的重置后从 0 继续播，暂停的仍然停在 0
    ResetEmitter(*state, true);
    for (int32 reachable : exclusive)
    {
        if (reachable == objectId) continue;
        if (std::find(shared.begin(), shared.end(), reachable) != shared.end()) continue;
        if (ParticleEmitterState* child = FindEmitter(reachable)) ResetEmitter(*child, false);
    }
}

//按子发射器规则展开一个根集合能到达的发射器编号
void ParticleSimulationContext::CollectReachableEmitters(const List<int32>& seeds, List<int32>& output) const
{
    output = seeds;
    for (usize index = 0; index < output.size(); ++index)
    {
        const ParticleEmitterState* state = FindEmitter(output[index]);
        ParticleSystem* source = state ? state->source.Get() : nullptr;
        if (!source) continue;

        for (const ParticleSubEmitterRule& rule : state->validatedSettings.subEmitters)
        {
            if (rule.targetSlot >= source->subEmitterTargets.size()) continue;
            Ens* targetEns = world ? world->GetEns(source->subEmitterTargets[rule.targetSlot]) : nullptr;
            ParticleSystem* target = targetEns ? targetEns->GetComponent<ParticleSystem>() : nullptr;
            if (!target || target == source) continue;

            int32 targetId = target->GetObjectId();
            if (!FindEmitter(targetId)) continue;
            if (std::find(output.begin(), output.end(), targetId) != output.end()) continue;
            output.push_back(targetId);
        }
    }
}

//在世界空间做球体扫描并处理碰撞响应
bool ParticleSimulationContext::CollideParticle(ParticleEmitterState& state, ParticleRecord& particle, float32 radius,
    const vector3& worldOrigin, const vector3& worldProposed, float32 remainingTime)
{
    const auto& settings = state.validatedSettings;
    ParticleSystem* source = state.source.Get();
    PhysicsQueryFilter filter;
    filter.layerMask = settings.collision.layerMask;
    filter.ignoredEns = source->GetEnsId();
    filter.includeTriggers = false;
    bool local = settings.main.simulationSpace == ParticleSimulationSpace::Local;
    matrix4x4 matrix = GetEmitterWorldMatrix(state), inverse = RenderMath::Inverse(matrix);
    vector3 position = worldOrigin;
    vector3 velocity = local ? RenderMath::TransformDirection(matrix, particle.velocity) : particle.velocity;
    vector3 delta{worldProposed.x-position.x,worldProposed.y-position.y,worldProposed.z-position.z};
    bool alive = true;
    for (uint32 attempt=0; attempt<2; ++attempt)
    {
        float32 distance = std::sqrt(RenderMath::Dot(delta,delta));
        if (distance <= 1e-6f) break;
        PhysicsQueryHit hit;
        ++state.stats.collisionQueries;
        bool hitSomething = physics->SweepSphereFiltered(position,radius,delta,distance,hit,filter);
        if (!hitSomething || !std::isfinite(hit.distance) || !std::isfinite(RenderMath::Dot(hit.normal,hit.normal)) || RenderMath::Dot(hit.normal,hit.normal)<1e-12f)
        {
            position = {position.x+delta.x,position.y+delta.y,position.z+delta.z};
            break;
        }
        ++state.stats.collisionHits;
        vector3 normal = RenderMath::Normalize(hit.normal);
        float32 traveled = std::clamp(hit.distance/distance,0.0f,1.0f);
        float32 skin = std::max(1e-4f,radius*0.001f);
        position = {position.x+delta.x*traveled+normal.x*skin,position.y+delta.y*traveled+normal.y*skin,position.z+delta.z*traveled+normal.z*skin};
        particle.position = local ? RenderMath::TransformPoint(inverse,position) : position;
        QueueEvent(state,particle,ParticleSubEmitterEvent::Collision,stepIndex*FixedStep+FixedStep-remainingTime+remainingTime*traveled);
        particle.age += particle.lifetime * settings.collision.lifetimeLoss;
        alive = particle.age < particle.lifetime && settings.collision.response != ParticleCollisionResponse::Kill;
        float32 vn = RenderMath::Dot(velocity,normal);
        if (vn < 0)
        {
            float32 bounce = hit.distance <= 1e-6f ? 0.0f : settings.collision.restitution;
            velocity = {(velocity.x-normal.x*vn)*(1-settings.collision.friction)-normal.x*vn*bounce,
                        (velocity.y-normal.y*vn)*(1-settings.collision.friction)-normal.y*vn*bounce,
                        (velocity.z-normal.z*vn)*(1-settings.collision.friction)-normal.z*vn*bounce};
        }
        if (!alive || hit.distance <= 1e-6f) break;
        remainingTime *= 1-traveled;
        delta = ScaleVector(velocity,remainingTime);
    }
    particle.position = local ? RenderMath::TransformPoint(inverse,position) : position;
    particle.velocity = local ? RenderMath::TransformDirection(inverse,velocity) : velocity;
    return alive;
}


//更新拖尾采样、过期与槽位归还
void ParticleSimulationContext::UpdateTrails(ParticleEmitterState& state, float32 step)
{
    const ParticleSettings& settings = state.validatedSettings;
    if (!settings.trails.enabled)
    {
        //关闭拖尾时把已有记录一起归还
        for (ParticleTrailRecord& trail : state.trails) ReleaseTrail(state, trail);
        state.trails.clear();
        state.trailPoints.clear();
        state.freeTrailSlots.clear();
        return;
    }

    uint32 capacity = settings.trails.maxPointsPerTrail;
    for (usize index = 0; index < state.trails.size();)
    {
        ParticleTrailRecord& trail = state.trails[index];

        //过期：头部点的时间早于 lifetime 就出队
        while (trail.count > 0)
        {
            uint32 tailSlot = (trail.head + capacity - trail.count) % capacity;
            const ParticleTrailPoint& tail = state.trailPoints[trail.pointStart + tailSlot];
            if (state.simulationTime - tail.time < settings.trails.lifetime) break;
            --trail.count;
        }

        //存活粒子的拖尾继续采样
        ParticleRecord* owner = nullptr;
        for (ParticleRecord& candidate : state.particles)
        {
            if (candidate.birthId == trail.particleBirthId)
            {
                owner = &candidate;
                break;
            }
        }

        if (owner && trail.attached)
        {
            if (trail.count > 0)
            {
                uint32 headSlot = (trail.head + capacity - 1) % capacity;
                const ParticleTrailPoint& last = state.trailPoints[trail.pointStart + headSlot];
                vector3 delta = { owner->position.x - last.position.x, owner->position.y - last.position.y,
                    owner->position.z - last.position.z };
                bool distanceReached = RenderMath::Dot(delta, delta) >=
                    settings.trails.minimumVertexDistance * settings.trails.minimumVertexDistance;
                bool intervalReached = state.simulationTime - last.time >= settings.trails.maximumVertexInterval;
                if (distanceReached || intervalReached)
                {
                    float32 normalizedAge = owner->lifetime > 0.0f ? std::clamp(owner->age / owner->lifetime, 0.0f, 1.0f) : 1.0f;
                    float32 evaluatedSize = owner->startSize * owner->inheritedSize *
                        std::max(0.0f, EvaluateCurve(settings.motion.sizeOverLifetime, normalizedAge));
                    color pointColor = EvaluateGradient(settings.motion.colorOverLifetime, normalizedAge);
                    pointColor.r *= owner->startLinearColor.r;
                    pointColor.g *= owner->startLinearColor.g;
                    pointColor.b *= owner->startLinearColor.b;
                    pointColor.a *= owner->startLinearColor.a;

                    ParticleTrailPoint point;
                    point.position = owner->position;
                    point.time = state.simulationTime;
                    point.width = settings.trails.width * evaluatedSize;
                    point.linearColor = pointColor;
                    point.accumulatedLength = last.accumulatedLength +
                        std::sqrt(RenderMath::Dot(delta, delta));

                    uint32 slot = trail.head;
                    //环形满时丢最旧的点
                    if (trail.count < capacity) ++trail.count;
                    state.trailPoints[trail.pointStart + slot] = point;
                    trail.head = (slot + 1) % capacity;
                }
            }
        }
        else if (!trail.attached && trail.count < 2)
        {
            //孤立尾迹没有足够点数就释放
            ReleaseTrail(state, trail);
            state.trails.erase(state.trails.begin() + static_cast<isize>(index));
            continue;
        }

        ++index;
    }

    (void)step;
    state.stats.activeTrails = state.trails.size();
}

//释放一条拖尾并归还槽位
void ParticleSimulationContext::ReleaseTrail(ParticleEmitterState& state, ParticleTrailRecord& trail)
{
    const ParticleSettings& settings = state.validatedSettings;
    uint32 capacity = std::max(1u, settings.trails.maxPointsPerTrail);
    uint32 slot = trail.pointStart / capacity;
    if (slot < settings.trails.maxTrails)
    {
        state.freeTrailSlots.push_back(slot);
    }

    trail.count = 0;
}

//重建子发射器有向图，发现回边就禁用该边
void ParticleSimulationContext::RefreshSubEmitterGraph()
{
    for (ParticleEmitterState& state : emitters)
    {
        const List<ParticleSubEmitterRule>& rules = state.validatedSettings.subEmitters;
        if (state.ruleDisabled.size() != rules.size()) state.ruleDisabled.assign(rules.size(), 0u);
        for (usize ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex) state.ruleDisabled[ruleIndex] = 0;

        //撤掉上一轮的循环诊断：循环可能已经被改掉了，留着会一直报警
        if (ParticleSystem* source = state.source.Get())
        {
            if (source->lastError.rfind(CycleErrorPrefix, 0) == 0) source->lastError.clear();
        }
    }

    //一次全局 DFS：节点按 ObjectId 升序（emitters 本身有序）作为入口，
    //灰色的目标就是回边。逐个发射器各跑一次 DFS 会把别人禁掉的回边重新放开。
    enum class Visited : uint8 { White, Gray, Black };
    List<Visited> marks(emitters.size(), Visited::White);
    List<usize> pathNodes;

    std::function<void(usize)> visit = [&](usize nodeIndex)
    {
        ParticleEmitterState& current = emitters[nodeIndex];
        ParticleSystem* currentSource = current.source.Get();
        marks[nodeIndex] = Visited::Gray;
        pathNodes.push_back(nodeIndex);

        const List<ParticleSubEmitterRule>& currentRules = current.validatedSettings.subEmitters;
        for (usize ruleIndex = 0; currentSource && ruleIndex < currentRules.size(); ++ruleIndex)
        {
            uint32 slot = currentRules[ruleIndex].targetSlot;
            if (slot >= currentSource->subEmitterTargets.size()) continue;

            Ens* targetEns = world ? world->GetEns(currentSource->subEmitterTargets[slot]) : nullptr;
            ParticleSystem* target = targetEns ? targetEns->GetComponent<ParticleSystem>() : nullptr;
            if (!target || target == currentSource) continue;

            const ParticleEmitterState* targetState = FindEmitter(target->GetObjectId());
            if (!targetState) continue;
            usize targetIndex = static_cast<usize>(targetState - emitters.data());

            if (marks[targetIndex] == Visited::Gray)
            {
                //回边：禁用该边并报出完整循环路径，继续检查其它边
                if (current.ruleDisabled.size() == currentRules.size()) current.ruleDisabled[ruleIndex] = 1;
                std::string cycle;
                for (auto step = std::find(pathNodes.begin(), pathNodes.end(), targetIndex); step != pathNodes.end(); ++step)
                {
                    ParticleSystem* node = emitters[*step].source.Get();
                    cycle += std::to_string(node ? node->GetObjectId() : 0) + " -> ";
                }

                currentSource->lastError = std::string(CycleErrorPrefix) + cycle +
                    std::to_string(target->GetObjectId()) + ".";
                continue;
            }

            if (marks[targetIndex] == Visited::White) visit(targetIndex);
        }

        pathNodes.pop_back();
        marks[nodeIndex] = Visited::Black;
    };

    for (usize index = 0; index < emitters.size(); ++index)
    {
        if (marks[index] == Visited::White) visit(index);
    }
}

//派发本步排队的子发射器事件
void ParticleSimulationContext::DispatchEvents()
{
    std::stable_sort(events.begin(),events.end(),[](const ParticleEvent& a,const ParticleEvent& b)
    { return std::tie(a.eventTime,a.sourceObjectId,a.birthId,a.type,a.ruleIndex)<std::tie(b.eventTime,b.sourceObjectId,b.birthId,b.type,b.ruleIndex); });
    usize index=0;
    while(index<events.size() && stepEventBudget)
    {
        ParticleEvent event=events[index++];
        --stepEventBudget;
        ParticleEmitterState* source=FindEmitter(event.sourceObjectId);
        if(!source || event.generation>=4 || event.ruleIndex>=source->validatedSettings.subEmitters.size()) continue;
        if(event.ruleIndex<source->ruleDisabled.size() && source->ruleDisabled[event.ruleIndex]) continue;
        const auto& rule=source->validatedSettings.subEmitters[event.ruleIndex];
        if(NextRandomSample(source->eventRandomState)>=rule.probability) continue;
        ParticleSystem* component=source->source.Get();
        Ens* ens=world->GetEns(component->subEmitterTargets[rule.targetSlot]);
        ParticleSystem* target=ens ? ens->GetComponent<ParticleSystem>() : nullptr;
        ParticleEmitterState* state=target ? FindEmitter(target->GetObjectId()) : nullptr;
        if(!state || !state->initialized || target==component || !target->GetEnabled() || !ens->GetWorldActive())
        { ++source->stats.invalidTargets; continue; }
        ++source->stats.subEmitterEvents;
        uint32 attempts=std::min(rule.count,stepChildBudget);
        uint64 before=state->stats.emittedParticles;
        for(uint32 i=0;i<attempts;++i) SpawnParticle(*state,0,&event,&rule);
        stepChildBudget-=attempts;
        state->stats.rejectedCapacity+=rule.count-attempts;
        AdvanceRandomState(state->randomState,uint64(rule.count-attempts)*BirthSampleCount);
        if(before!=state->stats.emittedParticles && state->state==ParticlePlaybackState::Stopped) state->state=ParticlePlaybackState::Draining;
    }
    for(;index<events.size();++index)
        if(auto* source=FindEmitter(events[index].sourceObjectId)) ++source->stats.droppedSubEmitterEvents;
    events.clear();
}


//重建发射器的世界包围盒
void ParticleSimulationContext::RebuildBounds(ParticleEmitterState& state)
{
    bounds3 bounds;
    matrix4x4 emitterWorld = GetEmitterWorldMatrix(state);
    bool local = state.validatedSettings.main.simulationSpace == ParticleSimulationSpace::Local;
    //局部尺寸要乘发射器世界尺度才能与渲染侧的包围盒口径一致
    float32 emitterBasis = local ? RenderMath::GetMaximumBasisLength(emitterWorld) : 1.0f;

    for (const ParticleRecord& particle : state.particles)
    {
        float32 size = particle.startSize * particle.inheritedSize *
            std::max(0.0f, EvaluateCurve(state.validatedSettings.motion.sizeOverLifetime,
                particle.lifetime > 0.0f ? std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f) : 1.0f)) * emitterBasis;
        vector3 position = local ? RenderMath::TransformPoint(emitterWorld, particle.position) : particle.position;
        //Billboard 的保守半径是外接圆，Mesh 粒子按尺寸盒近似
        float32 radius = size * 0.70710678118f;
        bounds3 particleBounds;
        particleBounds.center = position;
        particleBounds.extents = { radius, radius, radius };
        particleBounds.valid = true;
        if (!bounds.valid)
        {
            bounds = particleBounds;
            continue;
        }

        vector3 minimum = { std::min(bounds.center.x - bounds.extents.x, particleBounds.center.x - particleBounds.extents.x),
            std::min(bounds.center.y - bounds.extents.y, particleBounds.center.y - particleBounds.extents.y),
            std::min(bounds.center.z - bounds.extents.z, particleBounds.center.z - particleBounds.extents.z) };
        vector3 maximum = { std::max(bounds.center.x + bounds.extents.x, particleBounds.center.x + particleBounds.extents.x),
            std::max(bounds.center.y + bounds.extents.y, particleBounds.center.y + particleBounds.extents.y),
            std::max(bounds.center.z + bounds.extents.z, particleBounds.center.z + particleBounds.extents.z) };
        bounds.center = { (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f, (minimum.z + maximum.z) * 0.5f };
        bounds.extents = { (maximum.x - minimum.x) * 0.5f, (maximum.y - minimum.y) * 0.5f, (maximum.z - minimum.z) * 0.5f };
        bounds.valid = true;
        state.worldBounds = bounds;
    }

    state.worldBounds = bounds;
    state.stats.aliveParticles = state.particles.size();
}


/// <summary>清除活粒子并归还拖尾槽位，保留发射时钟。</summary>
void ParticleSimulationContext::ClearParticles(ParticleEmitterState& state)
{
    for (ParticleTrailRecord& trail : state.trails) ReleaseTrail(state, trail);
    state.particles.clear();
    state.trails.clear();
    state.births.clear();
    state.worldBounds = {};
    state.stats.aliveParticles = state.stats.activeTrails = 0;
    if (ParticleSystem* source = state.source.Get()) ResetEmitterEvents(source->GetObjectId());
}

/// <summary>复制事件世界快照并按匹配规则入队。</summary>
void ParticleSimulationContext::QueueEvent(ParticleEmitterState& state, const ParticleRecord& particle, ParticleSubEmitterEvent type, float64 time)
{
    ParticleSystem* source = state.source.Get();
    if (!source) return;
    const auto& settings = state.validatedSettings;
    matrix4x4 matrix = GetEmitterWorldMatrix(state);
    bool local = settings.main.simulationSpace == ParticleSimulationSpace::Local;
    Transform* transform = world->GetTransform(source->GetEnsId());
    float32 age = std::clamp(particle.age / particle.lifetime,0.0f,1.0f);
    color tint = EvaluateGradient(settings.motion.colorOverLifetime,age);
    ParticleEvent event;
    event.stepIndex=stepIndex; event.eventTime=time; event.sourceObjectId=source->GetObjectId(); event.birthId=particle.birthId; event.type=type;
    event.worldPosition = local ? RenderMath::TransformPoint(matrix,particle.position) : particle.position;
    event.worldVelocity = local ? RenderMath::TransformDirection(matrix,particle.velocity) : particle.velocity;
    event.worldRotation = RenderMath::Mul(particle.rotation,RenderMath::RotationZ(particle.angle-particle.startAngle));
    if (local && transform) event.worldRotation = RenderMath::Mul(transform->worldRotation,event.worldRotation);
    event.linearColor = {tint.r*particle.startLinearColor.r*particle.inheritedColor.r,tint.g*particle.startLinearColor.g*particle.inheritedColor.g,
        tint.b*particle.startLinearColor.b*particle.inheritedColor.b,tint.a*particle.startLinearColor.a*particle.inheritedColor.a};
    event.size = particle.startSize*particle.inheritedSize*std::max(0.0f,EvaluateCurve(settings.motion.sizeOverLifetime,age));
    if (local) event.size *= RenderMath::GetMaximumBasisLength(matrix);
    event.generation=particle.generation;
    for (uint32 i=0;i<settings.subEmitters.size();++i)
    {
        if (settings.subEmitters[i].event != type) continue;
        if (events.size() >= 4096) { ++state.stats.droppedSubEmitterEvents; continue; }
        event.ruleIndex=i;
        events.push_back(event);
    }
}

/// <summary>结束粒子寿命并保留或回收尾迹。</summary>
void ParticleSimulationContext::FinishParticle(ParticleEmitterState& state, const ParticleRecord& particle)
{
    QueueEvent(state,particle,ParticleSubEmitterEvent::Death,stepIndex*FixedStep+FixedStep);
    auto trail = std::find_if(state.trails.begin(),state.trails.end(),[&](const auto& t){ return t.attached && t.particleBirthId==particle.birthId; });
    if (trail == state.trails.end()) return;
    if (state.validatedSettings.trails.dieWithParticle)
    {
        ReleaseTrail(state,*trail);
        *trail=state.trails.back(); state.trails.pop_back();
    }
    else
    {
        uint32 cap=state.validatedSettings.trails.maxPointsPerTrail;
        ParticleTrailPoint point;
        point.position=particle.position; point.time=state.simulationTime+FixedStep;
        point.width=state.validatedSettings.trails.width*particle.startSize*particle.inheritedSize;
        point.linearColor=particle.startLinearColor;
        if (trail->count)
        {
            const auto& previous=state.trailPoints[trail->pointStart+(trail->head+cap-1)%cap];
            vector3 delta{point.position.x-previous.position.x,point.position.y-previous.position.y,point.position.z-previous.position.z};
            point.accumulatedLength=previous.accumulatedLength+std::sqrt(RenderMath::Dot(delta,delta));
        }
        state.trailPoints[trail->pointStart+trail->head]=point;
        trail->head=(trail->head+1)%cap; trail->count=std::min(cap,trail->count+1); trail->attached=false;
    }
}
