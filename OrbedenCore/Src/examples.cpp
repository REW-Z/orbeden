#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Profiler/Profiler.h"
#include "Runtime/Ens.h"
#include "Runtime/Reflection.h"
#include "Runtime/World.h"

#include <filesystem>
#include <fstream>
#include <string>



namespace examples
{
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

    //反射字段和方法调用示例
    void ExampleReflection()
    {
        Reflection::RegisterGeneratedReflection();

        World world;
        World* previousWorld = World::CurrentWorld();
        World::SetCurrentWorld(&world);

        Ens ens = world.CreateEnsWithStableId("world://examples/reflection_ens", "ReflectionEns");
        EnsComponent* basic = ens.Basic();
        if (!basic)
        {
            Log::Error("反射示例创建 Ens 失败");
            World::SetCurrentWorld(previousWorld);
            return;
        }

        Type* type = basic->GetType();
        std::string typeLine = std::string("反射类型: ") + type->GetName();
        Log::Info(typeLine.c_str());

        //遍历当前类型直接声明的字段
        for (const Reflection::FieldInfo& field : type->GetFields())
        {
            std::string line = std::string("字段: ") + field.name + " type=" + field.typeName;
            line += field.persistent ? " persistent=true" : " persistent=false";
            if (field.getter)
            {
                line += " value=" + field.GetValueAsString(basic);
            }

            Log::Info(line.c_str());
        }

        //通过字段名查找并写入字符串字段
        const Reflection::FieldInfo* nameField = type->GetField("name");
        if (nameField && nameField->SetValueFromString(basic, "ReflectedEns"))
        {
            std::string line = "name 字段写入后: " + nameField->GetValueAsString(basic);
            Log::Info(line.c_str());
        }

        //通过反射值写入 vector3 字段
        const Reflection::FieldInfo* positionField = type->GetField("localPosition");
        if (positionField)
        {
            vector3 reflectedPosition;
            reflectedPosition.x = 1.0f;
            reflectedPosition.y = 2.0f;
            reflectedPosition.z = 3.0f;

            if (positionField->SetValue(basic, Reflection::Value(reflectedPosition)))
            {
                Reflection::Value value = positionField->GetValue(basic);
                std::string line = "localPosition 字段写入后: " + value.ToString();
                Log::Info(line.c_str());
            }
        }

        //调用 Object 上的方法，参数和返回值都通过 Reflection::Value 传递
        Object reflectedObject;
        const Reflection::MethodInfo* setIdMethod = Object::StaticType()->GetMethod("SetInstanceId");
        if (setIdMethod)
        {
            List<Reflection::Value> args;
            args.push_back(Reflection::Value(StringId("world://examples/reflection_object")));

            bool success = false;
            setIdMethod->Invoke(&reflectedObject, args, &success);
            Log::Info(success ? "SetInstanceId 调用成功" : "SetInstanceId 调用失败");
        }

        const Reflection::MethodInfo* getIdMethod = Object::StaticType()->GetMethod("GetInstanceId");
        if (getIdMethod)
        {
            bool success = false;
            Reflection::Value value = getIdMethod->Invoke(&reflectedObject, List<Reflection::Value>(), &success);
            if (success)
            {
                std::string line = "GetInstanceId 返回: " + value.ToString();
                Log::Info(line.c_str());
            }
        }

        //调用 Component 上的方法，派生类型实例也可以作为 Object* 传入
        const Reflection::MethodInfo* getEnsIdMethod = Component::StaticType()->GetMethod("GetEnsId");
        if (getEnsIdMethod)
        {
            bool success = false;
            Reflection::Value value = getEnsIdMethod->Invoke(basic, List<Reflection::Value>(), &success);
            if (success)
            {
                std::string line = "GetEnsId 返回: " + value.ToString();
                Log::Info(line.c_str());
            }
        }

        World::SetCurrentWorld(previousWorld);
    }
}
