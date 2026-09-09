#include "Runtime/Native/RuntimeComponentBinds.h"

#include "FileSystem/PathDefines.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Runtime/Ens.h"
#include "Runtime/Native/NativeCall.h"
#include "Runtime/Object/TransformComponent.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/World.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
    //从 UTF-8 字节创建字符串。
    std::string ReadUtf8Text(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return std::string();
        return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(length));
    }

    //复制 UTF-8 字符串到 C# 缓冲区
    int32 CopyText(const std::string& text, uint8* buffer, int32 bufferSize)
    {
        int32 byteCount = static_cast<int32>(text.size());
        if (buffer && bufferSize > 0 && byteCount > 0)
        {
            int32 copyCount = std::min(byteCount, bufferSize);
            std::memcpy(buffer, text.data(), static_cast<size_t>(copyCount));
        }

        return byteCount;
    }

    //获取当前 World 中的唯一 Ens 实例。
    Ens* GetNativeEns(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world ? world->GetEns(ens) : nullptr;
    }

    //创建 Ens。
    EnsId ORBEDEN_NATIVE_CALL NativeWorldCreateEns(const uint8* name, int32 length)
    {
        World* world = World::CurrentWorld();
        Ens* ens = world ? world->CreateEns(ReadUtf8Text(name, length)) : nullptr;
        return ens ? ens->GetId() : EnsId();
    }

    //使用稳定 ID 创建 Ens。
    EnsId ORBEDEN_NATIVE_CALL NativeWorldCreateEnsWithStableId(const uint8* stableId, int32 stableIdLength, const uint8* name, int32 nameLength)
    {
        World* world = World::CurrentWorld();
        Ens* ens = world ? world->CreateEnsWithStableId(ReadUtf8Text(stableId, stableIdLength), ReadUtf8Text(name, nameLength)) : nullptr;
        return ens ? ens->GetId() : EnsId();
    }

    //按稳定 ID 查找 Ens。
    EnsId ORBEDEN_NATIVE_CALL NativeWorldFindEns(const uint8* stableId, int32 stableIdLength)
    {
        World* world = World::CurrentWorld();
        Ens* ens = world ? world->FindEns(StringId(ReadUtf8Text(stableId, stableIdLength))) : nullptr;
        return ens ? ens->GetId() : EnsId();
    }

    //销毁 Ens。
    uint8 ORBEDEN_NATIVE_CALL NativeWorldDestroyEns(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world && world->DestroyEns(ens) ? 1 : 0;
    }

    //读取当前内容根目录。
    int32 ORBEDEN_NATIVE_CALL NativePathDefinesGetContentRoot(uint8* buffer, int32 bufferSize)
    {
        return CopyText(PathDefines::GetContentRoot(), buffer, bufferSize);
    }

    //解析内容相对路径。
    int32 ORBEDEN_NATIVE_CALL NativePathDefinesGetContentFilePath(const uint8* path, int32 length, uint8* buffer, int32 bufferSize)
    {
        return CopyText(PathDefines::GetContentFilePath(ReadUtf8Text(path, length)), buffer, bufferSize);
    }

    //判断 Ens 是否有效。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsIsAlive(EnsId ens)
    {
        World* world = World::CurrentWorld();
        return world && world->IsAlive(ens) ? 1 : 0;
    }

    //读取 Ens 的 localActive。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsGetLocalActive(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value && value->GetLocalActive() ? 1 : 0;
    }

    //读取 Ens 的 worldActive。
    uint8 ORBEDEN_NATIVE_CALL NativeEnsGetWorldActive(EnsId ens)
    {
        Ens* value = GetNativeEns(ens);
        return value && value->GetWorldActive() ? 1 : 0;
    }

    //设置 Ens 的 localActive。
    void ORBEDEN_NATIVE_CALL NativeEnsSetLocalActive(EnsId ens, uint8 active)
    {
        Ens* value = GetNativeEns(ens);
        if (value) value->SetLocalActive(active != 0);
    }

    //读取 Ens 名称到 UTF-8 缓冲区。
    int32 ORBEDEN_NATIVE_CALL NativeEnsGetName(EnsId ens, uint8* buffer, int32 bufferSize)
    {
        Ens* value = GetNativeEns(ens);
        static const std::string emptyName;
        const std::string& name = value ? value->GetName() : emptyName;
        return CopyText(name, buffer, bufferSize);
    }

    //写入 Ens 名称。
    void ORBEDEN_NATIVE_CALL NativeEnsSetName(EnsId ens, const uint8* text, int32 length)
    {
        Ens* value = GetNativeEns(ens);
        if (value) value->SetName(ReadUtf8Text(text, length));
    }

}

WorldBind WorldBind::Create()
{
    WorldBind bind;
    bind.CreateEns = reinterpret_cast<void*>(&NativeWorldCreateEns);
    bind.CreateEnsWithStableId = reinterpret_cast<void*>(&NativeWorldCreateEnsWithStableId);
    bind.FindEns = reinterpret_cast<void*>(&NativeWorldFindEns);
    bind.DestroyEns = reinterpret_cast<void*>(&NativeWorldDestroyEns);
    return bind;
}

PathDefinesBind PathDefinesBind::Create()
{
    PathDefinesBind bind;
    bind.GetContentRoot = reinterpret_cast<void*>(&NativePathDefinesGetContentRoot);
    bind.GetContentFilePath = reinterpret_cast<void*>(&NativePathDefinesGetContentFilePath);
    return bind;
}

EnsBind EnsBind::Create()
{
    EnsBind bind;
    bind.IsAlive = reinterpret_cast<void*>(&NativeEnsIsAlive);
    bind.GetLocalActive = reinterpret_cast<void*>(&NativeEnsGetLocalActive);
    bind.GetWorldActive = reinterpret_cast<void*>(&NativeEnsGetWorldActive);
    bind.SetLocalActive = reinterpret_cast<void*>(&NativeEnsSetLocalActive);
    bind.GetName = reinterpret_cast<void*>(&NativeEnsGetName);
    bind.SetName = reinterpret_cast<void*>(&NativeEnsSetName);
    return bind;
}

