#pragma once

#include "Application.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/Native/NativeCall.h"
#include "Runtime/Object/Script.h"

#include <functional>

//脚本域的运行阶段模式。
//编辑模式只构造包装与帧系统并执行结构与渲染，不执行游戏生命周期和交互路由。
enum class ScriptExecutionMode : uint32
{
    Editor = 0,
    Play = 1,
};

//托管脚本域的固定原生入口。
struct ScriptEntryPoints
{
public:
    //第二个参数是 ScriptExecutionMode：编辑模式与 Play 模式共用同一套入口。
    using InitializeFunction = void(ORBEDEN_NATIVE_CALL*)(void*, uint32);
    using ShutdownFunction = void(ORBEDEN_NATIVE_CALL*)();
    using LoadAssemblyFunction = uint8(ORBEDEN_NATIVE_CALL*)(const uint8*, int32);
    using UpdateFunction = void(ORBEDEN_NATIVE_CALL*)(float32);
    using EnsWorldActiveChangedFunction = void(ORBEDEN_NATIVE_CALL*)(EnsId, uint8);
    using EnsDestroyedFunction = void(ORBEDEN_NATIVE_CALL*)(EnsId);
    using DrawGuiFunction = void(ORBEDEN_NATIVE_CALL*)();

    InitializeFunction initialize = nullptr;
    ShutdownFunction shutdown = nullptr;
    LoadAssemblyFunction loadAssembly = nullptr;
    UpdateFunction update = nullptr;
    UpdateFunction fixedUpdate = nullptr;
    UpdateFunction lateUpdate = nullptr;
    EnsWorldActiveChangedFunction ensWorldActiveChanged = nullptr;
    EnsDestroyedFunction ensDestroyed = nullptr;
    DrawGuiFunction drawGui = nullptr;
    //尾部追加：渲染前的托管 UI 阶段；旧宿主不提供时为空，按无 UI 处理。
    UpdateFunction processInput = nullptr;
    UpdateFunction prepareRender = nullptr;

    //判断运行所需的托管脚本入口是否完整。
    bool IsValid() const;
};

//一个脚本语言域在各运行阶段的批量函数表。
struct ScriptDomainCallbacks
{
    using LifecycleFunction = void(*)(void*);
    using UpdateFunction = void(*)(void*, float32);
    using DrawGuiFunction = void(*)(void*);

    LifecycleFunction start = nullptr;
    UpdateFunction update = nullptr;
    UpdateFunction fixedUpdate = nullptr;
    UpdateFunction lateUpdate = nullptr;
    DrawGuiFunction drawGUI = nullptr;
    LifecycleFunction end = nullptr;
};

//ScriptSystem 使用的统一脚本域条目。
struct ScriptDomainEntry
{
    void* context = nullptr;
    ScriptDomainCallbacks callbacks;
};

//C++ 有参生命周期函数的预绑定调用项。
struct NativeScriptUpdateInvocation
{
    Script* instance = nullptr;
    ScriptUpdateCallback callback = nullptr;
};

//C++ 无参生命周期函数的预绑定调用项。
struct NativeScriptInvocation
{
    Script* instance = nullptr;
    ScriptCallback callback = nullptr;
};

//以同构阶段函数表统一执行 C++ 与 NativeAOT/CLR C# 游戏脚本。
class ScriptSystem final : public IEngineSystem, public IRenderOverlay, public IWorldLifecycleListener
{
private:
    ScriptEntryPoints entryPoints;
    List<ScriptDomainEntry> domains;
    List<int32> nativeScriptIds;
    List<NativeScriptUpdateInvocation> nativeUpdateInvocations;
    List<NativeScriptUpdateInvocation> nativeFixedUpdateInvocations;
    List<NativeScriptUpdateInvocation> nativeLateUpdateInvocations;
    List<NativeScriptInvocation> nativeDrawGuiInvocations;
    RenderSystem* renderSystem = nullptr;
    World* world = nullptr;
    ScriptRuntimeMode runtimeMode = ScriptRuntimeMode::AOT;
    ScriptExecutionMode executionMode = ScriptExecutionMode::Play;
    bool initialized = false;
    bool renderOverlayAttached = false;
    bool nativeListsDirty = false;
    bool domainDispatching = false;
    bool applyingDeferredMutations = false;
    bool shuttingDown = false;
    List<int32> deferredComponentRemovals;
    List<EnsId> deferredEnsDestructions;

    //扫描当前 World 并启动全部活动 C++ 脚本。
    void InitializeNativeScripts();

    //结束并清空全部 C++ 脚本调度状态。
    void ShutdownNativeScripts();

    //根据活动状态重建 C++ 阶段调用表。
    void RebuildNativeInvocations();

    //把已经卸载的脚本从全部调用表中置空。
    void TombstoneNativeInvocations(Script* script);

    //判断脚本当前是否允许参与生命周期阶段。
    bool IsNativeScriptRunnable(Script* script) const;

    //按 Object ID 解析仍然存活的 C++ 脚本。
    Script* ResolveNativeScript(int32 objectId) const;

    //执行 C++ 脚本阶段。
    void DispatchNativeUpdate(float32 deltaTime);
    void DispatchNativeFixedUpdate(float32 fixedDeltaTime);
    void DispatchNativeLateUpdate(float32 deltaTime);
    void DispatchNativeDrawGUI();

    //执行托管脚本阶段。
    void DispatchManagedUpdate(float32 deltaTime);
    void DispatchManagedFixedUpdate(float32 fixedDeltaTime);
    void DispatchManagedLateUpdate(float32 deltaTime);
    void DispatchManagedDrawGUI();

    //统一脚本域函数表入口。
    static void NativeUpdateDomain(void* context, float32 deltaTime);
    static void NativeStartDomain(void* context);
    static void NativeEndDomain(void* context);
    static void NativeFixedUpdateDomain(void* context, float32 fixedDeltaTime);
    static void NativeLateUpdateDomain(void* context, float32 deltaTime);
    static void NativeDrawGuiDomain(void* context);
    static void ManagedUpdateDomain(void* context, float32 deltaTime);
    static void ManagedStartDomain(void* context);
    static void ManagedEndDomain(void* context);
    static void ManagedFixedUpdateDomain(void* context, float32 fixedDeltaTime);
    static void ManagedLateUpdateDomain(void* context, float32 deltaTime);
    static void ManagedDrawGuiDomain(void* context);

    //在进入下一个脚本域阶段前应用回调期间请求的删除。
    void ApplyDeferredMutations();

public:
    //获取当前 Application 拥有的 ScriptSystem。
    static ScriptSystem* Current();

    //判断脚本域是否正在执行回调
    bool IsDispatching() const { return domainDispatching || shuttingDown; }

    //回调期间把组件删除延迟到下一个脚本域阶段。
    bool DeferComponentRemoval(Component* component);

    //回调期间把 Ens 销毁延迟到下一个脚本域阶段。
    bool DeferEnsDestruction(EnsId ens);

    //获取依赖并准备脚本域入口，不启动脚本实例。
    bool OnInitialize(Application& app) override;

    //关闭脚本域。
    void OnShutdown() override;

    //设置 CLR 游戏程序集导出的完整入口。
    bool SetClrEntryPoints(const ScriptEntryPoints& value);

    //设置 NativeAOT 游戏模块导出的完整入口；由 Player 等宿主在 Initialize 前注入。
    //Core 不引用宿主符号，因此 AOT 入口必须运行时提供。
    bool SetAotEntryPoints(const ScriptEntryPoints& value);

    //在 World 加载完成后启动 C++ 与 C# 脚本域。
    //编辑模式只构造包装与帧系统，不执行 OnStart 等游戏生命周期。
    bool Initialize(ScriptExecutionMode mode = ScriptExecutionMode::Play);

    //获取当前运行阶段模式。
    ScriptExecutionMode GetExecutionMode() const { return executionMode; }

    //在 FixedUpdate 之前处理托管输入阶段；不受模拟暂停门控，编辑模式不执行交互路由。
    void ProcessManagedInput(float32 deltaTime);

    //在全部 LateUpdate 之后、渲染消费之前准备托管渲染；不受暂停门控。
    void PrepareManagedRender(float32 deltaTime);

    //在延迟删除保护区内执行一段外部回调。作用域结束时恢复原来的派发标志，
    //因此嵌在脚本阶段里的 UI 事件派发不会提前关掉外层的延迟删除保护。
    void DispatchExternalCallbacks(const std::function<void()>& callback);

    //按 C++、C# 顺序关闭当前脚本域。
    void Shutdown();

    //每帧更新游戏脚本。
    void Update(World& world, float32 deltaTime) override;

    //固定步长更新游戏脚本。
    void FixedUpdate(World& world, float32 fixedDeltaTime) override;

    //普通 Update 后更新游戏脚本。
    void LateUpdate(World& world, float32 deltaTime) override;

    //绘制游戏脚本 GUI。
    void DrawOverlay() override;

    //记录运行期间新挂载的 C++ 脚本。
    void AttachNativeScript(Script* script);

    //在组件销毁前移除 C++ 脚本并调用 End。
    void DetachNativeScript(Script* script);

    //响应 enabled 或 worldActive 变化。
    void RefreshNativeScript(Script* script);

    //把 Ens 活动状态转发给 C# 脚本域。
    void OnEnsWorldActiveChanged(EnsId ens, bool worldActive) override;

    //把 Ens 销毁事件转发给 C# 脚本域。
    void OnEnsDestroyed(EnsId ens) override;

    //判断脚本域是否已经启动。
    bool IsInitialized() const;
};
