#include "Runtime/Object/ParticleSystem.h"

#include "Log/Log.h"
#include "Runtime/Object/Ens.h"

#include "Runtime/Object/Shader.h"
#include "Runtime/Particles/ParticleSettings.h"
#include "Runtime/Particles/ParticleSimulationSystem.h"
#include "Runtime/World.h"

#include <cstring>

OBJECT_TYPE_IMPLEMENT(ParticleSystem, Component)

//读取配置副本
ParticleSettings ParticleSystem::GetSettings() const
{
    return settings;
}

//替换配置
bool ParticleSystem::SetSettings(const ParticleSettings& value)
{
    ParticleSettings candidate = value;
    ParticleSettings::Normalize(candidate);

    std::string error;
    if (!ParticleSettings::Validate(candidate, error))
    {
        lastError = error;
        return false;
    }

    settings = candidate;
    lastError.clear();
    //模式与路径都在配置里，几何变体的要求跟着变，所以这里重算一次资源诊断
    RefreshResourceDiagnostic();
    //配置变化让模拟系统在下一步重建该发射器，同时把所属 World 标脏
    ++configurationRevision;
    if (World* owner = GetWorld()) owner->SetDirty();
    return true;
}

//解析配置文本
ParticleSettingsParseResult ParticleSystem::ParseSettings(const std::string& text)
{
    ParticleSettingsParseResult result;
    //空文本按默认配置处理，重新保存时仍会写出完整 v1
    if (text.empty())
    {
        result.success = true;
        return result;
    }

    result.success = ParticleSettingsCodec::Decode(text, result.settings, result.error);
    return result;
}

//把配置格式化成规范文本
std::string ParticleSystem::FormatSettings(const ParticleSettings& value)
{
    std::string error;
    if (!ParticleSettings::Validate(value, error)) return std::string();
    return ParticleSettingsCodec::Encode(value);
}

//读取启用状态
bool ParticleSystem::GetEnabled() const
{
    return enabled;
}

//设置启用状态
void ParticleSystem::SetEnabled(bool value)
{
    if (enabled == value) return;

    enabled = value;
    ++configurationRevision;
    if (ParticleSimulationSystem* system = ParticleSimulationSystem::Current())
    {
        if (enabled) system->Register(*this);
        else system->Unregister(*this);
    }

}

//读取配置版本号
uint64 ParticleSystem::GetConfigurationRevision() const
{
    return configurationRevision;
}

//资源、层数或阴影开关变化后的通知
void ParticleSystem::OnConfigurationChanged()
{
    ++configurationRevision;
    RefreshResourceDiagnostic();
}

//检查当前配置引用的材质能否支持粒子几何
void ParticleSystem::RefreshResourceDiagnostic()
{
    //需要的变体由模式与路径决定：路径 Instanced 要实例变体，两种契约都有；
    //路径 DynamicBatch 要展开变体，声明 expandedGeometry off 的 Shader 没有；
    //拖尾另有 TrailInstanced 要求，只有 Particle 契约编译。
    bool needsExpandedVariants = settings.rendering.path == ParticleRenderPath::DynamicBatch;

    std::string diagnostic;
    auto checkMaterial = [&](const Ref<Material>& reference, bool trail, const char* usage)
    {
        if (!diagnostic.empty()) return;

        //空槽位不画也不报：新加的组件本来就保持空资源
        Material* material = reference.Get();
        if (!material) return;

        Shader* shader = material->shader.Get();
        if (!shader)
        {
            diagnostic = "Material '" + material->name + "' (" + std::string(usage) + ") has no shader.";
            return;
        }

        if (shader->passes.empty())
        {
            diagnostic = "Shader '" + shader->name + "' has no passes.";
            return;
        }

        //粒子的两种路径都不接受多 Pass，管线只按第一个 Pass 的几何契约取变体
        if (shader->passes.size() > 1)
        {
            diagnostic = "Material '" + material->name + "' (" + std::string(usage) + ") uses a multi-pass shader; particle rendering supports single-pass shaders only.";
            return;
        }

        const ShaderPass& pass = shader->passes[0];
        //拖尾的实例路径要 TrailInstanced，展开路径要 Expanded；普通粒子只在走展开路径时要 Expanded
        bool trailUnsupported = trail && pass.geometryContract != ShaderGeometryContract::Particle;
        bool expandedUnsupported = !pass.supportsExpandedGeometry && (trail || needsExpandedVariants);
        if (!trailUnsupported && !expandedUnsupported) return;

        diagnostic = "Material '" + material->name + "' (" + std::string(usage) + ") uses shader '" + shader->name +
            "' which " + (pass.geometryContract != ShaderGeometryContract::Particle && trail
                ? std::string("does not declare the Particle contract required by trails")
                : std::string("declares 'expandedGeometry off', so the dynamic batch path has no geometry")) +
            ". Nothing is drawn for this emitter.";
    };

    if (settings.rendering.mode == ParticleRenderMode::Mesh)
    {
        for (usize index = 0; index < materials.size(); ++index) checkMaterial(materials[index], false, "mesh");
    }
    else
    {
        checkMaterial(materials.empty() ? Ref<Material>() : materials[0], false, "billboard");
    }

    if (settings.trails.enabled) checkMaterial(trailMaterial, true, "trail");

    //资源类诊断由本函数独占，配置与引用图诊断由各自的路径写入
    if (diagnostic.empty())
    {
        if (lastError.rfind("Material '", 0) == 0 || lastError.rfind("Shader '", 0) == 0) lastError.clear();
        return;
    }

    lastError = std::move(diagnostic);
}

//读取最近一次诊断
std::string ParticleSystem::GetLastError() const
{
    return lastError;
}

//播放运行时模拟
void ParticleSystem::Play(bool restart)
{
    if (ParticleSimulationSystem* system = ParticleSimulationSystem::Current())
    {
        system->ControlRuntime(*this, ParticleControl::Play, restart);
    }
}

//暂停运行时模拟
void ParticleSystem::Pause()
{
    if (ParticleSimulationSystem* system = ParticleSimulationSystem::Current())
    {
        system->ControlRuntime(*this, ParticleControl::Pause, false);
    }
}

//停止运行时模拟
void ParticleSystem::Stop(bool clear)
{
    if (ParticleSimulationSystem* system = ParticleSimulationSystem::Current())
    {
        system->ControlRuntime(*this, ParticleControl::Stop, clear);
    }
}

//清空粒子、拖尾与未派发事件
void ParticleSystem::Clear()
{
    if (ParticleSimulationSystem* system = ParticleSimulationSystem::Current())
    {
        system->ControlRuntime(*this, ParticleControl::Clear, false);
    }
}

//手工出生粒子
uint32 ParticleSystem::Emit(uint32 count)
{
    ParticleSimulationSystem* system = ParticleSimulationSystem::Current();
    if (!system) return 0;
    return system->EmitRuntime(*this, count);
}

//读取运行时播放状态快照
ParticlePlaybackInfo ParticleSystem::GetPlaybackInfo() const
{
    ParticleSimulationSystem* system = ParticleSimulationSystem::Current();
    if (!system) return ParticlePlaybackInfo();
    return system->GetPlaybackInfo(GetObjectId(), false);
}

//挂载时注册到粒子模拟系统
void ParticleSystem::OnAttach()
{
    Component::OnAttach();
    if (ParticleSimulationSystem* system = ParticleSimulationSystem::Current())
    {
        system->Register(*this);
    }
}

//卸载时注销并清除状态
void ParticleSystem::OnDetach()
{
    if (ParticleSimulationSystem* system = ParticleSimulationSystem::Current())
    {
        system->Unregister(*this);
    }

    Component::OnDetach();
}

//所属 Ens 的 worldActive 变化时同步状态
void ParticleSystem::OnWorldActiveChanged(bool worldActive)
{
    Component::OnWorldActiveChanged(worldActive);
    if (ParticleSimulationSystem* system = ParticleSimulationSystem::Current())
    {
        if (worldActive && enabled) system->Register(*this);
        else system->Unregister(*this);
    }

}



#include "Runtime/Reflection.h"

namespace
{
    //手工注册的字段名；生成表里同名的占位项在注册时要让位
    bool IsHandWrittenField(const char* name)
    {
        return name && (std::strcmp(name, "enabled") == 0 || std::strcmp(name, "settings") == 0);
    }

    //读取启用状态
    std::string ReflectParticleGetEnabled(Object* object)
    {
        return Reflection::ToXmlValue(static_cast<ParticleSystem*>(object)->GetEnabled());
    }

    //写入启用状态
    bool ReflectParticleSetEnabled(Object* object, const std::string& value)
    {
        bool enabled = false;
        if (!Reflection::SetFromXmlValue(enabled, value)) return false;
        static_cast<ParticleSystem*>(object)->SetEnabled(enabled);
        return true;
    }

    //读取配置文本
    std::string ReflectParticleGetSettings(Object* object)
    {
        return ParticleSystem::FormatSettings(static_cast<ParticleSystem*>(object)->GetSettings());
    }

    //写入配置文本，解析或校验失败时保留旧值
    bool ReflectParticleSetSettings(Object* object, const std::string& value)
    {
        ParticleSettingsParseResult parsed = ParticleSystem::ParseSettings(value);
        if (!parsed.success) return false;
        return static_cast<ParticleSystem*>(object)->SetSettings(parsed.settings);
    }

    //类型化读取：启用状态
    Reflection::Value ReflectParticleGetEnabledValue(Object* object)
    {
        return Reflection::Value(static_cast<ParticleSystem*>(object)->GetEnabled());
    }

    bool ReflectParticleSetEnabledValue(Object* object, const Reflection::Value& value)
    {
        bool enabled = false;
        if (!value.TryGet(enabled)) return false;
        static_cast<ParticleSystem*>(object)->SetEnabled(enabled);
        return true;
    }

    //类型化读取：配置文本，与 XML 读取共用同一条校验加编码路径
    Reflection::Value ReflectParticleGetSettingsValue(Object* object)
    {
        return Reflection::Value(ReflectParticleGetSettings(object));
    }

    bool ReflectParticleSetSettingsValue(Object* object, const Reflection::Value& value)
    {
        std::string text;
        if (!value.TryGet(text)) return false;
        return ReflectParticleSetSettings(object, text);
    }
}

//注册粒子组件的持久化字段
void ParticleSystem::RegisterReflection()
    {
        static bool registered = false;
        if (registered) return;
        registered = true;

        //取生成表：资源引用与目标槽位由 MetaGen 生成，这里只做追加，不能丢掉它们
        List<const Reflection::FieldInfo*> generated;
        Reflection::CollectFields(ParticleSystem::StaticType(), generated);

        List<Reflection::FieldInfo> fields;
        fields.reserve(generated.size() + 2);
        for (const Reflection::FieldInfo* field : generated)
        {
            if (!field) continue;
            //生成表给私有字段留的是无访问器的占位项，必须剔除：表里留两个同名项会让按名查找
            //命中前面那个占位项，存档因此写得出、读不回来
            if (IsHandWrittenField(field->name)) continue;
            fields.push_back(*field);
        }

        //configurationRevision 不在此列，它不进持久化也不对外暴露。
        //表里只能有配置：粒子、拖尾、随机流与统计都在 ParticleSimulationSystem 里，
        //组件上任何一处都不要挂运行时状态，否则会被存档写进场景
        fields.push_back(Reflection::FieldInfo("enabled", "bool", Reflection::FieldKind::Bool, true,
            ReflectParticleGetEnabled, ReflectParticleSetEnabled, nullptr, ReflectParticleGetEnabledValue, ReflectParticleSetEnabledValue));
        fields.push_back(Reflection::FieldInfo("settings", "string", Reflection::FieldKind::String, true,
            ReflectParticleGetSettings, ReflectParticleSetSettings, nullptr, ReflectParticleGetSettingsValue, ReflectParticleSetSettingsValue));

        //整张表一次替换，避免生成字段与手工字段分两次注册互相覆盖
        Reflection::RegisterTypeFields(ParticleSystem::StaticType(), fields);
    }
