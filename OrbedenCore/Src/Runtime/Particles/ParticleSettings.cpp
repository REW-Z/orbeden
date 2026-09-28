#include "Runtime/Particles/ParticleSettings.h"

#include "Rendering/ColorSpace.h"

#include <algorithm>
#include <cmath>

namespace
{
    //首尾 key 的时间端点容差
    constexpr float32 EndpointTolerance = 1.0e-6f;
    //曲线与渐变允许的 key 数量范围
    constexpr usize MinimumKeyCount = 2;
    constexpr usize MaximumKeyCount = 64;
    //曲线值与切线允许的绝对值上限
    constexpr float32 MaximumCurveMagnitude = 1.0e6f;

    bool IsFinite(float32 value)
    {
        return std::isfinite(value);
    }

    bool IsFiniteVector3(const vector3& value)
    {
        return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
    }

    bool InRange(float32 value, float32 minimum, float32 maximum)
    {
        return IsFinite(value) && value >= minimum && value <= maximum;
    }

    //把字段路径与原因拼成诊断文本
    std::string DescribeField(const char* path, const char* reason)
    {
        std::string message = path;
        message += " ";
        message += reason;
        return message;
    }

    bool ValidateRange(const ParticleFloatRange& range, float32 minimum, float32 maximum, const char* path, std::string& error)
    {
        if (!IsFinite(range.min) || !IsFinite(range.max) || range.min > range.max)
        {
            error = DescribeField(path, "is not a finite ordered range");
            return false;
        }
        if (range.min < minimum || range.max > maximum)
        {
            error = DescribeField(path, "is outside the allowed range");
            return false;
        }
        return true;
    }

    //排序并消除表示差异，不改越界值
    void NormalizeCurve(ParticleCurve& curve)
    {
        std::stable_sort(curve.keys.begin(), curve.keys.end(),
            [](const ParticleCurveKey& a, const ParticleCurveKey& b) { return a.time < b.time; });
        for (ParticleCurveKey& key : curve.keys)
        {
            //-0 与 0 在编码后有不同文本，统一成 0
            if (key.time == 0.0f) key.time = 0.0f;
            if (key.value == 0.0f) key.value = 0.0f;
            if (key.inTangent == 0.0f) key.inTangent = 0.0f;
            if (key.outTangent == 0.0f) key.outTangent = 0.0f;
        }
    }

    void NormalizeGradient(ParticleGradient& gradient)
    {
        std::stable_sort(gradient.keys.begin(), gradient.keys.end(),
            [](const ParticleGradientKey& a, const ParticleGradientKey& b) { return a.time < b.time; });
        for (ParticleGradientKey& key : gradient.keys)
        {
            if (key.time == 0.0f) key.time = 0.0f;
        }
    }

    bool ValidateCurve(const ParticleCurve& curve, const char* path, std::string& error)
    {
        if (curve.keys.size() < MinimumKeyCount || curve.keys.size() > MaximumKeyCount)
        {
            error = DescribeField(path, "must have between 2 and 64 keys");
            return false;
        }

        for (usize index = 0; index < curve.keys.size(); ++index)
        {
            const ParticleCurveKey& key = curve.keys[index];
            if (!InRange(key.time, 0.0f, 1.0f))
            {
                error = std::string(path) + ".keys[" + std::to_string(index) + "].time is outside 0..1";
                return false;
            }
            if (!IsFinite(key.value) || !IsFinite(key.inTangent) || !IsFinite(key.outTangent) ||
                std::fabs(key.value) > MaximumCurveMagnitude ||
                std::fabs(key.inTangent) > MaximumCurveMagnitude || std::fabs(key.outTangent) > MaximumCurveMagnitude)
            {
                error = std::string(path) + ".keys[" + std::to_string(index) + "] has an invalid value or tangent";
                return false;
            }
            //重复时间会破坏分段查找，不替用户合并
            if (index > 0 && !(key.time > curve.keys[index - 1].time))
            {
                error = std::string(path) + ".keys[" + std::to_string(index) + "].time is not strictly increasing";
                return false;
            }
        }

        if (std::fabs(curve.keys.front().time) > EndpointTolerance ||
            std::fabs(curve.keys.back().time - 1.0f) > EndpointTolerance)
        {
            error = DescribeField(path, "must start at 0 and end at 1");
            return false;
        }

        return true;
    }

    bool ValidateGradient(const ParticleGradient& gradient, const char* path, std::string& error)
    {
        if (gradient.keys.size() < MinimumKeyCount || gradient.keys.size() > MaximumKeyCount)
        {
            error = DescribeField(path, "must have between 2 and 64 keys");
            return false;
        }

        for (usize index = 0; index < gradient.keys.size(); ++index)
        {
            const ParticleGradientKey& key = gradient.keys[index];
            if (!InRange(key.time, 0.0f, 1.0f))
            {
                error = std::string(path) + ".keys[" + std::to_string(index) + "].time is outside 0..1";
                return false;
            }
            if (!InRange(key.value.r, 0.0f, 1.0f) || !InRange(key.value.g, 0.0f, 1.0f) ||
                !InRange(key.value.b, 0.0f, 1.0f) || !InRange(key.value.a, 0.0f, 1.0f))
            {
                error = std::string(path) + ".keys[" + std::to_string(index) + "].value is outside 0..1";
                return false;
            }
            if (index > 0 && !(key.time > gradient.keys[index - 1].time))
            {
                error = std::string(path) + ".keys[" + std::to_string(index) + "].time is not strictly increasing";
                return false;
            }
        }

        if (std::fabs(gradient.keys.front().time) > EndpointTolerance ||
            std::fabs(gradient.keys.back().time - 1.0f) > EndpointTolerance)
        {
            error = DescribeField(path, "must start at 0 and end at 1");
            return false;
        }

        return true;
    }
}

List<ParticleCurveKey> CreateUnitCurveKeys()
{
    return { { 0.0f, 1.0f, 0.0f, 0.0f, ParticleCurveInterpolation::Linear },
             { 1.0f, 1.0f, 0.0f, 0.0f, ParticleCurveInterpolation::Linear } };
}

List<ParticleCurveKey> CreateZeroCurveKeys()
{
    return { { 0.0f, 0.0f, 0.0f, 0.0f, ParticleCurveInterpolation::Linear },
             { 1.0f, 0.0f, 0.0f, 0.0f, ParticleCurveInterpolation::Linear } };
}

List<ParticleGradientKey> CreateDefaultGradientKeys()
{
    return { { 0.0f, color{ 1.0f, 1.0f, 1.0f, 1.0f } }, { 1.0f, color{ 1.0f, 1.0f, 1.0f, 1.0f } } };
}

void ParticleSettings::Normalize(ParticleSettings& value)
{
    NormalizeCurve(value.motion.sizeOverLifetime);
    NormalizeCurve(value.motion.angularVelocityOverLifetime);
    NormalizeGradient(value.motion.colorOverLifetime);
    NormalizeCurve(value.trails.widthOverLength);
    NormalizeGradient(value.trails.colorOverLength);

    //种子 0 会让随机流退化成固定点，统一改成 1
    if (value.main.randomSeed == 0) value.main.randomSeed = 1;

    //消除负零，避免同一配置出现两种文本表示
    if (value.shape.radius == 0.0f) value.shape.radius = 0.0f;
    if (value.motion.drag == 0.0f) value.motion.drag = 0.0f;
    if (value.collision.lifetimeLoss == 0.0f) value.collision.lifetimeLoss = 0.0f;
    if (value.collision.friction == 0.0f) value.collision.friction = 0.0f;
    if (value.trails.minimumVertexDistance == 0.0f) value.trails.minimumVertexDistance = 0.0f;
}

bool ParticleSettings::Validate(const ParticleSettings& value, std::string& error)
{
    error.clear();

    //主模块
    if (value.main.maxParticles < 1 || value.main.maxParticles > 65536)
    {
        error = DescribeField("main.maxParticles", "must be between 1 and 65536");
        return false;
    }
    if (!InRange(value.main.duration, 0.01f, 3600.0f))
    {
        error = DescribeField("main.duration", "must be between 0.01 and 3600 seconds");
        return false;
    }
    if (!InRange(value.main.startDelay, 0.0f, 3600.0f))
    {
        error = DescribeField("main.startDelay", "must be between 0 and 3600 seconds");
        return false;
    }
    if (!ValidateRange(value.main.startLifetime, 0.001f, 3600.0f, "main.startLifetime", error)) return false;
    if (!ValidateRange(value.main.startSpeed, 0.0f, 100000.0f, "main.startSpeed", error)) return false;
    if (!ValidateRange(value.main.startSize, 0.0001f, 100000.0f, "main.startSize", error)) return false;
    if (!ValidateRange(value.main.startRotation, -360000.0f, 360000.0f, "main.startRotation", error)) return false;
    if (!InRange(value.main.startColor.r, 0.0f, 1.0f) || !InRange(value.main.startColor.g, 0.0f, 1.0f) ||
        !InRange(value.main.startColor.b, 0.0f, 1.0f) || !InRange(value.main.startColor.a, 0.0f, 1.0f))
    {
        error = DescribeField("main.startColor", "must be inside 0..1");
        return false;
    }

    //发射模块
    if (!InRange(value.emission.rateOverTime, 0.0f, 100000.0f))
    {
        error = DescribeField("emission.rateOverTime", "must be between 0 and 100000 per second");
        return false;
    }
    if (value.emission.bursts.size() > 64)
    {
        error = DescribeField("emission.bursts", "must not exceed 64 entries");
        return false;
    }
    for (usize index = 0; index < value.emission.bursts.size(); ++index)
    {
        const ParticleBurst& burst = value.emission.bursts[index];
        std::string prefix = "emission.bursts[" + std::to_string(index) + "]";
        if (burst.count > 65536)
        {
            error = prefix + ".count must not exceed 65536";
            return false;
        }
        if (burst.cycles < 1 || burst.cycles > 1024)
        {
            error = prefix + ".cycles must be between 1 and 1024";
            return false;
        }
        if (!InRange(burst.probability, 0.0f, 1.0f))
        {
            error = prefix + ".probability must be inside 0..1";
            return false;
        }
        if (!IsFinite(burst.time) || burst.time < 0.0f || burst.time >= value.main.duration)
        {
            error = prefix + ".time must be inside [0, duration)";
            return false;
        }
        if (burst.cycles > 1)
        {
            if (!InRange(burst.interval, 1.0e-6f, 3600.0f))
            {
                error = prefix + ".interval must be positive when cycles is above 1";
                return false;
            }
            //最后一次触发也必须落在 duration 之内
            if (!(burst.time + static_cast<float32>(burst.cycles - 1) * burst.interval < value.main.duration))
            {
                error = prefix + " exceeds the emission duration";
                return false;
            }
        }
    }

    //形状模块
    if (!InRange(value.shape.radius, 0.0f, 100000.0f))
    {
        error = DescribeField("shape.radius", "must be between 0 and 100000");
        return false;
    }
    if (!IsFiniteVector3(value.shape.boxExtents) || value.shape.boxExtents.x < 0.0f ||
        value.shape.boxExtents.y < 0.0f || value.shape.boxExtents.z < 0.0f ||
        value.shape.boxExtents.x > 100000.0f || value.shape.boxExtents.y > 100000.0f || value.shape.boxExtents.z > 100000.0f)
    {
        error = DescribeField("shape.boxExtents", "must be between 0 and 100000 on every axis");
        return false;
    }
    if (!InRange(value.shape.coneAngle, 0.0f, 89.0f))
    {
        error = DescribeField("shape.coneAngle", "must be between 0 and 89 degrees");
        return false;
    }

    //运动模块
    if (!InRange(value.motion.gravityMultiplier, -100.0f, 100.0f))
    {
        error = DescribeField("motion.gravityMultiplier", "must be between -100 and 100");
        return false;
    }
    if (!IsFiniteVector3(value.motion.acceleration) || std::fabs(value.motion.acceleration.x) > 100000.0f ||
        std::fabs(value.motion.acceleration.y) > 100000.0f || std::fabs(value.motion.acceleration.z) > 100000.0f)
    {
        error = DescribeField("motion.acceleration", "must stay within 100000 on every axis");
        return false;
    }
    if (!InRange(value.motion.drag, 0.0f, 1000.0f))
    {
        error = DescribeField("motion.drag", "must be between 0 and 1000 per second");
        return false;
    }
    if (!ValidateCurve(value.motion.sizeOverLifetime, "motion.sizeOverLifetime", error)) return false;
    if (!ValidateCurve(value.motion.angularVelocityOverLifetime, "motion.angularVelocityOverLifetime", error)) return false;
    if (!ValidateGradient(value.motion.colorOverLifetime, "motion.colorOverLifetime", error)) return false;

    //碰撞模块
    if (!InRange(value.collision.radiusScale, 0.0001f, 1000.0f))
    {
        error = DescribeField("collision.radiusScale", "must be between 0.0001 and 1000");
        return false;
    }
    if (!InRange(value.collision.restitution, 0.0f, 1.0f))
    {
        error = DescribeField("collision.restitution", "must be inside 0..1");
        return false;
    }
    if (!InRange(value.collision.friction, 0.0f, 1.0f))
    {
        error = DescribeField("collision.friction", "must be inside 0..1");
        return false;
    }
    if (!InRange(value.collision.lifetimeLoss, 0.0f, 1.0f))
    {
        error = DescribeField("collision.lifetimeLoss", "must be inside 0..1");
        return false;
    }

    //拖尾模块
    if (!InRange(value.trails.lifetime, 0.001f, 60.0f))
    {
        error = DescribeField("trails.lifetime", "must be between 0.001 and 60 seconds");
        return false;
    }
    if (!InRange(value.trails.minimumVertexDistance, 0.0f, 100000.0f))
    {
        error = DescribeField("trails.minimumVertexDistance", "must be between 0 and 100000");
        return false;
    }
    if (!InRange(value.trails.maximumVertexInterval, 1.0f / 240.0f, 1.0f))
    {
        error = DescribeField("trails.maximumVertexInterval", "must be between 1/240 and 1 second");
        return false;
    }
    if (value.trails.maxPointsPerTrail < 2 || value.trails.maxPointsPerTrail > 64)
    {
        error = DescribeField("trails.maxPointsPerTrail", "must be between 2 and 64");
        return false;
    }
    if (value.trails.maxTrails < 1 || value.trails.maxTrails > 65536)
    {
        error = DescribeField("trails.maxTrails", "must be between 1 and 65536");
        return false;
    }
    //平铺点池的上限，按 uint64 判定避免乘法溢出
    if (static_cast<uint64>(value.trails.maxTrails) * value.trails.maxPointsPerTrail > 1048576u)
    {
        error = DescribeField("trails.maxTrails", "times maxPointsPerTrail must not exceed 1048576");
        return false;
    }
    if (!InRange(value.trails.width, 0.0f, 100000.0f))
    {
        error = DescribeField("trails.width", "must be between 0 and 100000");
        return false;
    }
    if (!ValidateCurve(value.trails.widthOverLength, "trails.widthOverLength", error)) return false;
    if (!ValidateGradient(value.trails.colorOverLength, "trails.colorOverLength", error)) return false;
    if (!InRange(value.trails.textureTileLength, 0.0001f, 100000.0f))
    {
        error = DescribeField("trails.textureTileLength", "must be between 0.0001 and 100000");
        return false;
    }

    //渲染模块
    if (value.rendering.tilesX < 1 || value.rendering.tilesX > 256 ||
        value.rendering.tilesY < 1 || value.rendering.tilesY > 256)
    {
        error = DescribeField("rendering.tilesX", "and tilesY must be between 1 and 256");
        return false;
    }
    if (static_cast<uint64>(value.rendering.tilesX) * value.rendering.tilesY > 65536u)
    {
        error = DescribeField("rendering.tilesX", "times tilesY must not exceed 65536");
        return false;
    }
    if (!InRange(value.rendering.animationCycles, 0.0f, 1000.0f))
    {
        error = DescribeField("rendering.animationCycles", "must be between 0 and 1000");
        return false;
    }

    //子发射器
    if (value.subEmitters.size() > 16)
    {
        error = DescribeField("subEmitters", "must not exceed 16 entries");
        return false;
    }
    for (usize index = 0; index < value.subEmitters.size(); ++index)
    {
        const ParticleSubEmitterRule& rule = value.subEmitters[index];
        std::string prefix = "subEmitters[" + std::to_string(index) + "]";
        if (rule.targetSlot >= 16)
        {
            error = prefix + ".targetSlot must be below 16";
            return false;
        }
        if (rule.count < 1 || rule.count > 65536)
        {
            error = prefix + ".count must be between 1 and 65536";
            return false;
        }
        if (!InRange(rule.probability, 0.0f, 1.0f))
        {
            error = prefix + ".probability must be inside 0..1";
            return false;
        }
    }

    return true;
}

float32 EvaluateCurve(const ParticleCurve& curve, float32 t)
{
    if (curve.keys.empty()) return 0.0f;
    if (curve.keys.size() == 1) return curve.keys[0].value;

    float32 time = std::clamp(t, 0.0f, 1.0f);
    //端点直接返回末 key，避免落在最后一个区间之外
    if (time >= curve.keys.back().time) return curve.keys.back().value;
    if (time <= curve.keys.front().time) return curve.keys.front().value;

    //找到右端点，段模式取左 key 的插值方式
    auto upper = std::upper_bound(curve.keys.begin(), curve.keys.end(), time,
        [](float32 value, const ParticleCurveKey& key) { return value < key.time; });
    usize rightIndex = static_cast<usize>(upper - curve.keys.begin());
    const ParticleCurveKey& left = curve.keys[rightIndex - 1];
    const ParticleCurveKey& right = curve.keys[rightIndex];

    float32 span = right.time - left.time;
    if (!(span > 0.0f)) return left.value;

    float32 local = (time - left.time) / span;
    if (left.interpolation == ParticleCurveInterpolation::Constant) return left.value;
    if (left.interpolation == ParticleCurveInterpolation::Cubic)
    {
        //三次 Hermite：用两侧切线还原曲线形状
        float32 local2 = local * local;
        float32 local3 = local2 * local;
        float32 h00 = 2.0f * local3 - 3.0f * local2 + 1.0f;
        float32 h10 = local3 - 2.0f * local2 + local;
        float32 h01 = -2.0f * local3 + 3.0f * local2;
        float32 h11 = local3 - local2;
        return h00 * left.value + h10 * span * left.outTangent + h01 * right.value + h11 * span * right.inTangent;
    }

    return left.value + (right.value - left.value) * local;
}

color EvaluateGradient(const ParticleGradient& gradient, float32 t)
{
    if (gradient.keys.empty()) return { 1.0f, 1.0f, 1.0f, 1.0f };

    float32 time = std::clamp(t, 0.0f, 1.0f);
    //单 key 与落在端点之外都取端点值，同样要转成线性，否则端点比插值段亮一档
    if (gradient.keys.size() == 1 || time <= gradient.keys.front().time)
        return ColorSpace::SrgbToLinear(gradient.keys.front().value);
    if (time >= gradient.keys.back().time) return ColorSpace::SrgbToLinear(gradient.keys.back().value);

    auto upper = std::upper_bound(gradient.keys.begin(), gradient.keys.end(), time,
        [](float32 value, const ParticleGradientKey& key) { return value < key.time; });
    usize rightIndex = static_cast<usize>(upper - gradient.keys.begin());
    const ParticleGradientKey& left = gradient.keys[rightIndex - 1];
    const ParticleGradientKey& right = gradient.keys[rightIndex];

    float32 span = right.time - left.time;
    if (!(span > 0.0f)) return left.value;

    float32 local = (time - left.time) / span;
    //配置里的 RGB 是 sRGB，先各自转成线性再插值；Alpha 不做颜色空间转换
    color leftLinear = ColorSpace::SrgbToLinear(left.value);
    color rightLinear = ColorSpace::SrgbToLinear(right.value);
    color result;
    result.r = leftLinear.r + (rightLinear.r - leftLinear.r) * local;
    result.g = leftLinear.g + (rightLinear.g - leftLinear.g) * local;
    result.b = leftLinear.b + (rightLinear.b - leftLinear.b) * local;
    result.a = left.value.a + (right.value.a - left.value.a) * local;
    return result;
}

float32 SampleRange(const ParticleFloatRange& range, uint32& randomState)
{
    return range.min + (range.max - range.min) * NextRandomSample(randomState);
}


#include "Runtime/Reflection.h"

#include <charconv>

namespace
{
    //配置文本的总长度上限
    constexpr usize MaximumTextLength = 1024u * 1024u;
    //数组嵌套深度上限
    constexpr uint32 MaximumDepth = 8;
    //schema 版本前缀
    constexpr const char* SchemaTag = "ParticleSettings:1";
    //float32 的十进制有效位数
    constexpr int FloatDigits = 9;

    std::string EncodeFloat(float32 value)
    {
        char buffer[32] = {};
        auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, FloatDigits);
        if (result.ec != std::errc()) return "0";
        return std::string(buffer, result.ptr);
    }

    std::string EncodeUInt(uint32 value)
    {
        return std::to_string(value);
    }

    std::string EncodeBool(bool value)
    {
        return value ? "1" : "0";
    }

    std::string EncodeVector3(const vector3& value)
    {
        return Reflection::FormatArrayValues({ EncodeFloat(value.x), EncodeFloat(value.y), EncodeFloat(value.z) });
    }

    std::string EncodeColor(const color& value)
    {
        return Reflection::FormatArrayValues(
            { EncodeFloat(value.r), EncodeFloat(value.g), EncodeFloat(value.b), EncodeFloat(value.a) });
    }

    std::string EncodeRange(const ParticleFloatRange& value)
    {
        return Reflection::FormatArrayValues({ EncodeFloat(value.min), EncodeFloat(value.max) });
    }

    std::string EncodeCurveKey(const ParticleCurveKey& key)
    {
        return Reflection::FormatArrayValues({ EncodeFloat(key.time), EncodeFloat(key.value), EncodeFloat(key.inTangent),
            EncodeFloat(key.outTangent), EncodeUInt(static_cast<uint32>(key.interpolation)) });
    }

    std::string EncodeCurve(const ParticleCurve& curve)
    {
        List<std::string> keys;
        keys.reserve(curve.keys.size());
        for (const ParticleCurveKey& key : curve.keys) keys.push_back(EncodeCurveKey(key));
        return Reflection::FormatArrayValues(keys);
    }

    std::string EncodeGradientKey(const ParticleGradientKey& key)
    {
        return Reflection::FormatArrayValues({ EncodeFloat(key.time), EncodeColor(key.value) });
    }

    std::string EncodeGradient(const ParticleGradient& gradient)
    {
        List<std::string> keys;
        keys.reserve(gradient.keys.size());
        for (const ParticleGradientKey& key : gradient.keys) keys.push_back(EncodeGradientKey(key));
        return Reflection::FormatArrayValues(keys);
    }

    std::string EncodeBurst(const ParticleBurst& burst)
    {
        return Reflection::FormatArrayValues({ EncodeFloat(burst.time), EncodeUInt(burst.count), EncodeUInt(burst.cycles),
            EncodeFloat(burst.interval), EncodeFloat(burst.probability) });
    }

    std::string EncodeSubEmitterRule(const ParticleSubEmitterRule& rule)
    {
        return Reflection::FormatArrayValues({ EncodeUInt(rule.targetSlot), EncodeUInt(static_cast<uint32>(rule.event)),
            EncodeUInt(rule.count), EncodeFloat(rule.probability), EncodeBool(rule.inheritVelocity),
            EncodeBool(rule.inheritColor), EncodeBool(rule.inheritSize) });
    }

    std::string EncodeMain(const ParticleMainSettings& value)
    {
        return Reflection::FormatArrayValues({ EncodeUInt(value.maxParticles), EncodeFloat(value.duration),
            EncodeBool(value.looping), EncodeBool(value.playOnAwake), EncodeFloat(value.startDelay),
            EncodeUInt(static_cast<uint32>(value.simulationSpace)), EncodeUInt(value.randomSeed),
            EncodeRange(value.startLifetime), EncodeRange(value.startSpeed), EncodeRange(value.startSize),
            EncodeRange(value.startRotation), EncodeColor(value.startColor) });
    }

    std::string EncodeEmission(const ParticleEmissionSettings& value)
    {
        List<std::string> bursts;
        bursts.reserve(value.bursts.size());
        for (const ParticleBurst& burst : value.bursts) bursts.push_back(EncodeBurst(burst));
        return Reflection::FormatArrayValues({ EncodeBool(value.enabled), EncodeFloat(value.rateOverTime),
            Reflection::FormatArrayValues(bursts) });
    }

    std::string EncodeShape(const ParticleShapeSettings& value)
    {
        return Reflection::FormatArrayValues({ EncodeUInt(static_cast<uint32>(value.shape)), EncodeFloat(value.radius),
            EncodeVector3(value.boxExtents), EncodeFloat(value.coneAngle), EncodeBool(value.surfaceOnly) });
    }

    std::string EncodeMotion(const ParticleMotionSettings& value)
    {
        return Reflection::FormatArrayValues({ EncodeFloat(value.gravityMultiplier), EncodeVector3(value.acceleration),
            EncodeFloat(value.drag), EncodeCurve(value.sizeOverLifetime), EncodeCurve(value.angularVelocityOverLifetime),
            EncodeGradient(value.colorOverLifetime) });
    }

    std::string EncodeCollision(const ParticleCollisionSettings& value)
    {
        return Reflection::FormatArrayValues({ EncodeBool(value.enabled), EncodeUInt(value.layerMask),
            EncodeFloat(value.radiusScale), EncodeFloat(value.restitution), EncodeFloat(value.friction),
            EncodeFloat(value.lifetimeLoss), EncodeUInt(static_cast<uint32>(value.response)) });
    }

    std::string EncodeTrails(const ParticleTrailSettings& value)
    {
        return Reflection::FormatArrayValues({ EncodeBool(value.enabled), EncodeFloat(value.lifetime),
            EncodeFloat(value.minimumVertexDistance), EncodeFloat(value.maximumVertexInterval),
            EncodeUInt(value.maxPointsPerTrail), EncodeUInt(value.maxTrails), EncodeFloat(value.width),
            EncodeCurve(value.widthOverLength), EncodeGradient(value.colorOverLength), EncodeBool(value.dieWithParticle),
            EncodeFloat(value.textureTileLength) });
    }

    std::string EncodeRendering(const ParticleRenderSettings& value)
    {
        return Reflection::FormatArrayValues({ EncodeUInt(static_cast<uint32>(value.path)),
            EncodeUInt(static_cast<uint32>(value.mode)), EncodeUInt(static_cast<uint32>(value.blendMode)),
            EncodeUInt(value.tilesX), EncodeUInt(value.tilesY), EncodeFloat(value.animationCycles),
            EncodeBool(value.randomStartFrame) });
    }

    //解码上下文：跟踪嵌套深度并累积第一条诊断
    class DecodeContext
    {
    public:
        std::string error;
        uint32 depth = 0;
    };

    //读取一层长度前缀数组
    bool ReadArray(DecodeContext& context, const std::string& text, List<std::string>& values, const char* path)
    {
        if (context.depth >= MaximumDepth)
        {
            context.error = std::string("ParticleSettings nesting is too deep at ") + path;
            return false;
        }
        if (!Reflection::ParseArrayValues(text, values))
        {
            context.error = std::string("ParticleSettings malformed array at ") + path;
            return false;
        }
        return true;
    }

    //进入一层嵌套
    struct DepthGuard
    {
        DecodeContext& context;
        explicit DepthGuard(DecodeContext& value) : context(value) { ++context.depth; }
        ~DepthGuard() { --context.depth; }
    };

    bool ParseFloat(DecodeContext& context, const std::string& text, float32& value, const char* path)
    {
        if (text.empty())
        {
            context.error = std::string("ParticleSettings empty number at ") + path;
            return false;
        }

        auto result = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
        if (result.ec != std::errc() || result.ptr != text.data() + text.size() || !std::isfinite(value))
        {
            context.error = std::string("ParticleSettings invalid number at ") + path;
            return false;
        }
        return true;
    }

    bool ParseUInt(DecodeContext& context, const std::string& text, uint32& value, uint32 maximum, const char* path)
    {
        if (text.empty())
        {
            context.error = std::string("ParticleSettings empty integer at ") + path;
            return false;
        }

        uint64 parsed = 0;
        auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec != std::errc() || result.ptr != text.data() + text.size() || parsed > maximum)
        {
            context.error = std::string("ParticleSettings invalid integer at ") + path;
            return false;
        }
        value = static_cast<uint32>(parsed);
        return true;
    }

    bool ParseBool(DecodeContext& context, const std::string& text, bool& value, const char* path)
    {
        uint32 parsed = 0;
        if (!ParseUInt(context, text, parsed, 1, path)) return false;
        value = parsed != 0;
        return true;
    }

    //按数量与每项回调解析数组
    bool ParseArrayOf(DecodeContext& context, const std::string& text, usize expectedCount,
        void* userData, bool (*parseItem)(DecodeContext&, const std::string&, void*, usize), const char* path)
    {
        DepthGuard guard(context);
        List<std::string> values;
        if (!ReadArray(context, text, values, path)) return false;
        if (values.size() != expectedCount)
        {
            context.error = std::string("ParticleSettings wrong item count at ") + path;
            return false;
        }
        for (usize index = 0; index < values.size(); ++index)
        {
            if (!parseItem(context, values[index], userData, index)) return false;
        }
        return true;
    }

    bool ParseVector3(DecodeContext& context, const std::string& text, vector3& value, const char* path)
    {
        float32 components[3] = {};
        return ParseArrayOf(context, text, 3, components, [](DecodeContext& inner, const std::string& item, void* data, usize index)
        {
            return ParseFloat(inner, item, static_cast<float32*>(data)[index], "vector component");
        }, path);
    }

    bool ParseColor(DecodeContext& context, const std::string& text, color& value, const char* path)
    {
        float32 components[4] = {};
        if (!ParseArrayOf(context, text, 4, components, [](DecodeContext& inner, const std::string& item, void* data, usize index)
        {
            return ParseFloat(inner, item, static_cast<float32*>(data)[index], "color component");
        }, path)) return false;

        value = { components[0], components[1], components[2], components[3] };
        return true;
    }

    bool ParseRange(DecodeContext& context, const std::string& text, ParticleFloatRange& value, const char* path)
    {
        DepthGuard guard(context);
        List<std::string> values;
        if (!ReadArray(context, text, values, path)) return false;
        if (values.size() != 2)
        {
            context.error = std::string("ParticleSettings wrong item count at ") + path;
            return false;
        }
        return ParseFloat(context, values[0], value.min, path) && ParseFloat(context, values[1], value.max, path);
    }

    bool ParseCurve(DecodeContext& context, const std::string& text, ParticleCurve& value, const char* path)
    {
        DepthGuard guard(context);
        List<std::string> keys;
        if (!ReadArray(context, text, keys, path)) return false;

        value.keys.clear();
        value.keys.reserve(keys.size());
        for (const std::string& keyText : keys)
        {
            DepthGuard keyGuard(context);
            List<std::string> fields;
            if (!ReadArray(context, keyText, fields, path)) return false;
            if (fields.size() != 5)
            {
                context.error = std::string("ParticleSettings curve key must have 5 fields at ") + path;
                return false;
            }

            ParticleCurveKey key;
            uint32 interpolation = 0;
            if (!ParseFloat(context, fields[0], key.time, path) || !ParseFloat(context, fields[1], key.value, path) ||
                !ParseFloat(context, fields[2], key.inTangent, path) || !ParseFloat(context, fields[3], key.outTangent, path) ||
                !ParseUInt(context, fields[4], interpolation, static_cast<uint32>(ParticleCurveInterpolation::Constant), path))
            {
                return false;
            }
            key.interpolation = static_cast<ParticleCurveInterpolation>(interpolation);
            value.keys.push_back(key);
        }

        return true;
    }

    bool ParseGradient(DecodeContext& context, const std::string& text, ParticleGradient& value, const char* path)
    {
        DepthGuard guard(context);
        List<std::string> keys;
        if (!ReadArray(context, text, keys, path)) return false;

        value.keys.clear();
        value.keys.reserve(keys.size());
        for (const std::string& keyText : keys)
        {
            DepthGuard keyGuard(context);
            List<std::string> fields;
            if (!ReadArray(context, keyText, fields, path)) return false;
            if (fields.size() != 2)
            {
                context.error = std::string("ParticleSettings gradient key must have 2 fields at ") + path;
                return false;
            }

            ParticleGradientKey key;
            if (!ParseFloat(context, fields[0], key.time, path) || !ParseColor(context, fields[1], key.value, path)) return false;
            value.keys.push_back(key);
        }

        return true;
    }

    bool ParseBurst(DecodeContext& context, const std::string& text, ParticleBurst& value, const char* path)
    {
        DepthGuard guard(context);
        List<std::string> fields;
        if (!ReadArray(context, text, fields, path)) return false;
        if (fields.size() != 5)
        {
            context.error = std::string("ParticleSettings burst must have 5 fields at ") + path;
            return false;
        }

        return ParseFloat(context, fields[0], value.time, path) &&
            ParseUInt(context, fields[1], value.count, 65536u, path) &&
            ParseUInt(context, fields[2], value.cycles, 1024u, path) &&
            ParseFloat(context, fields[3], value.interval, path) &&
            ParseFloat(context, fields[4], value.probability, path);
    }

    bool ParseSubEmitterRule(DecodeContext& context, const std::string& text, ParticleSubEmitterRule& value, const char* path)
    {
        DepthGuard guard(context);
        List<std::string> fields;
        if (!ReadArray(context, text, fields, path)) return false;
        if (fields.size() != 7)
        {
            context.error = std::string("ParticleSettings sub emitter must have 7 fields at ") + path;
            return false;
        }

        uint32 event = 0;
        return ParseUInt(context, fields[0], value.targetSlot, 15u, path) &&
            ParseUInt(context, fields[1], event, static_cast<uint32>(ParticleSubEmitterEvent::Death), path) &&
            ParseUInt(context, fields[2], value.count, 65536u, path) &&
            ParseFloat(context, fields[3], value.probability, path) &&
            ParseBool(context, fields[4], value.inheritVelocity, path) &&
            ParseBool(context, fields[5], value.inheritColor, path) &&
            ParseBool(context, fields[6], value.inheritSize, path) &&
            (value.event = static_cast<ParticleSubEmitterEvent>(event), true);
    }
}

std::string ParticleSettingsCodec::Encode(const ParticleSettings& settings)
{
    List<std::string> subEmitters;
    subEmitters.reserve(settings.subEmitters.size());
    for (const ParticleSubEmitterRule& rule : settings.subEmitters) subEmitters.push_back(EncodeSubEmitterRule(rule));

    //最外层固定 9 项：schema 文本加八个模块
    return Reflection::FormatArrayValues({ SchemaTag, EncodeMain(settings.main), EncodeEmission(settings.emission),
        EncodeShape(settings.shape), EncodeMotion(settings.motion), EncodeCollision(settings.collision),
        EncodeTrails(settings.trails), EncodeRendering(settings.rendering),
        Reflection::FormatArrayValues(subEmitters) });
}

bool ParticleSettingsCodec::Decode(const std::string& text, ParticleSettings& settings, std::string& error)
{
    error.clear();
    if (text.size() > MaximumTextLength)
    {
        error = "ParticleSettings text is too long";
        return false;
    }

    DecodeContext context;
    List<std::string> modules;
    {
        DepthGuard guard(context);
        if (!ReadArray(context, text, modules, "ParticleSettings"))
        {
            error = context.error;
            return false;
        }
    }
    if (modules.size() != 9)
    {
        error = "ParticleSettings must have 9 top level entries";
        return false;
    }
    if (modules[0] != SchemaTag)
    {
        error = "ParticleSettings schema version is not supported";
        return false;
    }

    //全部解析在局部对象上完成，成功之后才写回输出
    ParticleSettings parsed;
    {
        DepthGuard guard(context);
        List<std::string> fields;

        if (!ReadArray(context, modules[1], fields, "main")) { error = context.error; return false; }
        if (fields.size() != 12) { error = "ParticleSettings main must have 12 fields"; return false; }
        uint32 simulationSpace = 0;
        if (!ParseUInt(context, fields[0], parsed.main.maxParticles, 65536u, "main.maxParticles") ||
            !ParseFloat(context, fields[1], parsed.main.duration, "main.duration") ||
            !ParseBool(context, fields[2], parsed.main.looping, "main.looping") ||
            !ParseBool(context, fields[3], parsed.main.playOnAwake, "main.playOnAwake") ||
            !ParseFloat(context, fields[4], parsed.main.startDelay, "main.startDelay") ||
            !ParseUInt(context, fields[5], simulationSpace, static_cast<uint32>(ParticleSimulationSpace::Local), "main.simulationSpace") ||
            !ParseUInt(context, fields[6], parsed.main.randomSeed, 0xFFFFFFFFu, "main.randomSeed") ||
            !ParseRange(context, fields[7], parsed.main.startLifetime, "main.startLifetime") ||
            !ParseRange(context, fields[8], parsed.main.startSpeed, "main.startSpeed") ||
            !ParseRange(context, fields[9], parsed.main.startSize, "main.startSize") ||
            !ParseRange(context, fields[10], parsed.main.startRotation, "main.startRotation") ||
            !ParseColor(context, fields[11], parsed.main.startColor, "main.startColor"))
        {
            error = context.error;
            return false;
        }
        parsed.main.simulationSpace = static_cast<ParticleSimulationSpace>(simulationSpace);

        if (!ReadArray(context, modules[2], fields, "emission")) { error = context.error; return false; }
        if (fields.size() != 3) { error = "ParticleSettings emission must have 3 fields"; return false; }
        if (!ParseBool(context, fields[0], parsed.emission.enabled, "emission.enabled") ||
            !ParseFloat(context, fields[1], parsed.emission.rateOverTime, "emission.rateOverTime"))
        {
            error = context.error;
            return false;
        }
        {
            DepthGuard burstGuard(context);
            List<std::string> bursts;
            if (!ReadArray(context, fields[2], bursts, "emission.bursts")) { error = context.error; return false; }
            if (bursts.size() > 64) { error = "ParticleSettings emission has too many bursts"; return false; }
            for (const std::string& burstText : bursts)
            {
                ParticleBurst burst;
                if (!ParseBurst(context, burstText, burst, "emission.bursts")) { error = context.error; return false; }
                parsed.emission.bursts.push_back(burst);
            }
        }

        if (!ReadArray(context, modules[3], fields, "shape")) { error = context.error; return false; }
        if (fields.size() != 5) { error = "ParticleSettings shape must have 5 fields"; return false; }
        uint32 shape = 0;
        if (!ParseUInt(context, fields[0], shape, static_cast<uint32>(ParticleShape::Box), "shape.shape") ||
            !ParseFloat(context, fields[1], parsed.shape.radius, "shape.radius") ||
            !ParseVector3(context, fields[2], parsed.shape.boxExtents, "shape.boxExtents") ||
            !ParseFloat(context, fields[3], parsed.shape.coneAngle, "shape.coneAngle") ||
            !ParseBool(context, fields[4], parsed.shape.surfaceOnly, "shape.surfaceOnly"))
        {
            error = context.error;
            return false;
        }
        parsed.shape.shape = static_cast<ParticleShape>(shape);

        if (!ReadArray(context, modules[4], fields, "motion")) { error = context.error; return false; }
        if (fields.size() != 6) { error = "ParticleSettings motion must have 6 fields"; return false; }
        if (!ParseFloat(context, fields[0], parsed.motion.gravityMultiplier, "motion.gravityMultiplier") ||
            !ParseVector3(context, fields[1], parsed.motion.acceleration, "motion.acceleration") ||
            !ParseFloat(context, fields[2], parsed.motion.drag, "motion.drag") ||
            !ParseCurve(context, fields[3], parsed.motion.sizeOverLifetime, "motion.sizeOverLifetime") ||
            !ParseCurve(context, fields[4], parsed.motion.angularVelocityOverLifetime, "motion.angularVelocityOverLifetime") ||
            !ParseGradient(context, fields[5], parsed.motion.colorOverLifetime, "motion.colorOverLifetime"))
        {
            error = context.error;
            return false;
        }

        if (!ReadArray(context, modules[5], fields, "collision")) { error = context.error; return false; }
        if (fields.size() != 7) { error = "ParticleSettings collision must have 7 fields"; return false; }
        uint32 response = 0;
        if (!ParseBool(context, fields[0], parsed.collision.enabled, "collision.enabled") ||
            !ParseUInt(context, fields[1], parsed.collision.layerMask, 0xFFFFFFFFu, "collision.layerMask") ||
            !ParseFloat(context, fields[2], parsed.collision.radiusScale, "collision.radiusScale") ||
            !ParseFloat(context, fields[3], parsed.collision.restitution, "collision.restitution") ||
            !ParseFloat(context, fields[4], parsed.collision.friction, "collision.friction") ||
            !ParseFloat(context, fields[5], parsed.collision.lifetimeLoss, "collision.lifetimeLoss") ||
            !ParseUInt(context, fields[6], response, static_cast<uint32>(ParticleCollisionResponse::Kill), "collision.response"))
        {
            error = context.error;
            return false;
        }
        parsed.collision.response = static_cast<ParticleCollisionResponse>(response);

        if (!ReadArray(context, modules[6], fields, "trails")) { error = context.error; return false; }
        if (fields.size() != 11) { error = "ParticleSettings trails must have 11 fields"; return false; }
        if (!ParseBool(context, fields[0], parsed.trails.enabled, "trails.enabled") ||
            !ParseFloat(context, fields[1], parsed.trails.lifetime, "trails.lifetime") ||
            !ParseFloat(context, fields[2], parsed.trails.minimumVertexDistance, "trails.minimumVertexDistance") ||
            !ParseFloat(context, fields[3], parsed.trails.maximumVertexInterval, "trails.maximumVertexInterval") ||
            !ParseUInt(context, fields[4], parsed.trails.maxPointsPerTrail, 64u, "trails.maxPointsPerTrail") ||
            !ParseUInt(context, fields[5], parsed.trails.maxTrails, 65536u, "trails.maxTrails") ||
            !ParseFloat(context, fields[6], parsed.trails.width, "trails.width") ||
            !ParseCurve(context, fields[7], parsed.trails.widthOverLength, "trails.widthOverLength") ||
            !ParseGradient(context, fields[8], parsed.trails.colorOverLength, "trails.colorOverLength") ||
            !ParseBool(context, fields[9], parsed.trails.dieWithParticle, "trails.dieWithParticle") ||
            !ParseFloat(context, fields[10], parsed.trails.textureTileLength, "trails.textureTileLength"))
        {
            error = context.error;
            return false;
        }

        if (!ReadArray(context, modules[7], fields, "rendering")) { error = context.error; return false; }
        if (fields.size() != 7) { error = "ParticleSettings rendering must have 7 fields"; return false; }
        uint32 path = 0;
        uint32 mode = 0;
        uint32 blendMode = 0;
        if (!ParseUInt(context, fields[0], path, static_cast<uint32>(ParticleRenderPath::Instanced), "rendering.path") ||
            !ParseUInt(context, fields[1], mode, static_cast<uint32>(ParticleRenderMode::Mesh), "rendering.mode") ||
            !ParseUInt(context, fields[2], blendMode, static_cast<uint32>(BlendMode::Additive), "rendering.blendMode") ||
            !ParseUInt(context, fields[3], parsed.rendering.tilesX, 256u, "rendering.tilesX") ||
            !ParseUInt(context, fields[4], parsed.rendering.tilesY, 256u, "rendering.tilesY") ||
            !ParseFloat(context, fields[5], parsed.rendering.animationCycles, "rendering.animationCycles") ||
            !ParseBool(context, fields[6], parsed.rendering.randomStartFrame, "rendering.randomStartFrame"))
        {
            error = context.error;
            return false;
        }
        parsed.rendering.path = static_cast<ParticleRenderPath>(path);
        parsed.rendering.mode = static_cast<ParticleRenderMode>(mode);
        parsed.rendering.blendMode = static_cast<BlendMode>(blendMode);

        {
            DepthGuard ruleGuard(context);
            List<std::string> rules;
            if (!ReadArray(context, modules[8], rules, "subEmitters")) { error = context.error; return false; }
            if (rules.size() > 16) { error = "ParticleSettings has too many sub emitters"; return false; }
            for (const std::string& ruleText : rules)
            {
                ParticleSubEmitterRule rule;
                if (!ParseSubEmitterRule(context, ruleText, rule, "subEmitters")) { error = context.error; return false; }
                parsed.subEmitters.push_back(rule);
            }
        }
    }

    //规范化后整体校验，任何一项不通过都不写回
    ParticleSettings::Normalize(parsed);
    if (!ParticleSettings::Validate(parsed, error)) return false;

    settings = std::move(parsed);
    return true;
}


#include "Log/Log.h"

#include <bit>

namespace
{
    //一次 xorshift32 步进
    uint32 StepRandomState(uint32 value)
    {
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        return value;
    }

    //把 32 位状态上的 GF(2) 线性变换按列存储：列 i 是单位向量 e_i 的像
    struct RandomBitTransform
    {
        uint32 columns[32] = {};
    };

    //作用一次线性变换
    uint32 ApplyTransform(const RandomBitTransform& transform, uint32 state)
    {
        uint32 result = 0;
        uint32 bits = state;
        while (bits != 0)
        {
            uint32 index = static_cast<uint32>(std::countr_zero(bits));
            result ^= transform.columns[index];
            bits &= bits - 1;
        }

        return result;
    }

    //先作用 inner 再作用 outer，合成同一个 xorshift 步进的更高次幂
    RandomBitTransform ComposeTransform(const RandomBitTransform& outer, const RandomBitTransform& inner)
    {
        RandomBitTransform result;
        for (uint32 index = 0; index < 32; ++index)
        {
            result.columns[index] = ApplyTransform(outer, inner.columns[index]);
        }

        return result;
    }

    //预计算 64 个平方幂：T、T^2、T^4 … 直到 2^63
    const List<RandomBitTransform>& GetRandomPowerTable()
    {
        static const List<RandomBitTransform> powers = []
        {
            List<RandomBitTransform> table;
            table.reserve(64);
            RandomBitTransform base;
            for (uint32 index = 0; index < 32; ++index) base.columns[index] = StepRandomState(1u << index);
            table.push_back(base);
            for (uint32 exponent = 1; exponent < 64; ++exponent)
            {
                table.push_back(ComposeTransform(table.back(), table.back()));
            }

            return table;
        }();

        return powers;
    }
}

float32 NextRandomSample(uint32& state)
{
    state = StepRandomState(state);
    //取高 24 位映射到 [0,1)
    return static_cast<float32>(state >> 8) / 16777216.0f;
}

void AdvanceRandomState(uint32& state, uint64 sampleCount)
{
    if (sampleCount == 0) return;

    //把「逐个消费」换成同一个线性变换的 sampleCount 次幂，复杂度 O(64×32)
    const List<RandomBitTransform>& powers = GetRandomPowerTable();
    for (uint32 bit = 0; bit < 64; ++bit)
    {
        if ((sampleCount & (1ull << bit)) == 0) continue;
        state = ApplyTransform(powers[bit], state);
    }
}
