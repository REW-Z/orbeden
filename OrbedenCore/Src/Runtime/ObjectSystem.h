#pragma once

#include "Runtime/Ens.h"

#include <string>
#include <unordered_map>
#include <vector>

class ActorComponent;

//Actor运行时记录
struct ActorRecord
{
public:
    uint32 version = 1;
    bool alive = false;
    ActorComponent* basic = nullptr;
};

//Object系统运行时数据
struct ObjectSystem
{
public:
    std::vector<Type*> types;
    std::unordered_map<std::string, Type*> typeByName;
    std::unordered_map<uint64, Object*> objectById;
    std::vector<ActorRecord> actors;
    std::vector<uint32> freeActorIds;
    std::unordered_map<uint64, Component*> componentByActorAndType;
    uint64 nextObjectIndex = 1;
};

//获取对象系统数据
ObjectSystem& GetObjectSystem();

//生成组件映射Key
uint64 GetComponentKey(Ens ens, Type* type);

//判断Actor句柄是否有效
bool IsEnsAlive(Ens ens);

//获取基础组件
ActorComponent* GetBasicComponent(Ens ens);

//注册组件
void RegisterComponent(Ens ens, Component* component);

//注销组件
void UnregisterComponent(Ens ens, Type* type);
