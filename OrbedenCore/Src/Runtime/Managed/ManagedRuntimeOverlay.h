#pragma once

#include "Rendering/RenderSystem.h"
#include "Runtime/Managed/ScriptSystem.h"

#include <string>

// Runtime 托管覆盖层启动配置。
struct ManagedRuntimeOverlayConfig
{
public:
    std::string userAssemblyPath;
    std::string userTypeName;
    std::string userMethodName;
};

// Runtime 托管覆盖层，负责调用 C# OnGui。
class ManagedRuntimeOverlay : public IRenderOverlay
{
private:
    ScriptSystem* scriptSystem = nullptr;
    void* DrawGuiFunction = nullptr;
    bool initialized = false;

public:
    ManagedRuntimeOverlay() = default;
    ManagedRuntimeOverlay(const ManagedRuntimeOverlay&) = delete;
    ManagedRuntimeOverlay& operator=(const ManagedRuntimeOverlay&) = delete;

    // 使用外部脚本系统绑定可选的 OnGui。
    bool Initialize(ScriptSystem& runtime, const ManagedRuntimeOverlayConfig& config);

    // 关闭托管覆盖层。
    void Shutdown();

    // 绘制一帧托管 Runtime GUI。
    void DrawOverlay() override;

    // 判断托管覆盖层是否可用。
    bool IsInitialized() const;

    // 获取外部脚本系统。
    ScriptSystem& GetScriptSystem();
};
