#pragma once

#include "Application.h"
#include "Defines/types.h"

#include <atomic>
#include <coroutine>
#include <exception>
#include <future>
#include <string>

//文件路径工具
class Path
{
public:
    std::string fullName;

public:
    Path() = default;
    Path(std::string newFullName);
    Path(const char* newFullName);

    //获取扩展名
    std::string GetExtension();

    //获取文件名
    std::string GetName();

    //获取无扩展名文件名
    std::string GetNameWithOutExtension();

    //获取目录
    std::string GetDirectory();

    //是否有扩展名
    bool HasExtension();

public:
    //获取扩展名
    static std::string GetExtension(std::string fullName);

    //获取文件名
    static std::string GetName(std::string fullName);

    //获取无扩展名完整路径
    static std::string GetFullNameWithOutExtension(std::string fullName);

    //获取无扩展名文件名
    static std::string GetNameWithOutExtension(std::string fullName);

    //获取目录
    static std::string GetDirectory(std::string fullName);

    //是否有扩展名
    static bool HasExtension(std::string fullName);
};

//文件基类
class File
{
public:
    Path filePath;

protected:
    bool isBinary = false;

public:
    virtual ~File() = default;

    //是否为二进制文件
    bool IsBinary();
};

//二进制文件
class FileBinary : public File
{
public:
    void* buffer = nullptr;
    int length = 0;

public:
    FileBinary();
    virtual ~FileBinary();
};

//文本文件
class FileText : public File
{
public:
    std::string content;

public:
    FileText();
};

//异步文件结果
class AsyncFileResult
{
public:
    std::atomic_bool isComplete = false;
    std::atomic_bool isCompelete = false;
    std::atomic_bool success = false;
    std::atomic<float> progress = 0.0f;

    File* filePtr = nullptr;
    std::future<void> task;
};

//异步文件协程
class AsyncFileRoutine
{
public:
    struct promise_type
    {
        AsyncFileResult* yielded = nullptr;

        AsyncFileRoutine get_return_object()
        {
            return AsyncFileRoutine(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(AsyncFileResult* result) noexcept
        {
            yielded = result;
            return {};
        }

        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    AsyncFileRoutine(const AsyncFileRoutine&) = delete;
    AsyncFileRoutine& operator=(const AsyncFileRoutine&) = delete;

    AsyncFileRoutine(AsyncFileRoutine&& other) noexcept;
    ~AsyncFileRoutine();

    //恢复到下一个 yield
    bool MoveNext();

    //获取当前 yield 的异步结果
    AsyncFileResult* Current();

    //等待当前 yield 的异步结果
    void WaitCurrent();

private:
    explicit AsyncFileRoutine(std::coroutine_handle<promise_type> newHandle);

    std::coroutine_handle<promise_type> handle;
};

//文件系统
class FileSystem : public IEngineSystem
{
public:
    enum class IOSLIB
    {
        StandardC = 0,
        Fstream = 1,
        OS = 2,
    };

    enum class MODE
    {
        NORMAL = 1,
        BINARY = 2,
    };

    //文件是否存在
    static bool Exist(std::string path);

    //读取二进制文件
    static void LoadFileBinary(std::string path, FileBinary* filePtr, AsyncFileResult* result = nullptr, IOSLIB ioslibFlag = IOSLIB::StandardC);

    //读取文本文件
    static void LoadFileText(std::string path, FileText* filePtr, AsyncFileResult* result = nullptr, IOSLIB ioslibFlag = IOSLIB::Fstream);

    //同步读取文件
    static File* LoadFile(std::string path, IOSLIB ioslibFlag = IOSLIB::StandardC, MODE mode = MODE::BINARY);

    //异步读取文件
    static AsyncFileResult* LoadFileAsync(std::string path, IOSLIB ioslibFlag = IOSLIB::StandardC, MODE mode = MODE::BINARY);

    //读取文本内容
    static std::string LoadText(std::string path);

    //写入对象二进制数据
    static void WriteObject(std::string path, void* objPtr, usize size);

    //读取对象二进制数据
    static void ReadObject(std::string path, void* objPtr, usize size);
};
