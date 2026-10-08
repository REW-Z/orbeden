#include "Scripting/ScriptSystem.h"

#include "Log/Log.h"
#include "Profiler/Profiler.h"
#include "Runtime/Native/OrbedenNativeApi.h"
#include "Scripting/ScriptInterop.h"

#include <algorithm>
#include <exception>

namespace
{
    ScriptSystem* currentScriptSystem = nullptr;

    void LogNativeScriptException(Script* script, const char* phase, const char* message)
    {
        std::string typeName = script && script->GetType() ? script->GetType()->GetName() : "<unknown>";
        Log::Error(("C++ script " + typeName + " failed in " + phase + ": " + (message ? message : "unknown exception")).c_str());
    }

    void InvokeNativeScript(Script* script, ScriptCallback callback, const char* phase)
    {
        if (!script || !callback) return;

        try
        {
            callback(script);
        }
        catch (const std::exception& exception)
        {
            LogNativeScriptException(script, phase, exception.what());
        }
        catch (...)
        {
            LogNativeScriptException(script, phase, "non-standard exception");
        }
    }

    void InvokeNativeScript(Script* script, ScriptUpdateCallback callback, float32 deltaTime, const char* phase)
    {
        if (!script || !callback) return;

        try
        {
            callback(script, deltaTime);
        }
        catch (const std::exception& exception)
        {
            LogNativeScriptException(script, phase, exception.what());
        }
        catch (...)
        {
            LogNativeScriptException(script, phase, "non-standard exception");
        }
    }
}

bool ScriptEntryPoints::IsValid() const
{
    return initialize
        && shutdown
        && update
        && fixedUpdate
        && lateUpdate
        && ensWorldActiveChanged
        && ensDestroyed
        && drawGui;
}

bool ScriptSystem::SetAotEntryPoints(const ScriptEntryPoints& value)
{
    if (runtimeMode != ScriptRuntimeMode::AOT || initialized || !value.IsValid()) return false;

    entryPoints = value;
    return true;
}

ScriptSystem* ScriptSystem::Current()
{
    return currentScriptSystem;
}

bool ScriptSystem::SetClrEntryPoints(const ScriptEntryPoints& value)
{
    if (runtimeMode != ScriptRuntimeMode::CLR || initialized || !value.IsValid()) return false;

    entryPoints = value;
    return true;
}

bool ScriptSystem::OnInitialize(Application& app)
{
    if (currentScriptSystem && currentScriptSystem != this)
    {
        Log::Error("Only one ScriptSystem can be active.");
        return false;
    }

    renderSystem = app.GetSystem<RenderSystem>();
    if (!renderSystem) return false;

    world = &app.GetWorld();
    world->AddLifecycleListener(this);
    runtimeMode = app.GetScriptRuntimeMode();
    Script::RegisterReflection();
    ScriptInterop::Initialize(world);
    currentScriptSystem = this;

    //AOT 入口由宿主（Player）在 Initialize 前通过 SetAotEntryPoints 注入；
    //此处不校验，Initialize 统一检查入口完整性并给出明确错误。
    return true;
}

void ScriptSystem::OnShutdown()
{
    Shutdown();
    if (world) world->RemoveLifecycleListener(this);
    if (currentScriptSystem == this) currentScriptSystem = nullptr;
    ScriptInterop::Shutdown();
    renderSystem = nullptr;
    world = nullptr;
}

bool ScriptSystem::Initialize(ScriptExecutionMode mode)
{
    if (initialized) return true;
    if (!world || !renderSystem || !entryPoints.IsValid())
    {
        //写清缺的是哪一块：世界/渲染系统为空说明系统已被关闭，入口点缺失说明宿主没绑定成功。
        std::string missing = !world ? "world" : !renderSystem ? "render system" : "script entry points";
        Log::Error(("ScriptSystem initialize failed: " + missing
            + " is missing; the host must supply entry points (CLR or AOT) before Initialize.").c_str());
        return false;
    }

    executionMode = mode;
    ScriptInterop::Initialize(world);

    domains.clear();
    //编辑模式不注册原生域：C++ 脚本的 OnStart/OnUpdate 属于游戏生命周期。
    if (executionMode == ScriptExecutionMode::Play)
    {
        domains.push_back({
            this,
            {
                &ScriptSystem::NativeStartDomain,
                &ScriptSystem::NativeUpdateDomain,
                &ScriptSystem::NativeFixedUpdateDomain,
                &ScriptSystem::NativeLateUpdateDomain,
                &ScriptSystem::NativeDrawGuiDomain,
                &ScriptSystem::NativeEndDomain,
            },
        });
    }
    domains.push_back({
        this,
        {
            &ScriptSystem::ManagedStartDomain,
            &ScriptSystem::ManagedUpdateDomain,
            &ScriptSystem::ManagedFixedUpdateDomain,
            &ScriptSystem::ManagedLateUpdateDomain,
            &ScriptSystem::ManagedDrawGuiDomain,
            &ScriptSystem::ManagedEndDomain,
        },
    });

    initialized = true;
    for (const ScriptDomainEntry& domain : domains)
    {
        ApplyDeferredMutations();
        domainDispatching = true;
        if (domain.callbacks.start) domain.callbacks.start(domain.context);
        domainDispatching = false;
    }

    renderSystem->SetRenderOverlay(this);
    renderOverlayAttached = true;
    Log::Info(runtimeMode == ScriptRuntimeMode::AOT
        ? "C++ and AOT C# script domains initialized."
        : "C++ and CLR C# script domains initialized.");
    return true;
}

void ScriptSystem::Shutdown()
{
    shuttingDown = true;
    if (initialized)
    {
        ApplyDeferredMutations();
        for (const ScriptDomainEntry& domain : domains)
        {
            ApplyDeferredMutations();
            domainDispatching = true;
            if (domain.callbacks.end) domain.callbacks.end(domain.context);
            domainDispatching = false;
        }
        ApplyDeferredMutations();
    }

    if (renderOverlayAttached && renderSystem)
    {
        renderSystem->SetRenderOverlay(nullptr);
    }

    domains.clear();
    ScriptInterop::RegisterManagedApi(nullptr);
    //入口点是宿主对托管运行时（CLR 宿主或 AOT 模块）的绑定，只在宿主换运行时或换程序集时才重新提供。
    //世界被整体替换时 Application 会先 Shutdown 再 Initialize 重建脚本域，这里清空会让重建直接失败，
    //所以入口点在 Shutdown 后原样保留，由下一次 SetClrEntryPoints/SetAotEntryPoints 覆盖。
    initialized = false;
    renderOverlayAttached = false;
    domainDispatching = false;
    applyingDeferredMutations = false;
    deferredComponentRemovals.clear();
    deferredEnsDestructions.clear();
    shuttingDown = false;
}

bool ScriptSystem::DeferComponentRemoval(Component* component)
{
    if (!initialized || !domainDispatching || applyingDeferredMutations || !component) return false;
    int32 objectId = component->GetObjectId();
    if (std::find(deferredComponentRemovals.begin(), deferredComponentRemovals.end(), objectId) == deferredComponentRemovals.end())
        deferredComponentRemovals.push_back(objectId);
    return true;
}

bool ScriptSystem::DeferEnsDestruction(EnsId ens)
{
    if (!initialized || !domainDispatching || applyingDeferredMutations || ens.IsNull()) return false;
    if (std::find(deferredEnsDestructions.begin(), deferredEnsDestructions.end(), ens) == deferredEnsDestructions.end())
        deferredEnsDestructions.push_back(ens);
    return true;
}

void ScriptSystem::ApplyDeferredMutations()
{
    if (applyingDeferredMutations || (!world)) return;
    if (deferredEnsDestructions.empty() && deferredComponentRemovals.empty()) return;

    List<EnsId> ensDestructions;
    List<int32> componentRemovals;
    ensDestructions.swap(deferredEnsDestructions);
    componentRemovals.swap(deferredComponentRemovals);
    applyingDeferredMutations = true;
    for (EnsId ens : ensDestructions) world->DestroyEns(ens);
    for (int32 objectId : componentRemovals)
    {
        Object* object = Object::FindObjectById(objectId);
        Component* component = object ? object->Cast<Component>() : nullptr;
        if (component && component->GetWorld() == world) world->RemoveComponent(component);
    }
    applyingDeferredMutations = false;
}

void ScriptSystem::InitializeNativeScripts()
{
    nativeScriptIds.clear();
    nativeListsDirty = true;

    world->ForEachEns([this](Ens& ens)
        {
            for (Component* component : ens.GetComponents())
            {
                Script* script = component ? component->Cast<Script>() : nullptr;
                if (script && script->GetDomain() == ScriptDomain::Native) AttachNativeScript(script);
            }
        });

    RebuildNativeInvocations();
}

void ScriptSystem::ShutdownNativeScripts()
{
    List<int32> scriptIds = nativeScriptIds;
    for (int32 objectId : scriptIds)
    {
        DetachNativeScript(ResolveNativeScript(objectId));
    }

    nativeScriptIds.clear();
    nativeUpdateInvocations.clear();
    nativeFixedUpdateInvocations.clear();
    nativeLateUpdateInvocations.clear();
    nativeDrawGuiInvocations.clear();
    nativeListsDirty = false;
}

void ScriptSystem::RebuildNativeInvocations()
{
    if (nativeListsDirty)
    {
        nativeListsDirty = false;
        nativeUpdateInvocations.clear();
        nativeFixedUpdateInvocations.clear();
        nativeLateUpdateInvocations.clear();
        nativeDrawGuiInvocations.clear();

        List<int32> scriptIds;
        world->ForEachEns([&](Ens& ens)
        {
            for (Component* component : ens.GetComponents())
            {
                Script* script = component ? component->Cast<Script>() : nullptr;
                if (script && script->GetDomain() == ScriptDomain::Native && script->runtimeRegistered)
                    scriptIds.push_back(script->GetObjectId());
            }
        });
        for (int32 objectId : scriptIds)
        {
            Script* script = ResolveNativeScript(objectId);
            if (!IsNativeScriptRunnable(script)) continue;

            ScriptCallbackTable callbacks = ResolveScriptCallbacks(script->GetType());
            if (!script->scriptStarted)
            {
                script->scriptStarted = true;
                InvokeNativeScript(script, callbacks.start, "OnStart");

                script = ResolveNativeScript(objectId);
                if (!IsNativeScriptRunnable(script)) continue;
                callbacks = ResolveScriptCallbacks(script->GetType());
            }

            if (callbacks.update) nativeUpdateInvocations.push_back({ script, callbacks.update });
            if (callbacks.fixedUpdate) nativeFixedUpdateInvocations.push_back({ script, callbacks.fixedUpdate });
            if (callbacks.lateUpdate) nativeLateUpdateInvocations.push_back({ script, callbacks.lateUpdate });
            if (callbacks.drawGUI) nativeDrawGuiInvocations.push_back({ script, callbacks.drawGUI });
        }
    }
}

void ScriptSystem::TombstoneNativeInvocations(Script* script)
{
    for (NativeScriptUpdateInvocation& invocation : nativeUpdateInvocations)
    {
        if (invocation.instance == script) invocation.instance = nullptr;
    }
    for (NativeScriptUpdateInvocation& invocation : nativeFixedUpdateInvocations)
    {
        if (invocation.instance == script) invocation.instance = nullptr;
    }
    for (NativeScriptUpdateInvocation& invocation : nativeLateUpdateInvocations)
    {
        if (invocation.instance == script) invocation.instance = nullptr;
    }
    for (NativeScriptInvocation& invocation : nativeDrawGuiInvocations)
    {
        if (invocation.instance == script) invocation.instance = nullptr;
    }
}

bool ScriptSystem::IsNativeScriptRunnable(Script* script) const
{
    if (!script || script->GetDomain() != ScriptDomain::Native || !script->runtimeRegistered || !script->enabled) return false;
    Ens* owner = script->GetEns();
    return owner && owner->GetWorldActive();
}

Script* ScriptSystem::ResolveNativeScript(int32 objectId) const
{
    Object* object = Object::FindObjectById(objectId);
    return object ? object->Cast<Script>() : nullptr;
}

void ScriptSystem::AttachNativeScript(Script* script)
{
    if (!initialized || shuttingDown || !script || script->GetDomain() != ScriptDomain::Native || script->runtimeRegistered) return;

    script->runtimeRegistered = true;
    script->scriptStarted = false;
    nativeScriptIds.push_back(script->GetObjectId());
    nativeListsDirty = true;
}

void ScriptSystem::DetachNativeScript(Script* script)
{
    if (!script || !script->runtimeRegistered) return;

    script->runtimeRegistered = false;
    nativeScriptIds.erase(std::remove(nativeScriptIds.begin(), nativeScriptIds.end(), script->GetObjectId()), nativeScriptIds.end());
    TombstoneNativeInvocations(script);

    bool callEnd = script->scriptStarted;
    script->scriptStarted = false;
    if (callEnd)
    {
        ScriptCallbackTable callbacks = ResolveScriptCallbacks(script->GetType());
        InvokeNativeScript(script, callbacks.end, "OnEnd");
    }

    nativeListsDirty = true;
}

void ScriptSystem::RefreshNativeScript(Script* script)
{
    if (!initialized || !script || !script->runtimeRegistered) return;
    if (!IsNativeScriptRunnable(script)) TombstoneNativeInvocations(script);
    nativeListsDirty = true;
}

void ScriptSystem::DispatchNativeUpdate(float32 deltaTime)
{
    RebuildNativeInvocations();
    for (const NativeScriptUpdateInvocation& invocation : nativeUpdateInvocations)
    {
        if (invocation.instance) InvokeNativeScript(invocation.instance, invocation.callback, deltaTime, "OnUpdate");
    }
}

void ScriptSystem::DispatchNativeFixedUpdate(float32 fixedDeltaTime)
{
    RebuildNativeInvocations();
    for (const NativeScriptUpdateInvocation& invocation : nativeFixedUpdateInvocations)
    {
        if (invocation.instance) InvokeNativeScript(invocation.instance, invocation.callback, fixedDeltaTime, "OnFixedUpdate");
    }
}

void ScriptSystem::DispatchNativeLateUpdate(float32 deltaTime)
{
    RebuildNativeInvocations();
    for (const NativeScriptUpdateInvocation& invocation : nativeLateUpdateInvocations)
    {
        if (invocation.instance) InvokeNativeScript(invocation.instance, invocation.callback, deltaTime, "OnLateUpdate");
    }
}

void ScriptSystem::DispatchNativeDrawGUI()
{
    RebuildNativeInvocations();
    for (const NativeScriptInvocation& invocation : nativeDrawGuiInvocations)
    {
        if (invocation.instance) InvokeNativeScript(invocation.instance, invocation.callback, "OnDrawGUI");
    }
}

void ScriptSystem::DispatchManagedUpdate(float32 deltaTime)
{
    if (entryPoints.update) entryPoints.update(deltaTime);
}

void ScriptSystem::DispatchManagedFixedUpdate(float32 fixedDeltaTime)
{
    if (entryPoints.fixedUpdate) entryPoints.fixedUpdate(fixedDeltaTime);
}

void ScriptSystem::DispatchManagedLateUpdate(float32 deltaTime)
{
    if (entryPoints.lateUpdate) entryPoints.lateUpdate(deltaTime);
}

void ScriptSystem::DispatchManagedDrawGUI()
{
    if (entryPoints.drawGui) entryPoints.drawGui();
}

void ScriptSystem::NativeUpdateDomain(void* context, float32 deltaTime)
{
    static_cast<ScriptSystem*>(context)->DispatchNativeUpdate(deltaTime);
}

void ScriptSystem::NativeStartDomain(void* context)
{
    static_cast<ScriptSystem*>(context)->InitializeNativeScripts();
}

void ScriptSystem::NativeEndDomain(void* context)
{
    static_cast<ScriptSystem*>(context)->ShutdownNativeScripts();
}

void ScriptSystem::NativeFixedUpdateDomain(void* context, float32 fixedDeltaTime)
{
    static_cast<ScriptSystem*>(context)->DispatchNativeFixedUpdate(fixedDeltaTime);
}

void ScriptSystem::NativeLateUpdateDomain(void* context, float32 deltaTime)
{
    static_cast<ScriptSystem*>(context)->DispatchNativeLateUpdate(deltaTime);
}

void ScriptSystem::NativeDrawGuiDomain(void* context)
{
    static_cast<ScriptSystem*>(context)->DispatchNativeDrawGUI();
}

void ScriptSystem::ManagedUpdateDomain(void* context, float32 deltaTime)
{
    static_cast<ScriptSystem*>(context)->DispatchManagedUpdate(deltaTime);
}

void ScriptSystem::ManagedStartDomain(void* context)
{
    ScriptSystem* system = static_cast<ScriptSystem*>(context);
    OrbedenNativeApi nativeApi = OrbedenNativeApi::Create(system->world);
    if (system->entryPoints.initialize)
        system->entryPoints.initialize(&nativeApi, static_cast<uint32>(system->executionMode));
}

void ScriptSystem::ManagedEndDomain(void* context)
{
    ScriptSystem* system = static_cast<ScriptSystem*>(context);
    if (system->entryPoints.shutdown) system->entryPoints.shutdown();
}

void ScriptSystem::ManagedFixedUpdateDomain(void* context, float32 fixedDeltaTime)
{
    static_cast<ScriptSystem*>(context)->DispatchManagedFixedUpdate(fixedDeltaTime);
}

void ScriptSystem::ManagedLateUpdateDomain(void* context, float32 deltaTime)
{
    static_cast<ScriptSystem*>(context)->DispatchManagedLateUpdate(deltaTime);
}

void ScriptSystem::ManagedDrawGuiDomain(void* context)
{
    static_cast<ScriptSystem*>(context)->DispatchManagedDrawGUI();
}

void ScriptSystem::ProcessManagedInput(float32 deltaTime)
{
    //输入阶段在 FixedUpdate 之前跑；暂停与编辑模式都不中断它，编辑模式只跳过交互路由。
    if (!initialized || !entryPoints.processInput) return;
    if (executionMode == ScriptExecutionMode::Editor) return;

    PROFILE("Script/ProcessInput");
    entryPoints.processInput(deltaTime);
}

void ScriptSystem::PrepareManagedRender(float32 deltaTime)
{
    //渲染准备不在暂停门控内：暂停的 Play 里菜单仍要能响应。
    if (!initialized || !entryPoints.prepareRender) return;

    PROFILE("Script/PrepareRender");
    entryPoints.prepareRender(deltaTime);
}

void ScriptSystem::DispatchExternalCallbacks(const std::function<void()>& callback)
{
    if (!callback) return;

    //以作用域恢复派发标志：回调里再进脚本阶段时，外层保护不能被提前清掉。
    struct DispatchScope
    {
        bool& flag;
        bool previous;
        DispatchScope(bool& value) : flag(value), previous(value) { flag = true; }
        ~DispatchScope() { flag = previous; }
    } scope(domainDispatching);

    callback();
    ApplyDeferredMutations();
}

void ScriptSystem::Update(World& currentWorld, float32 deltaTime)
{
    PROFILE("Script/Update");

    (void)currentWorld;
    if (!initialized) return;
    if (executionMode == ScriptExecutionMode::Editor) return;

    for (const ScriptDomainEntry& domain : domains)
    {
        ApplyDeferredMutations();
        domainDispatching = true;
        if (domain.callbacks.update) domain.callbacks.update(domain.context, deltaTime);
        domainDispatching = false;
    }
}

void ScriptSystem::FixedUpdate(World& currentWorld, float32 fixedDeltaTime)
{
    PROFILE("Script/FixedUpdate");

    (void)currentWorld;
    if (!initialized) return;
    if (executionMode == ScriptExecutionMode::Editor) return;

    for (const ScriptDomainEntry& domain : domains)
    {
        ApplyDeferredMutations();
        domainDispatching = true;
        if (domain.callbacks.fixedUpdate) domain.callbacks.fixedUpdate(domain.context, fixedDeltaTime);
        domainDispatching = false;
    }
}

void ScriptSystem::LateUpdate(World& currentWorld, float32 deltaTime)
{
    PROFILE("Script/LateUpdate");

    (void)currentWorld;
    if (!initialized) return;
    if (executionMode == ScriptExecutionMode::Editor) return;

    for (const ScriptDomainEntry& domain : domains)
    {
        ApplyDeferredMutations();
        domainDispatching = true;
        if (domain.callbacks.lateUpdate) domain.callbacks.lateUpdate(domain.context, deltaTime);
        domainDispatching = false;
    }
}

void ScriptSystem::DrawOverlay()
{
    if (!initialized) return;
    if (executionMode == ScriptExecutionMode::Editor) return;

    for (const ScriptDomainEntry& domain : domains)
    {
        ApplyDeferredMutations();
        domainDispatching = true;
        if (domain.callbacks.drawGUI) domain.callbacks.drawGUI(domain.context);
        domainDispatching = false;
    }
}

void ScriptSystem::OnEnsWorldActiveChanged(EnsId ens, bool worldActive)
{
    if (initialized && entryPoints.ensWorldActiveChanged)
    {
        entryPoints.ensWorldActiveChanged(ens, worldActive ? uint8(1) : uint8(0));
    }
}

void ScriptSystem::OnEnsDestroyed(EnsId ens)
{
    if (initialized && entryPoints.ensDestroyed) entryPoints.ensDestroyed(ens);
}

bool ScriptSystem::IsInitialized() const
{
    return initialized;
}
