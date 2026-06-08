#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Profiler/Profiler.h"
#include "Runtime/Ens.h"
#include "Runtime/Reflection.h"
#include "Runtime/Camera.h"
#include "Runtime/Resources/Material.h"
#include "Runtime/Resources/MaterialShader.h"
#include "Runtime/Resources/Mesh.h"
#include "Runtime/SpaceComponent.h"
#include "Runtime/StaticMeshRenderer.h"
#include "Runtime/World.h"

#include <filesystem>
#include <fstream>
#include <string>



namespace examples
{
    template<typename T>
    T* GetOrCreateObject(World& world, const std::string& id)
    {
        Object* found = Object::FindObject(StringId(id));
        if (found)
        {
            return found->Cast<T>();
        }

        return world.CreateObject<T>(id);
    }

    //创建渲染系统验证场景
    void ExampleRenderScene(World& world)
    {
        MaterialShader* shader = GetOrCreateObject<MaterialShader>(world, "world://examples/render/basic_shader");
        Material* material = GetOrCreateObject<Material>(world, "world://examples/render/basic_material");
        Mesh* mesh = GetOrCreateObject<Mesh>(world, "world://examples/render/triangle_mesh");
        if (!shader || !material || !mesh)
        {
            Log::Error("渲染示例创建资源失败");
            return;
        }

        shader->name = "BasicForward";
        shader->vertexSource =
            "#version 430 core\n"
            "layout(location = 0) in vec3 a_Position;\n"
            "layout(location = 1) in vec3 a_Normal;\n"
            "layout(location = 2) in vec2 a_TexCoord;\n"
            "layout(location = 3) in vec3 a_Tangent;\n"
            "uniform mat4 u_Model;\n"
            "uniform mat4 u_ViewProjection;\n"
            "out vec2 v_TexCoord;\n"
            "void main()\n"
            "{\n"
            "    v_TexCoord = a_TexCoord;\n"
            "    gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);\n"
            "}\n";
        shader->fragmentSource =
            "#version 430 core\n"
            "in vec2 v_TexCoord;\n"
            "uniform vec3 u_DiffuseColor;\n"
            "uniform bool u_HasDiffuseTexture;\n"
            "uniform sampler2D u_DiffuseTexture;\n"
            "out vec4 FragColor;\n"
            "void main()\n"
            "{\n"
            "    vec4 color = vec4(u_DiffuseColor, 1.0);\n"
            "    if (u_HasDiffuseTexture)\n"
            "    {\n"
            "        color *= texture(u_DiffuseTexture, v_TexCoord);\n"
            "    }\n"
            "    FragColor = color;\n"
            "}\n";

        material->name = "RenderExampleMaterial";
        material->diffuse = { 0.95f, 0.42f, 0.18f };
        material->shader.Set(shader);

        mesh->name = "RenderExampleTriangle";
        mesh->vertices =
        {
            { -0.75f, -0.55f, 0.0f },
            { 0.75f, -0.55f, 0.0f },
            { 0.0f, 0.75f, 0.0f },
        };
        mesh->texcoords =
        {
            { 0.0f, 0.0f },
            { 1.0f, 0.0f },
            { 0.5f, 1.0f },
        };
        mesh->normals =
        {
            { 0.0f, 0.0f, 1.0f },
            { 0.0f, 0.0f, 1.0f },
            { 0.0f, 0.0f, 1.0f },
        };
        mesh->indices = { 0, 1, 2 };
        mesh->subMeshes.clear();
        SubMesh subMesh;
        subMesh.name = "Triangle";
        subMesh.indexStart = 0;
        subMesh.indexCount = 3;
        subMesh.material.Set(material);
        mesh->subMeshes.push_back(subMesh);

        Ens cameraEns = world.FindEns(StringId("world://examples/render/camera"));
        if (!cameraEns.IsValid())
        {
            cameraEns = world.CreateEnsWithStableId("world://examples/render/camera", "RenderCamera");
        }

        Ens meshEns = world.FindEns(StringId("world://examples/render/mesh"));
        if (!meshEns.IsValid())
        {
            meshEns = world.CreateEnsWithStableId("world://examples/render/mesh", "RenderMesh");
        }

        Camera* camera = cameraEns.IsValid() ? cameraEns.GetComponent<Camera>() : nullptr;
        if (!camera && cameraEns.IsValid())
        {
            camera = cameraEns.AddComponent<Camera>();
        }

        StaticMeshRenderer* renderer = meshEns.IsValid() ? meshEns.GetComponent<StaticMeshRenderer>() : nullptr;
        if (!renderer && meshEns.IsValid())
        {
            renderer = meshEns.AddComponent<StaticMeshRenderer>();
        }

        if (!camera || !renderer)
        {
            Log::Error("渲染示例创建组件失败");
            return;
        }

        camera->clearColor = { 0.08f, 0.1f, 0.13f, 1.0f };
        camera->fieldOfView = 60.0f;
        renderer->mesh.Set(mesh);

        if (SpaceComponent* meshSpace = meshEns.Space())
        {
            meshSpace->localPosition = { 0.0f, 0.0f, -3.0f };
        }
    }

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
        SpaceComponent* space = ens.Space();
        if (!space)
        {
            Log::Error("反射示例创建 Ens 失败");
            World::SetCurrentWorld(previousWorld);
            return;
        }

        Type* type = space->GetType();
        std::string typeLine = std::string("反射类型: ") + type->GetName();
        Log::Info(typeLine.c_str());

        //遍历当前类型直接声明的字段
        for (const Reflection::FieldInfo& field : type->GetFields())
        {
            std::string line = std::string("\t\t") + "[" + field.typeName + "] " + field.name;
            line += field.persistent ? " persistent=true" : " persistent=false";
            if (field.getter)
            {
                line += " value=" + field.GetValueAsString(space);
            }

            Log::Info(line.c_str());
        }

        //Ens 名称已经是运行时记录，不再作为 SpaceComponent 字段反射
        ens.SetName("新名称");
        std::string nameLine = "Ens 名称写入后: " + ens.GetName();
        Log::Info(nameLine.c_str());

        //通过反射值写入 vector3 字段
        const Reflection::FieldInfo* positionField = type->GetField("localPosition");
        if (positionField)
        {
            vector3 reflectedPosition;
            reflectedPosition.x = 1.0f;
            reflectedPosition.y = 2.0f;
            reflectedPosition.z = 3.0f;

            if (positionField->SetValue(space, Reflection::Value(reflectedPosition)))
            {
                Reflection::Value value = positionField->GetValue(space);
                std::string line = "localPosition 字段写入后: " + value.ToString();
                Log::Info(line.c_str());
            }
        }

        //通过文本写入 vector3 字段
        const Reflection::FieldInfo* scaleField = type->GetField("localScale");
        if (scaleField && scaleField->SetValueFromString(space, "2 2 2"))
        {
            std::string line = "localScale 字段写入后: " + scaleField->GetValueAsString(space);
            Log::Info(line.c_str());
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
            Reflection::Value value = getEnsIdMethod->Invoke(space, List<Reflection::Value>(), &success);
            if (success)
            {
                std::string line = "GetEnsId 返回: " + value.ToString();
                Log::Info(line.c_str());
            }
        }

        World::SetCurrentWorld(previousWorld);
    }
}
