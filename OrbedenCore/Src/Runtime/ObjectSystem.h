#pragma once

#include "Runtime/Ens.h"

#include <string>
#include <unordered_map>

class EnsComponent;

//Ens运行时记录
struct EnsRecord
{
public:
    uint32 version = 1;
    bool alive = false;
    EnsComponent* basic = nullptr;
};

//Object系统运行时数据
struct ObjectSystem
{
public:
    List<Type*> types;
    std::unordered_map<std::string, Type*> typeByName;
    std::unordered_map<uint64, Object*> objectById;
    List<EnsRecord> enses;
    List<uint32> freeEnsIds;
    std::unordered_map<uint64, Component*> componentByEnsAndType;
    uint64 nextObjectIndex = 1;
};

//获取对象系统数据
ObjectSystem& GetObjectSystem();

//生成组件映射Key
uint64 GetComponentKey(EnsId ens, Type* type);

//判断Ens句柄是否有效
bool IsEnsAlive(EnsId ens);

//获取基础组件
EnsComponent* GetBasicComponent(EnsId ens);

//注册组件
void RegisterComponent(EnsId ens, Component* component);

//注销组件
void UnregisterComponent(EnsId ens, Type* type);
