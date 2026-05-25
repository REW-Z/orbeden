#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Profiler/Profiler.h"
#include "Runtime/Ens.h"
#include "Runtime/World.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    int gProbeAttachCount = 0;
    int gProbeDetachCount = 0;

    class ProbeWorldObject : public Object
    {
        OBJECT_TYPE_DECLARE(ProbeWorldObject)
    };

    class ProbeComponent : public Component
    {
        OBJECT_TYPE_DECLARE(ProbeComponent)

    public:
        void OnAttach() override
        {
            ++gProbeAttachCount;
        }

        void OnDetach() override
        {
            ++gProbeDetachCount;
        }
    };

    OBJECT_TYPE_IMPLEMENT(ProbeWorldObject, Object)
    OBJECT_TYPE_IMPLEMENT(ProbeComponent, Component)

    //同步读取文件并释放
    void ExampleSyncReadFile(const std::string& path)
    {
        File* file = nullptr;
        {
            PROFILE("SyncReadFile");
            file = FileSystem::LoadFile(path, FileSystem::IOSLIB::Fstream, FileSystem::MODE::NORMAL);
        }

        FileText* text = static_cast<FileText*>(file);

        Log::Info("同步读取:");
        Log::Info(text->content.c_str());

        DELETE(file);
    }

    //异步读取文件并释放
    void ExampleAsyncReadFile(const std::string& path)
    {
        AsyncFileResult* result = FileSystem::LoadFileAsync(path, FileSystem::IOSLIB::Fstream, FileSystem::MODE::NORMAL);
        result->task.wait();

        FileText* text = static_cast<FileText*>(result->filePtr);

        Log::Info("异步读取:");
        Log::Info(text->content.c_str());

        DELETE(result->filePtr);
        DELETE(result);
    }

    //yield 异步文件结果
    AsyncFileRoutine ExampleYieldAsyncFile(const std::string& path)
    {
        AsyncFileResult* result = FileSystem::LoadFileAsync(path, FileSystem::IOSLIB::Fstream, FileSystem::MODE::NORMAL);
        co_yield result;

        FileText* text = static_cast<FileText*>(result->filePtr);

        Log::Info("yield 异步读取:");
        Log::Info(text->content.c_str());

        DELETE(result->filePtr);
        DELETE(result);
    }

    //运行 yield 文件协程
    void RunYieldAsyncFile(AsyncFileRoutine routine)
    {
        while (routine.MoveNext())
        {
            routine.WaitCurrent();
        }
    }

    //验证World运行时行为
    void ExampleWorld()
    {
        gProbeAttachCount = 0;
        gProbeDetachCount = 0;

        World world;
        World::SetCurrentWorld(&world);

        //验证显式world和兼容静态入口
        Ens garden = world.CreateEns("Garden");
        Ens tree = Ens::Create("Tree");
        assert(garden.IsValid());
        assert(tree.IsValid());
        assert(garden.GetWorld() == &world);
        assert(tree.GetWorld() == &world);

        tree.SetParent(garden);
        tree.SetLocalPosition({ 1.0f, 2.0f, 3.0f });
        assert(tree.GetParent().GetEns() == garden.GetEns());
        assert(tree.GetLocalPosition().x == 1.0f);

        //验证弱引用和基础查找
        Type* basicType = typeof(EnsComponent);
        Ref<EnsComponent> basicPtr(tree.Basic());
        Object* object = basicPtr.Get();
        if (object && object->Is(basicType) && object is(EnsComponent))
        {
            EnsComponent* basic = object as(EnsComponent);
            tree.SetName(basic->name);
        }

        Ens foundGarden = world.FindEns(garden.Basic()->GetInstanceId());
        assert(foundGarden.IsValid());
        assert(foundGarden.GetEns() == garden.GetEns());

        //验证组件挂载与遍历
        ProbeComponent* gardenProbe = garden.AddComponent<ProbeComponent>();
        ProbeComponent* treeProbe = tree.AddComponent<ProbeComponent>();
        assert(gardenProbe);
        assert(treeProbe);
        assert(gProbeAttachCount == 2);
        assert(garden.GetComponent<ProbeComponent>() == gardenProbe);
        assert(tree.HasComponent<ProbeComponent>());

        uint32 ensCount = 0;
        world.ForEachEns([&ensCount](Ens) { ++ensCount; });
        assert(ensCount == 2);

        uint32 probeCount = 0;
        world.ForEachComponent(typeof(ProbeComponent), [&probeCount](Component* component)
            {
                assert(component);
                ++probeCount;
            });
        assert(probeCount == 2);

        //验证独立世界对象和重复稳定ID
        ProbeWorldObject* runtimeObject = world.CreateObject<ProbeWorldObject>();
        assert(runtimeObject);
        assert(runtimeObject->GetWorld() == &world);

        Ref<ProbeWorldObject> runtimeRef(runtimeObject);
        assert(runtimeRef.Get() == runtimeObject);
        assert(world.DestroyObject(runtimeObject));
        assert(runtimeRef.Get() == nullptr);

        ProbeWorldObject* duplicateA = world.CreateObject<ProbeWorldObject>("asset://duplicate");
        ProbeWorldObject* duplicateB = world.CreateObject<ProbeWorldObject>("asset://duplicate");
        assert(duplicateA);
        assert(duplicateB == nullptr);
        assert(world.DestroyObject(duplicateA));

        //验证销毁、句柄复用和跨world父级保护
        EnsId deadTree = tree.GetEns();
        tree.Destroy();
        assert(!tree.IsValid());
        assert(gProbeDetachCount == 1);

        Ens rock = world.CreateEns("Rock");
        assert(rock.IsValid());
        assert(rock.GetEns().id == deadTree.id);
        assert(rock.GetEns().version != deadTree.version);

        World otherWorld;
        Ens foreign = otherWorld.CreateEns("Foreign");
        rock.SetParent(foreign);
        assert(!rock.GetParent().IsValid());

        ProbeComponent* rockProbe = rock.AddComponent<ProbeComponent>();
        assert(rockProbe);
        assert(rock.RemoveComponent<ProbeComponent>());
        assert(gProbeDetachCount == 2);

        garden.Destroy();
        rock.Destroy();
        foreign.Destroy();
        World::SetCurrentWorld(nullptr);
    }
}

int main()
{
    std::string path = "OrbedenCore/.bin/FileSystemExample/hello.txt";

    std::filesystem::create_directories(Path::GetDirectory(path));

    std::ofstream file(path, std::ios::out);
    file << "Hello FileSystem";
    file.close();

    {
        PROFILE("FileSystemExample");
        ExampleSyncReadFile(path);
    }

    ExampleAsyncReadFile(path);
    RunYieldAsyncFile(ExampleYieldAsyncFile(path));
    ExampleWorld();

    Profiler::WriteProfileLog();
    Profiler::Clear();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
