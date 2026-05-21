#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Profiler/Profiler.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace
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

    Profiler::WriteProfileLog();
    Profiler::Clear();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
