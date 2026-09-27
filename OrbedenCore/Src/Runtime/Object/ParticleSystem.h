#pragma once

#include "Runtime/Object/Component.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/Particles/ParticleSettings.h"

#include <array>
#include <string>

//CPU 粒子发射器组件。
//组件只保存持久化配置与资源引用，粒子、拖尾与随机流全部由 ParticleSimulationSystem 持有。
class ParticleSystem : public Component
{
    OBJECT_TYPE_DECLARE(ParticleSystem)
    ORBEDEN_COMPONENT_UNIQUE

private:
    friend class ParticleSimulationSystem;
    friend class ParticleSimulationContext;

    bool enabled = true;
    //只通过 GetSettings / SetSettings 修改，字段本身不参与持久化
    ParticleSettings settings;
    //配置版本号，模拟系统据此判断是否需要重置发射器状态
    uint64 configurationRevision = 1;
    //最近的配置、资源或引用图诊断
    std::string lastError;

public:
    /// <summary>注册组件的持久化字段。</summary>
    ORBEDEN_BIND_IGNORE
    static void RegisterReflection();

    //Mesh 模式逐子网格取材质；Billboard 只读第一个槽位
    ORBEDEN_BIND_CHANGED(OnConfigurationChanged)
    Ref<Mesh> mesh;
    ORBEDEN_BIND_CHANGED(OnConfigurationChanged)
    List<Ref<Material>> materials;
    ORBEDEN_BIND_CHANGED(OnConfigurationChanged)
    Ref<Material> trailMaterial;
    //固定十六槽的子发射器目标，槽号稳定，规则通过 targetSlot 定位
    ORBEDEN_BIND_CHANGED(OnConfigurationChanged)
    std::array<EnsId, 16> subEmitterTargets{};
    ORBEDEN_BIND_CHANGED(OnConfigurationChanged)
    uint32 drawLayer = 1u;
    ORBEDEN_BIND_CHANGED(OnConfigurationChanged)
    bool castShadows = false;
    ORBEDEN_BIND_CHANGED(OnConfigurationChanged)
    bool receiveShadows = false;

    /// <summary>读取配置副本，不暴露内部引用。</summary>
    ParticleSettings GetSettings() const;

    /// <summary>替换配置；规范化与校验失败时保留旧值并记录错误。</summary>
    bool SetSettings(const ParticleSettings& value);

    /// <summary>解析配置文本，不访问 World。</summary>
    static ParticleSettingsParseResult ParseSettings(const std::string& text);

    /// <summary>把合法配置格式化成规范文本；非法配置返回空串。</summary>
    static std::string FormatSettings(const ParticleSettings& value);

    /// <summary>读取启用状态。</summary>
    bool GetEnabled() const;

    /// <summary>设置启用状态；关闭会立即清空两套模拟状态。</summary>
    void SetEnabled(bool value);

    /// <summary>读取配置版本号。</summary>
    uint64 GetConfigurationRevision() const;

    /// <summary>资源、层数或阴影开关变化后的通知。</summary>
    void OnConfigurationChanged();

    /// <summary>读取最近一次诊断文本。</summary>
    std::string GetLastError() const;

    /// <summary>播放运行时模拟；restart 为真时清空粒子并重置时间与随机流。</summary>
    void Play(bool restart = true);

    /// <summary>暂停运行时模拟，保留粒子与时间。</summary>
    void Pause();

    /// <summary>停止运行时模拟；clear 为真时立即清空粒子。</summary>
    void Stop(bool clear = true);

    /// <summary>清空粒子、拖尾与未派发事件，不改变播放状态与时钟。</summary>
    void Clear();

    /// <summary>手工出生粒子并返回实际接收数量。</summary>
    uint32 Emit(uint32 count);

    /// <summary>读取运行时播放状态快照。</summary>
    ParticlePlaybackInfo GetPlaybackInfo() const;

    //挂载时注册到粒子模拟系统
    void OnAttach() override;

    //卸载时注销并清除两套状态
    void OnDetach() override;

    //所属 Ens 的 worldActive 变化时同步模拟状态
    void OnWorldActiveChanged(bool worldActive) override;
};
