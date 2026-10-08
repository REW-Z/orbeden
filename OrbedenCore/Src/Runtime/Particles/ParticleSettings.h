#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EngineTypes.h"

#include <string>

//粒子配置值类型：全部是纯数据，不是 Object，也不放在 Runtime/Object 下。
//枚举底层固定为 uint32，序号参与持久化编码，不能重排。

//模拟空间：粒子位置与速度存在世界空间还是发射器局部空间
enum class ParticleSimulationSpace : uint32
{
    World = 0,
    Local = 1,
};

//渲染路径：CPU 展开几何合并动态缓冲，或上传实例数据
enum class ParticleRenderPath : uint32
{
    DynamicBatch = 0,
    Instanced = 1,
};

//粒子呈现方式
enum class ParticleRenderMode : uint32
{
    Billboard = 0,
    Mesh = 1,
};

//发射形状
enum class ParticleShape : uint32
{
    Point = 0,
    Sphere = 1,
    Cone = 2,
    Box = 3,
};

//曲线插值方式。段模式取左 key 的插值方式。
enum class ParticleCurveInterpolation : uint32
{
    Linear = 0,
    Cubic = 1,
    Constant = 2,
};

//碰撞响应
enum class ParticleCollisionResponse : uint32
{
    Bounce = 0,
    Kill = 1,
};

//子发射器触发时机
enum class ParticleSubEmitterEvent : uint32
{
    Birth = 0,
    Collision = 1,
    Death = 2,
};

//播放状态
enum class ParticlePlaybackState : uint32
{
    Stopped = 0,
    Playing = 1,
    Paused = 2,
    Draining = 3,
};

//编辑态预览命令
enum class ParticlePreviewAction : uint32
{
    Play = 0,
    Pause = 1,
    Reset = 2,
    Stop = 3,
};

//区间取值，出生时在下一次随机数上做 lerp
struct ParticleFloatRange
{
public:
    float32 min = 1.0f;
    float32 max = 1.0f;
};

//曲线关键帧
struct ParticleCurveKey
{
public:
    float32 time = 0.0f;
    float32 value = 1.0f;
    float32 inTangent = 0.0f;
    float32 outTangent = 0.0f;
    ParticleCurveInterpolation interpolation = ParticleCurveInterpolation::Linear;
};

//默认曲线：两端值都是 1 的直线
List<ParticleCurveKey> CreateUnitCurveKeys();

//默认曲线：两端值都是 0 的直线
List<ParticleCurveKey> CreateZeroCurveKeys();

//默认渐变：两端白色
struct ParticleGradientKey
{
public:
    float32 time = 0.0f;
    color value = { 1.0f, 1.0f, 1.0f, 1.0f };
};

List<ParticleGradientKey> CreateDefaultGradientKeys();

//归一化时间到取值的曲线，key 按时间严格递增
struct ParticleCurve
{
public:
    List<ParticleCurveKey> keys = CreateUnitCurveKeys();
};

//归一化时间到颜色的渐变，RGB 在线性空间插值，Alpha 单独线性插值
struct ParticleGradient
{
public:
    List<ParticleGradientKey> keys = CreateDefaultGradientKeys();
};

//一次批量发射
struct ParticleBurst
{
public:
    float32 time = 0.0f;
    uint32 count = 10;
    uint32 cycles = 1;
    float32 interval = 0.1f;
    float32 probability = 1.0f;
};

//一条子发射规则，通过槽号定位目标发射器
struct ParticleSubEmitterRule
{
public:
    uint32 targetSlot = 0;
    ParticleSubEmitterEvent event = ParticleSubEmitterEvent::Birth;
    uint32 count = 1;
    float32 probability = 1.0f;
    bool inheritVelocity = false;
    bool inheritColor = false;
    bool inheritSize = false;
};

//主模块：容量、时间线与出生属性
struct ParticleMainSettings
{
public:
    uint32 maxParticles = 4096;
    float32 duration = 5.0f;
    bool looping = true;
    bool playOnAwake = true;
    float32 startDelay = 0.0f;
    ParticleSimulationSpace simulationSpace = ParticleSimulationSpace::World;
    uint32 randomSeed = 1;
    ParticleFloatRange startLifetime = { 2.0f, 2.0f };
    ParticleFloatRange startSpeed = { 1.0f, 1.0f };
    ParticleFloatRange startSize = { 1.0f, 1.0f };
    ParticleFloatRange startRotation = { 0.0f, 0.0f };
    color startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
};

//发射模块：连续速率与 Burst
struct ParticleEmissionSettings
{
public:
    bool enabled = true;
    float32 rateOverTime = 10.0f;
    List<ParticleBurst> bursts;
};

//形状模块
struct ParticleShapeSettings
{
public:
    ParticleShape shape = ParticleShape::Point;
    float32 radius = 0.5f;
    vector3 boxExtents = { 0.5f, 0.5f, 0.5f };
    float32 coneAngle = 25.0f;
    bool surfaceOnly = false;
};

//运动模块
struct ParticleMotionSettings
{
public:
    float32 gravityMultiplier = 0.0f;
    vector3 acceleration = { 0.0f, 0.0f, 0.0f };
    float32 drag = 0.0f;
    ParticleCurve sizeOverLifetime;
    ParticleCurve angularVelocityOverLifetime = { CreateZeroCurveKeys() };
    ParticleGradient colorOverLifetime;
};

//碰撞模块
struct ParticleCollisionSettings
{
public:
    bool enabled = false;
    uint32 layerMask = 0xFFFFFFFFu;
    float32 radiusScale = 0.5f;
    float32 restitution = 0.5f;
    float32 friction = 0.0f;
    float32 lifetimeLoss = 0.0f;
    ParticleCollisionResponse response = ParticleCollisionResponse::Bounce;
};

//拖尾模块
struct ParticleTrailSettings
{
public:
    bool enabled = false;
    float32 lifetime = 0.5f;
    float32 minimumVertexDistance = 0.05f;
    float32 maximumVertexInterval = 0.05f;
    uint32 maxPointsPerTrail = 32;
    uint32 maxTrails = 4096;
    float32 width = 0.2f;
    ParticleCurve widthOverLength;
    ParticleGradient colorOverLength;
    bool dieWithParticle = false;
    float32 textureTileLength = 1.0f;
};

//渲染模块
struct ParticleRenderSettings
{
public:
    ParticleRenderPath path = ParticleRenderPath::Instanced;
    ParticleRenderMode mode = ParticleRenderMode::Billboard;
    BlendMode blendMode = BlendMode::Alpha;
    uint32 tilesX = 1;
    uint32 tilesY = 1;
    float32 animationCycles = 1.0f;
    bool randomStartFrame = false;
};

//完整粒子配置。字段顺序是持久化编码顺序，不能重排。
struct ParticleSettings
{
public:
    ParticleMainSettings main;
    ParticleEmissionSettings emission;
    ParticleShapeSettings shape;
    ParticleMotionSettings motion;
    ParticleCollisionSettings collision;
    ParticleTrailSettings trails;
    ParticleRenderSettings rendering;
    List<ParticleSubEmitterRule> subEmitters;

    /// <summary>按字段顺序校验配置，第一项失败时把字段路径写入 error。</summary>
    static bool Validate(const ParticleSettings& value, std::string& error);

    /// <summary>只做规范化：排序 key、把种子 0 改成 1、消除负零，不改越界值。</summary>
    static void Normalize(ParticleSettings& value);
};

//解析结果，不访问 World
struct ParticleSettingsParseResult
{
public:
    bool success = false;
    ParticleSettings settings;
    std::string error;
};

/// <summary>按归一化时间求曲线值，t 会被 clamp 到 0..1。</summary>
float32 EvaluateCurve(const ParticleCurve& curve, float32 t);

/// <summary>按归一化时间求渐变颜色，RGB 在线性空间插值。</summary>
color EvaluateGradient(const ParticleGradient& gradient, float32 t);

/// <summary>在区间上取样，消费一次随机数。</summary>
float32 SampleRange(const ParticleFloatRange& range, uint32& randomState);



//粒子运行时的公开快照与统计。
//这些类型只描述结果，不含模拟内部状态。

//组件控制命令
enum class ParticleControl : uint32
{
    Play = 0,
    Pause = 1,
    Stop = 2,
    Clear = 3,
};

//播放状态快照，只取用户常用子集
struct ParticlePlaybackInfo
{
public:
    ParticlePlaybackState state = ParticlePlaybackState::Stopped;
    float32 time = 0.0f;
    uint32 aliveCount = 0;
    uint32 trailCount = 0;
    uint64 emittedCount = 0;
    uint64 rejectedCount = 0;
};

//全部为累计计数：存活类字段是当前值，其余自上次重置起累加
struct ParticleSimulationStats
{
public:
    uint64 aliveParticles = 0;
    uint64 activeTrails = 0;
    uint64 emittedParticles = 0;
    uint64 rejectedCapacity = 0;
    uint64 rejectedTrails = 0;
    uint64 collisionQueries = 0;
    uint64 collisionHits = 0;
    uint64 subEmitterEvents = 0;
    uint64 droppedSubEmitterEvents = 0;
    uint64 invalidTargets = 0;
    uint64 invalidTransforms = 0;
    //因为限制步数而被丢弃的模拟时长
    float64 droppedSimulationSeconds = 0.0;
};



//粒子配置的存储协议：递归长度前缀数组文本。
//这套文本只用于持久化与资源交换，不在检视面板展示。
class ParticleSettingsCodec
{
public:
    /// <summary>把配置编码成规范文本。</summary>
    static std::string Encode(const ParticleSettings& settings);

    /// <summary>解码配置文本；全部解析与校验通过后才写入输出。</summary>
    static bool Decode(const std::string& text, ParticleSettings& settings, std::string& error);
};

#include "Defines/types.h"

//每发射器独立的 xorshift32 随机流。
//把随机数抽出来单独成文件，是因为模拟采样与「跳过被拒绝出生的样本」共用同一套状态推进规则。

/// <summary>取一个 [0,1) 的随机样本，并推进随机状态。种子 0 按 1 处理。</summary>
float32 NextRandomSample(uint32& state);

/// <summary>按样本数量整体推进随机状态，结果与逐个消费一致。</summary>
void AdvanceRandomState(uint32& state, uint64 sampleCount);
