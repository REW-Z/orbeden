#include "FileSystem.h"

#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"

#include <filesystem>
#include <future>
#include <fstream>
#include <iterator>
#include <limits>

namespace
{
    //设置异步结果状态
    void SetAsyncState(AsyncFileResult* result, File* filePtr, bool complete, bool success, float progress)
    {
        if (!result) return;

        result->filePtr = filePtr;
        result->isComplete = complete;
        result->isCompelete = complete;
        result->success = success;
        result->progress = progress;
    }

    //输出文件读取失败日志
    void LogFileError(const char* action, const std::string& path)
    {
        std::string text = action;
        text += ": ";
        text += path;
        Log::Error(text.c_str());
    }
}

//构造路径
Path::Path(std::string newFullName)
    : fullName(newFullName)
{
}

//构造路径
Path::Path(const char* newFullName)
    : fullName(newFullName ? newFullName : "")
{
}

//获取扩展名
std::string Path::GetExtension()
{
    return Path::GetExtension(fullName);
}

//获取文件名
std::string Path::GetName()
{
    return Path::GetName(fullName);
}

//获取无扩展名文件名
std::string Path::GetNameWithOutExtension()
{
    return Path::GetNameWithOutExtension(fullName);
}

//获取目录
std::string Path::GetDirectory()
{
    return Path::GetDirectory(fullName);
}

//是否有扩展名
bool Path::HasExtension()
{
    return Path::HasExtension(fullName);
}

//获取扩展名
std::string Path::GetExtension(std::string fullName)
{
    return Utf8Path::ToUtf8(Utf8Path::FromUtf8(fullName).extension());
}

//获取文件名
std::string Path::GetName(std::string fullName)
{
    return Utf8Path::ToUtf8(Utf8Path::FromUtf8(fullName).filename());
}

//获取无扩展名完整路径
std::string Path::GetFullNameWithOutExtension(std::string fullName)
{
    std::filesystem::path path = Utf8Path::FromUtf8(fullName);
    path.replace_extension();
    return Utf8Path::ToUtf8(path);
}

//获取无扩展名文件名
std::string Path::GetNameWithOutExtension(std::string fullName)
{
    return Utf8Path::ToUtf8(Utf8Path::FromUtf8(fullName).stem());
}

//获取目录
std::string Path::GetDirectory(std::string fullName)
{
    return Utf8Path::ToUtf8(Utf8Path::FromUtf8(fullName).parent_path());
}

//是否有扩展名
bool Path::HasExtension(std::string fullName)
{
    return Utf8Path::FromUtf8(fullName).has_extension();
}

//是否为二进制文件
bool File::IsBinary()
{
    return isBinary;
}

//构造二进制文件
FileBinary::FileBinary()
{
    isBinary = true;
}

//释放二进制缓冲
FileBinary::~FileBinary()
{
    FREE(buffer);
    buffer = nullptr;
    length = 0;
}

//构造文本文件
FileText::FileText()
{
    isBinary = false;
}

AsyncFileRoutine::AsyncFileRoutine(std::coroutine_handle<promise_type> newHandle)
    : handle(newHandle)
{
}

AsyncFileRoutine::AsyncFileRoutine(AsyncFileRoutine&& other) noexcept
    : handle(other.handle)
{
    other.handle = {};
}

AsyncFileRoutine::~AsyncFileRoutine()
{
    if (handle) handle.destroy();
}

//恢复到下一个 yield
bool AsyncFileRoutine::MoveNext()
{
    if (!handle || handle.done()) return false;

    handle.resume();
    return !handle.done();
}

//获取当前 yield 的异步结果
AsyncFileResult* AsyncFileRoutine::Current()
{
    return handle.promise().yielded;
}

//等待当前 yield 的异步结果
void AsyncFileRoutine::WaitCurrent()
{
    AsyncFileResult* result = Current();
    if (result && result->task.valid())
    {
        result->task.wait();
    }
}

//文件是否存在
bool FileSystem::Exist(std::string path)
{
    return std::filesystem::exists(Utf8Path::FromUtf8(path));
}

//读取二进制文件
void FileSystem::LoadFileBinary(std::string path, FileBinary* filePtr, AsyncFileResult* result, IOSLIB ioslibFlag)
{
    if (!filePtr)
    {
        SetAsyncState(result, nullptr, true, false, 1.0f);
        return;
    }

    //初始化输出对象
    filePtr->filePath = Path(path);
    FREE(filePtr->buffer);
    filePtr->buffer = nullptr;
    filePtr->length = 0;
    SetAsyncState(result, filePtr, false, false, 0.0f);

    //检查支持模式
    if (ioslibFlag == IOSLIB::OS)
    {
        LogFileError("FileSystem OS read is not supported", path);
        SetAsyncState(result, filePtr, true, false, 1.0f);
        return;
    }

    //打开文件
    std::ifstream file(Utf8Path::FromUtf8(path), std::ios::binary | std::ios::ate);
    if (!file)
    {
        LogFileError("Load binary file failed", path);
        SetAsyncState(result, filePtr, true, false, 1.0f);
        return;
    }

    //读取文件内容
    std::streamoff length = file.tellg();
    if (length <= 0)
    {
        filePtr->length = 0;
        SetAsyncState(result, filePtr, true, true, 1.0f);
        return;
    }

    if (length > std::numeric_limits<int>::max())
    {
        LogFileError("Binary file is too large", path);
        SetAsyncState(result, filePtr, true, false, 1.0f);
        return;
    }

    file.seekg(0, std::ios::beg);
    filePtr->length = static_cast<int>(length);
    filePtr->buffer = MALLOC(static_cast<uint32>(length));

    if (!filePtr->buffer || !file.read(reinterpret_cast<char*>(filePtr->buffer), length))
    {
        LogFileError("Read binary file failed", path);
        FREE(filePtr->buffer);
        filePtr->buffer = nullptr;
        filePtr->length = 0;
        SetAsyncState(result, filePtr, true, false, 1.0f);
        return;
    }

    SetAsyncState(result, filePtr, true, true, 1.0f);
}

//读取文本文件
void FileSystem::LoadFileText(std::string path, FileText* filePtr, AsyncFileResult* result, IOSLIB ioslibFlag)
{
    if (!filePtr)
    {
        SetAsyncState(result, nullptr, true, false, 1.0f);
        return;
    }

    //初始化输出对象
    filePtr->filePath = Path(path);
    filePtr->content.clear();
    SetAsyncState(result, filePtr, false, false, 0.0f);

    //检查支持模式
    if (ioslibFlag == IOSLIB::OS)
    {
        LogFileError("FileSystem OS read is not supported", path);
        SetAsyncState(result, filePtr, true, false, 1.0f);
        return;
    }

    //读取文本内容
    std::ifstream file(Utf8Path::FromUtf8(path), std::ios::in);
    if (!file)
    {
        LogFileError("Load text file failed", path);
        SetAsyncState(result, filePtr, true, false, 1.0f);
        return;
    }

    filePtr->content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    SetAsyncState(result, filePtr, true, true, 1.0f);
}

//同步读取文件
File* FileSystem::LoadFile(std::string path, IOSLIB ioslibFlag, MODE mode)
{
    if (mode == MODE::BINARY)
    {
        FileBinary* filePtr = NEW(FileBinary)FileBinary();
        LoadFileBinary(path, filePtr, nullptr, ioslibFlag);
        return filePtr;
    }

    FileText* filePtr = NEW(FileText)FileText();
    LoadFileText(path, filePtr, nullptr, ioslibFlag);
    return filePtr;
}

//异步读取文件
AsyncFileResult* FileSystem::LoadFileAsync(std::string path, IOSLIB ioslibFlag, MODE mode)
{
    AsyncFileResult* result = NEW(AsyncFileResult)AsyncFileResult();

    if (mode == MODE::BINARY)
    {
        FileBinary* filePtr = NEW(FileBinary)FileBinary();
        SetAsyncState(result, filePtr, false, false, 0.0f);

        result->task = std::async(std::launch::async, FileSystem::LoadFileBinary, path, filePtr, result, ioslibFlag);
        return result;
    }

    FileText* filePtr = NEW(FileText)FileText();
    SetAsyncState(result, filePtr, false, false, 0.0f);

    result->task = std::async(std::launch::async, FileSystem::LoadFileText, path, filePtr, result, ioslibFlag);
    return result;
}

//读取文本内容
std::string FileSystem::LoadText(std::string path)
{
    FileText file;
    LoadFileText(path, &file);
    return file.content;
}

//写入对象二进制数据
void FileSystem::WriteObject(std::string path, void* objPtr, usize size)
{
    if (!objPtr || size == 0) return;

    //创建目录并写入文件
    std::filesystem::path filePath = Utf8Path::FromUtf8(path);
    if (filePath.has_parent_path())
    {
        std::filesystem::create_directories(filePath.parent_path());
    }

    std::ofstream file(filePath, std::ios::binary | std::ios::out);
    if (!file)
    {
        LogFileError("Write object failed", path);
        return;
    }

    file.write(reinterpret_cast<const char*>(objPtr), static_cast<std::streamsize>(size));
}

//读取对象二进制数据
void FileSystem::ReadObject(std::string path, void* objPtr, usize size)
{
    if (!objPtr || size == 0) return;

    //读取对象数据
    std::ifstream file(Utf8Path::FromUtf8(path), std::ios::binary | std::ios::in);
    if (!file)
    {
        LogFileError("Read object failed", path);
        return;
    }

    file.read(reinterpret_cast<char*>(objPtr), static_cast<std::streamsize>(size));
}
