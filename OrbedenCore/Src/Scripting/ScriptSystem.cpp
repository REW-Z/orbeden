#include "Scripting/ScriptSystem.h"

#include "Log/Log.h"
#include "Runtime/Native/OrbedenNativeApi.h"
#include "Scripting/ScriptInterop.h"

#include <algorithm>
#include <exception>

#if defined(ORBEDEN_PLAYER)
extern "C"
{
    void ORBEDEN_NATIVE_CALL OrbedenGame_Initialize(void* nativeApi);
    void ORBEDEN_NATIVE_CALL OrbedenGame_Shutdown();
    void ORBEDEN_NATIVE_CALL OrbedenGame_Update(float32 deltaTime);
    void ORBEDEN_NATIVE_CALL OrbedenGame_FixedUpdate(float32 fixedDeltaTime);
    void ORBEDEN_NATIVE_CALL OrbedenGame_LateUpdate(float32 deltaTime);
    void ORBEDEN_NATIVE_CALL OrbedenGame_EnsWorldActiveChanged(EnsId ens, uint8 worldActive);
    void ORBEDEN_NATIVE_CALL OrbedenGame_EnsDestroyed(EnsId ens);
    void ORBEDEN_NATIVE_CALL OrbedenGame_DrawGui();
}
#endif

namespace
{
    ScriptSystem* currentScriptSystem = nullptr;

    void LogNativeScriptException(ScriptBehaviour* script, const char* phase, const char* message)
    {
        std::string typeName = script && script->GetType() ? script->GetType()->GetName() : "<unknown>";
        Log::Error(("C++ script " + typeName + " failed in " + phase + ": " + (message ? message : "unknown exception")).c_str());
    }

    void InvokeNativeScript(ScriptBehaviour* script, ScriptCallback callback, const char* phase)
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

    void InvokeNativeScript(ScriptBehaviour* script, ScriptUpdateCallback callback, float32 deltaTime, const char* phase)
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

#if defined(ORBEDEN_PLAYER)
void ScriptSystem::SetAotEntryPoints()
{
    entryPoints.initialize = &OrbedenGame_Initialize;
    entryPoints.shutdown = &OrbedenGame_Shutdown;
    entryPoints.update = &OrbedenGame_Update;
    entryPoints.fixedUpdate = &OrbedenGame_FixedUpdate;
    entryPoints.lateUpdate = &OrbedenGame_LateUpdate;
    entryPoints.ensWorldActiveChanged = &OrbedenGame_EnsWorldActiveChanged;
    entryPoints.ensDestroyed = &OrbedenGame_EnsDestroyed;
    entryPoints.drawGui = &OrbedenGame_DrawGui;
}
#endif

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
    ScriptBehaviour::RegisterReflection();
    ScriptInterop::Initialize(world);
    currentScriptSystem = this;

    if (runtimeMode == ScriptRuntimeMode::CLR) return true;

#if defined(ORBEDEN_PLAYER)
    SetAotEntryPoints();
    return true;
#else
    Log::Error("ScriptSystem AOT mode is unavailable in this Core build.");
    return false;
#endif
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

bool ScriptSystem::Initialize()
{
    if (initialized) return true;
    if (!world || !renderSystem || !entryPoints.IsValid())
    {
        Log::Error("ScriptSystem initialize failed: script domains are incomplete.");
        return false;
    }

    ScriptInterop::Initialize(world);

    domains.clear();
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
    entryPoints = ScriptEntryPoints();
    initialized = false;
    renderOverlayAttached = false;
    domainDispatching = false;
    applyingDeferredMutations = false;
    deferredComponentRemovals.clear();
    deferredEnsDestructions.clear();
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
                ScriptBehaviour* script = component ? component->Cast<ScriptBehaviour>() : nullptr;
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
        ScriptBehaviour* script = ResolveNativeScript(objectId);
        if (!script || !script->runtimeRegistered) continue;

        script->runtimeRegistered = false;
        bool callEnd = script->scriptStarted;
        script->scriptStarted = false;
        if (callEnd)
        {
            ScriptCallbackTable callbacks = ResolveScriptCallbacks(script->GetType());
            InvokeNativeScript(script, callbacks.end, "OnEnd");
        }
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
    while (nativeListsDirty)
    {
        nativeListsDirty = false;
        nativeUpdateInvocations.clear();
        nativeFixedUpdateInvocations.clear();
        nativeLateUpdateInvocations.clear();
        nativeDrawGuiInvocations.clear();

        List<int32> scriptIds = nativeScriptIds;
        for (int32 objectId : scriptIds)
        {
            ScriptBehaviour* script = ResolveNativeScript(objectId);
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

void ScriptSystem::TombstoneNativeInvocations(ScriptBehaviour* script)
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

bool ScriptSystem::IsNativeScriptRunnable(ScriptBehaviour* script) const
{
    if (!script || script->GetDomain() != ScriptDomain::Native || !script->runtimeRegistered || !script->enabled) return false;
    Ens* owner = script->GetEns();
    return owner && owner->GetWorldActive();
}

ScriptBehaviour* ScriptSystem::ResolveNativeScript(int32 objectId) const
{
    Object* object = Object::FindObjectById(objectId);
    return object ? object->Cast<ScriptBehaviour>() : nullptr;
}

void ScriptSystem::AttachNativeScript(ScriptBehaviour* script)
{
    if (!initialized || !script || script->GetDomain() != ScriptDomain::Native || script->runtimeRegistered) return;

    script->runtimeRegistered = true;
    script->scriptStarted = false;
    nativeScriptIds.push_back(script->GetObjectId());
    nativeListsDirty = true;
}

void ScriptSystem::DetachNativeScript(ScriptBehaviour* script)
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

void ScriptSystem::RefreshNativeScript(ScriptBehaviour* script)
{
    if (!initialized || !script || !script->runtimeRegistered) return;
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
    if (system->entryPoints.initialize) system->entryPoints.initialize(&nativeApi);
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

void ScriptSystem::Update(World& currentWorld, float32 deltaTime)
{
    (void)currentWorld;
    if (!initialized) return;

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
    (void)currentWorld;
    if (!initialized) return;

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
    (void)currentWorld;
    if (!initialized) return;

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
