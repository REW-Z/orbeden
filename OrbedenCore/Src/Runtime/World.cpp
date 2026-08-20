#include "Runtime/World.h"

#include "Memory/MemoryManager.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/Object/TransformComponent.h"

#include <algorithm>
#include <cassert>

namespace
{
    //获取当前活动世界指针
    World*& GetCurrentWorldStorage()
    {
        static World* currentWorld = nullptr;
        return currentWorld;
    }
}

//查找组件稀疏集
ComponentStorage* World::FindComponentStorage(Type* type) const
{
    if (!type || type->GetId() >= componentStorages.size()) return nullptr;

    ComponentStorage* storage = componentStorages[type->GetId()];
    return storage && storage->GetType() == type ? storage : nullptr;
}

//获取或创建组件稀疏集
ComponentStorage* World::GetOrCreateComponentStorage(Type* type)
{
    if (!type || !type->Is(Component::StaticType())) return nullptr;

    TypeId typeId = type->GetId();
    if (typeId >= componentStorages.size())
    {
        componentStorages.resize(static_cast<usize>(typeId) + 1, nullptr);
    }

    ComponentStorage*& storage = componentStorages[typeId];
    if (!storage)
    {
        storage = NEW(ComponentStorage)ComponentStorage(this, type);
    }

    return storage;
}

//生成Ens对象ID
std::string World::AllocateEnsObjectPath()
{
    std::string instancePath;
    do
    {
        instancePath = "world://ens/" + Object::GenerateUuidText();
    } while (Object::FindObject(StringId(instancePath)));

    return instancePath;
}

//生成未命名Ens的名称
std::string World::GetEnsName(const std::string& name) const
{
    if (!name.empty()) return name;

    auto nameExists = [this](const std::string& value)
        {
            for (Ens* ens : liveEns)
            {
                if (ens && ens->alive && ens->name == value) return true;
            }

            return false;
        };

    constexpr const char* baseName = "Unnamed";
    if (!nameExists(baseName)) return baseName;

    uint32 index = 1;
    std::string resolvedName;
    do
    {
        resolvedName = std::string(baseName) + "(" + std::to_string(index++) + ")";
    } while (nameExists(resolvedName));

    return resolvedName;
}

//生成世界运行时对象ID
std::string World::AllocateRuntimeObjectPath(Type* type)
{
    return Object::CreateRuntimeInstancePath("world://runtime", type);
}

//接收世界拥有的运行时对象
bool World::AddOwnedObject(Object* object)
{
    if (!object) return false;
    if (object->GetWorld() != this) return false;
    if (object->GetOwnership() != Object::Ownership::WorldOwned) return false;
    if (object->Is(Component::StaticType())) return false;
    if (std::find(ownedObjects.begin(), ownedObjects.end(), object) != ownedObjects.end()) return true;

    ownedObjects.push_back(object);
    return true;
}

//摘除世界拥有的运行时对象
bool World::RemoveOwnedObject(Object* object)
{
    auto it = std::find(ownedObjects.begin(), ownedObjects.end(), object);
    if (it == ownedObjects.end()) return false;

    ownedObjects.erase(it);
    return true;
}

//设置 Ens 的 localActive 并传播层级状态
void World::SetEnsLocalActive(EnsId ens, bool active)
{
    Ens* storedEns = GetEns(ens);
    if (!storedEns || storedEns->localActive == active) return;

    storedEns->localActive = active;
    RefreshEnsWorldActive(ens);
}

//刷新指定 Ens 子树的 worldActive
void World::RefreshEnsWorldActive(EnsId ens)
{
    Ens* storedEns = GetEns(ens);
    TransformComponent* transform = GetTransformComponent(ens);
    if (!storedEns || !transform) return;

    Ens* parent = GetEns(transform->parent);
    bool active = storedEns->localActive && (!parent || parent->worldActive);
    if (storedEns->worldActive != active)
    {
        storedEns->worldActive = active;

        List<Component*> componentInstances = storedEns->componentInstances;
        for (Component* component : componentInstances)
        {
            if (component) component->OnWorldActiveChanged(active);
        }
    }

    EnsId child = transform->firstChild;
    while (!child.IsNull())
    {
        TransformComponent* childTransform = GetTransformComponent(child);
        EnsId nextChild = childTransform ? childTransform->next : EnsId();
        RefreshEnsWorldActive(child);
        child = nextChild;
    }
}

//获取当前活动世界
World* World::CurrentWorld()
{
    return GetCurrentWorldStorage();
}

//设置当前活动世界
void World::SetCurrentWorld(World* world)
{
    GetCurrentWorldStorage() = world;
}

//注册变换监听器
void World::AddTransformListener(ITransformListener* listener)
{
    if (!listener) return;
    if (std::find(transformListeners.begin(), transformListeners.end(), listener) != transformListeners.end()) return;

    transformListeners.push_back(listener);
}

//注销变换监听器
void World::RemoveTransformListener(ITransformListener* listener)
{
    auto it = std::find(transformListeners.begin(), transformListeners.end(), listener);
    if (it != transformListeners.end()) transformListeners.erase(it);
}

//通知指定节点及其子树的世界变换失效
void World::NotifyTransformChanged(EnsId ens)
{
    TransformComponent* transform = GetTransformComponent(ens);
    if (!transform) return;

    transform->transformDirty = true;
    for (ITransformListener* listener : transformListeners)
    {
        if (listener) listener->OnTransformChanged(*this, ens);
    }
}

//销毁世界及其运行时对象
World::~World()
{
    Clear();

    if (CurrentWorld() == this)
    {
        SetCurrentWorld(nullptr);
    }
}

//清空世界运行时对象
void World::Clear()
{
    while (!liveEns.empty())
    {
        DestroyEns(liveEns.back()->GetId());
    }

    //再销毁独立世界对象
    while (!ownedObjects.empty())
    {
        Object* object = ownedObjects.back();
        ownedObjects.pop_back();
        object->SetWorld(nullptr);
        object->SetOwnership(Object::Ownership::None);
        Object::DestroyDetachedInstance(object);
    }

    //所有Ens销毁后释放空的组件稀疏集
    for (ComponentStorage*& storage : componentStorages)
    {
        assert(!storage || storage->GetCount() == 0);
        DELETE(storage);
    }

    ensSlots.clear();
    liveEns.clear();
    freeEnsIds.clear();
    componentStorages.clear();
    renderSettings = RenderSettings();
}

//创建Ens
Ens* World::CreateEns(const std::string& name)
{
    return CreateEnsInternal(GetEnsName(name), AllocateEnsObjectPath());
}

//使用稳定ID创建Ens
Ens* World::CreateEnsWithStableId(const std::string& stableId, const std::string& name)
{
    if (stableId.empty()) return CreateEns(name);
    if (Object::FindObject(StringId(stableId))) return nullptr;

    return CreateEnsInternal(GetEnsName(name), stableId);
}

//使用指定稳定ID创建Ens
Ens* World::CreateEnsInternal(const std::string& name, const std::string& stableId)
{
    //分配Ens句柄
    EnsId value;
    EnsSlot* slot = nullptr;
    Ens* storedEns = nullptr;
    if (!freeEnsIds.empty())
    {
        value.id = freeEnsIds.back();
        freeEnsIds.pop_back();

        slot = &ensSlots[value.id];
        value.version = slot->version + 1;
        if (value.version == 0) value.version = 1;
        slot->version = value.version;
    }
    else
    {
        value.id = static_cast<uint32>(ensSlots.size());
        value.version = 1;
        ensSlots.push_back(EnsSlot());
        slot = &ensSlots.back();
        slot->version = value.version;
    }

    storedEns = NEW(Ens)Ens(this, value);
    slot->value = storedEns;
    slot->denseIndex = static_cast<uint32>(liveEns.size());
    liveEns.push_back(storedEns);

    ComponentStorage* transformStorage = GetOrCreateComponentStorage(TransformComponent::StaticType());
    Component* transformComponent = transformStorage ? transformStorage->Create(value, stableId) : nullptr;
    TransformComponent* transform = transformComponent ? transformComponent->Cast<TransformComponent>() : nullptr;
    if (!transform)
    {
        storedEns->alive = false;
        liveEns.pop_back();
        slot->value = nullptr;
        slot->denseIndex = EnsId::InvalidId;
        storedEns->~Ens();
        Memory::GetHeapAllocator()->Deallocate(reinterpret_cast<std::byte*>(storedEns));
        freeEnsIds.push_back(value.id);
        return nullptr;
    }

    storedEns->name = name;
    storedEns->AddComponentInstance(transform);
    NotifyTransformChanged(value);
    return storedEns;
}

//销毁Ens
bool World::DestroyEns(EnsId ens)
{
    Ens* storedEns = GetEns(ens);
    if (!storedEns) return false;

    TransformComponent* transform = GetTransformComponent(ens);
    if (!transform) return false;

    //停用待销毁 Ens
    SetEnsLocalActive(ens, false);

    //解除子级关系
    EnsId child = transform->firstChild;
    while (!child.IsNull())
    {
        TransformComponent* childTransform = GetTransformComponent(child);
        EnsId nextChild = childTransform ? childTransform->next : EnsId();
        SetParent(child, EnsId());
        child = nextChild;
    }

    //解除父级关系
    SetParent(ens, EnsId());

    //销毁额外组件实例
    List<Component*> componentInstances = storedEns->componentInstances;
    for (Component* component : componentInstances)
    {
        if (component && component != transform)
        {
            RemoveComponent(component);
        }
    }

    //注销并销毁变换组件
    ComponentStorage* transformStorage = FindComponentStorage(TransformComponent::StaticType());
    Component* removedTransform = transformStorage ? transformStorage->Remove(transform) : nullptr;
    assert(removedTransform == transform);
    storedEns->RemoveComponentInstance(transform);
    transform->SetEnsId(EnsId());
    transform->SetWorld(nullptr);
    transform->SetOwnership(Object::Ownership::None);
    bool transformDeleted = Object::DestroyDetachedInstance(transform);
    assert(transformDeleted);

    storedEns->alive = false;

    EnsSlot& slot = ensSlots[ens.id];
    assert(slot.denseIndex < liveEns.size());
    assert(liveEns[slot.denseIndex] == storedEns);

    //移除 Ens 存活记录
    Ens* movedEns = liveEns.back();
    liveEns[slot.denseIndex] = movedEns;
    liveEns.pop_back();
    if (movedEns != storedEns)
    {
        ensSlots[movedEns->GetId().id].denseIndex = slot.denseIndex;
    }

    slot.value = nullptr;
    slot.denseIndex = EnsId::InvalidId;
    storedEns->~Ens();
    Memory::GetHeapAllocator()->Deallocate(reinterpret_cast<std::byte*>(storedEns));

    freeEnsIds.push_back(ens.id);
    return true;
}

//获取World持有的唯一Ens实例
Ens* World::GetEns(EnsId ens)
{
    if (ens.IsNull()) return nullptr;
    if (ens.id >= ensSlots.size()) return nullptr;

    EnsSlot& slot = ensSlots[ens.id];
    if (!slot.value) return nullptr;
    if (slot.version != ens.version) return nullptr;
    if (!slot.value->alive) return nullptr;

    return slot.value;
}

//获取World持有的唯一Ens实例
const Ens* World::GetEns(EnsId ens) const
{
    return const_cast<World*>(this)->GetEns(ens);
}

//判断Ens是否存活
bool World::IsAlive(EnsId ens) const
{
    return GetEns(ens) != nullptr;
}

//获取变换组件
TransformComponent* World::GetTransformComponent(EnsId ens) const
{
    if (!IsAlive(ens)) return nullptr;

    ComponentStorage* storage = FindComponentStorage(TransformComponent::StaticType());
    Component* component = storage ? storage->Get(ens) : nullptr;
    return component ? component->Cast<TransformComponent>() : nullptr;
}

//设置父级
void World::SetParent(EnsId child, EnsId parent)
{
    TransformComponent* transform = GetTransformComponent(child);
    if (!transform) return;
    if (child == parent) return;
    if (!parent.IsNull() && !IsAlive(parent)) return;

    //检查父级循环
    EnsId current = parent;
    while (!current.IsNull())
    {
        if (current == child) return;

        TransformComponent* currentTransform = GetTransformComponent(current);
        current = currentTransform ? currentTransform->parent : EnsId();
    }

    //从旧父级摘除
    TransformComponent* oldParent = GetTransformComponent(transform->parent);
    TransformComponent* previous = GetTransformComponent(transform->prev);
    TransformComponent* next = GetTransformComponent(transform->next);

    if (oldParent && oldParent->firstChild == child) oldParent->firstChild = transform->next;
    if (oldParent && oldParent->lastChild == child) oldParent->lastChild = transform->prev;
    if (previous) previous->next = transform->next;
    if (next) next->prev = transform->prev;

    transform->parent = EnsId();
    transform->prev = EnsId();
    transform->next = EnsId();

    if (parent.IsNull())
    {
        NotifyTransformChanged(child);
        RefreshEnsWorldActive(child);
        return;
    }

    //挂到新父级末尾
    TransformComponent* parentTransform = GetTransformComponent(parent);
    if (!parentTransform) return;

    TransformComponent* lastChild = GetTransformComponent(parentTransform->lastChild);
    transform->parent = parent;
    transform->prev = parentTransform->lastChild;

    if (lastChild)
    {
        lastChild->next = child;
    }
    else
    {
        parentTransform->firstChild = child;
    }

    parentTransform->lastChild = child;
    NotifyTransformChanged(child);
    RefreshEnsWorldActive(child);
}

//移动 Ens 到指定同级位置
bool World::MoveEns(EnsId child, EnsId parent, EnsId beforeSibling)
{
    TransformComponent* transform = GetTransformComponent(child);
    if (!transform || child == parent || child == beforeSibling) return false;
    if (!parent.IsNull() && !IsAlive(parent)) return false;

    //验证同层插入目标
    if (!beforeSibling.IsNull())
    {
        TransformComponent* beforeTransform = GetTransformComponent(beforeSibling);
        if (!beforeTransform || beforeTransform->parent != parent) return false;
    }

    //验证目标父级
    EnsId current = parent;
    while (!current.IsNull())
    {
        if (current == child) return false;
        TransformComponent* currentTransform = GetTransformComponent(current);
        current = currentTransform ? currentTransform->parent : EnsId();
    }

    SetParent(child, parent);
    transform = GetTransformComponent(child);
    if (!transform || transform->parent != parent) return false;

    //获取根节点插入位置
    if (parent.IsNull())
    {
        Ens* childEns = GetEns(child);
        Ens* beforeEns = beforeSibling.IsNull() ? nullptr : GetEns(beforeSibling);
        auto childIt = std::find(liveEns.begin(), liveEns.end(), childEns);
        if (childIt == liveEns.end()) return false;

        liveEns.erase(childIt);
        auto beforeIt = beforeEns ? std::find(liveEns.begin(), liveEns.end(), beforeEns) : liveEns.end();
        liveEns.insert(beforeIt, childEns);
        for (usize index = 0; index < liveEns.size(); ++index)
        {
            ensSlots[liveEns[index]->GetId().id].denseIndex = static_cast<uint32>(index);
        }
        return true;
    }

    //处理空插入目标
    if (beforeSibling.IsNull()) return true;

    TransformComponent* parentTransform = GetTransformComponent(parent);
    TransformComponent* beforeTransform = GetTransformComponent(beforeSibling);
    TransformComponent* previous = GetTransformComponent(transform->prev);
    if (!parentTransform || !beforeTransform) return false;

    //调整同级节点顺序
    if (previous) previous->next = EnsId();
    parentTransform->lastChild = transform->prev;

    TransformComponent* beforePrevious = GetTransformComponent(beforeTransform->prev);
    transform->prev = beforeTransform->prev;
    transform->next = beforeSibling;
    beforeTransform->prev = child;
    if (beforePrevious) beforePrevious->next = child;
    else parentTransform->firstChild = child;
    return true;
}

//获取父级
Ens* World::GetParent(EnsId child) const
{
    TransformComponent* transform = GetTransformComponent(child);
    return transform ? const_cast<World*>(this)->GetEns(transform->parent) : nullptr;
}

//添加组件
Component* World::AddComponent(EnsId ens, Type* type)
{
    TransformComponent* transform = GetTransformComponent(ens);
    if (!transform || !type || !type->Is(Component::StaticType())) return nullptr;
    if (type == TransformComponent::StaticType()) return transform;

    Component* oldComponent = GetComponent(ens, type);
    if (oldComponent) return oldComponent;

    return AddComponentInstance(ens, type);
}

//添加同类型的独立组件实例
Component* World::AddComponentInstance(EnsId ens, Type* type)
{
    TransformComponent* transform = GetTransformComponent(ens);
    if (!transform || !type || !type->Is(Component::StaticType()) || !type->CanCreateObject()) return nullptr;
    if (type == TransformComponent::StaticType()) return transform;

    Ens* storedEns = GetEns(ens);
    if (!storedEns) return nullptr;

    //创建并注册组件
    std::string instancePath = Object::CreateRuntimeInstancePath(transform->GetInstanceId().GetPath(), type);
    ComponentStorage* storage = GetOrCreateComponentStorage(type);
    Component* component = storage ? storage->Create(ens, instancePath) : nullptr;
    if (!component) return nullptr;

    storedEns->AddComponentInstance(component);
    component->OnAttach();
    return component;
}

//获取组件
Component* World::GetComponent(EnsId ens, Type* type) const
{
    if (!type || !IsAlive(ens)) return nullptr;

    ComponentStorage* storage = FindComponentStorage(type);
    return storage ? storage->Get(ens) : nullptr;
}

//获取指定类型的全部组件实例
void World::GetComponentInstances(EnsId ens, Type* type, List<Component*>& output) const
{
    output.clear();
    if (!type) return;

    ComponentStorage* storage = FindComponentStorage(type);
    if (storage) storage->GetAll(ens, output);
}

//移除组件
bool World::RemoveComponent(EnsId ens, Type* type)
{
    Component* component = GetComponent(ens, type);
    return RemoveComponent(component);
}

//移除指定组件实例
bool World::RemoveComponent(Component* component)
{
    if (!component) return false;
    if (component->GetWorld() != this) return false;
    EnsId ens = component->GetEnsId();
    TransformComponent* transform = GetTransformComponent(ens);
    if (!transform || component == transform) return false;

    ComponentStorage* storage = FindComponentStorage(component->GetType());
    if (!storage) return false;

    //执行组件卸载回调
    component->OnDetach();

    //移除组件索引和对象
    Component* removedComponent = storage->Remove(component);
    if (removedComponent != component) return false;

    Ens* storedEns = GetEns(ens);
    if (storedEns) storedEns->RemoveComponentInstance(component);
    component->SetEnsId(EnsId());
    component->SetWorld(nullptr);
    component->SetOwnership(Object::Ownership::None);
    bool deleted = Object::DestroyDetachedInstance(component);
    assert(deleted);
    return deleted;
}

//按稳定ID查找Ens
Ens* World::FindEns(const StringId& id) const
{
    Object* object = Object::FindObject(id);
    TransformComponent* transform = object ? object->Cast<TransformComponent>() : nullptr;
    if (!transform) return nullptr;
    if (transform->GetWorld() != this) return nullptr;

    return const_cast<World*>(this)->GetEns(transform->GetEnsId());
}

//遍历所有存活的Ens
void World::VisitEns(EnsVisitorFunction visitor, void* userData) const
{
    if (!visitor) return;

    for (Ens* ens : liveEns)
    {
        if (!ens || !ens->alive) continue;

        visitor(ens, userData);
    }
}
