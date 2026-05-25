#include "Profiler.h"

#include "Memory/MemoryManager.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

ProfileSample* Profiler::currentSample = nullptr;
ProfileSample* Profiler::headSample = nullptr;
ProfileSample* Profiler::tailSample = nullptr;

namespace
{
    //判断采样名是否相同
    bool IsSameName(const char* left, const char* right)
    {
        if (left == right) return true;
        if (!left || !right) return false;

        return std::strcmp(left, right) == 0;
    }

    //查找同级采样
    ProfileSample* FindSample(ProfileSample* sample, const char* name)
    {
        while (sample)
        {
            if (IsSameName(sample->name, name)) return sample;

            sample = sample->next;
        }

        return nullptr;
    }

    //追加到采样链表
    void AppendSample(ProfileSample*& head, ProfileSample*& tail, ProfileSample* sample)
    {
        if (tail)
        {
            tail->next = sample;
        }
        else
        {
            head = sample;
        }

        tail = sample;
    }

    //追加缩进
    void AppendIndent(std::string& output, int32 depth)
    {
        for (int32 index = 0; index < depth; index++)
        {
            output += "    ";
        }
    }

    //计算子采样总耗时
    int64 CalculateChildrenTime(ProfileSample* sample)
    {
        int64 timeTotal = 0;
        ProfileSample* child = sample ? sample->firstChild : nullptr;
        while (child)
        {
            timeTotal += child->timeTotalMicroseconds;
            child = child->next;
        }

        return timeTotal;
    }

    //写入采样树
    void WriteSampleLog(std::string& output, ProfileSample* sample, int32 depth)
    {
        while (sample)
        {
            int64 childrenTime = CalculateChildrenTime(sample);
            int64 selfTime = sample->timeTotalMicroseconds - childrenTime;
            if (selfTime < 0) selfTime = 0;

            AppendIndent(output, depth);
            output += sample->name ? sample->name : "Unnamed";
            output += "\n";

            AppendIndent(output, depth);
            output += "---------";
            output += "\n";

            AppendIndent(output, depth);
            output += "timeTotalUs:";
            output += std::to_string(sample->timeTotalMicroseconds);
            output += "\n";

            AppendIndent(output, depth);
            output += "selfTimeUs:";
            output += std::to_string(selfTime);
            output += "\n";

            AppendIndent(output, depth);
            output += "invokeCounts:";
            output += std::to_string(sample->invokeCount);
            output += "\n\n";

            if (sample->firstChild)
            {
                WriteSampleLog(output, sample->firstChild, depth + 1);
            }

            sample = sample->next;
        }
    }

    //删除采样树
    void DeleteSampleTree(ProfileSample* sample)
    {
        while (sample)
        {
            ProfileSample* nextSample = sample->next;
            DeleteSampleTree(sample->firstChild);
            DELETE(sample);
            sample = nextSample;
        }
    }
}

//开始采样
ProfileSample* Profiler::StartSample(const char* name)
{
    //查找当前层级采样
    ProfileSample* parent = currentSample;
    ProfileSample* sample = FindSample(parent ? parent->firstChild : headSample, name);
    if (sample)
    {
        currentSample = sample;
        return sample;
    }

    //创建新采样块
    ProfileSample* newSample = NEW(ProfileSample)ProfileSample();
    newSample->name = name;
    newSample->parent = parent;

    if (parent)
    {
        AppendSample(parent->firstChild, parent->lastChild, newSample);
    }
    else
    {
        AppendSample(headSample, tailSample, newSample);
    }

    currentSample = newSample;
    return newSample;
}

//保存采样
void Profiler::StoreSample(const char*, int64 elapsedMicroseconds, ProfileSample* sample)
{
    if (!sample) return;

    sample->timeTotalMicroseconds += elapsedMicroseconds;
    sample->invokeCount++;

    if (currentSample == sample)
    {
        currentSample = sample->parent;
    }
}

//写入剖析日志
void Profiler::WriteProfileLog(const char* path)
{
    if (!path) return;

    //组合输出文本
    std::string output;
    WriteSampleLog(output, headSample, 0);

    //写入日志文件
    std::filesystem::path filePath(path);
    if (filePath.has_parent_path())
    {
        std::filesystem::create_directories(filePath.parent_path());
    }

    std::ofstream file(path, std::ios::out);
    file.write(output.c_str(), static_cast<std::streamsize>(output.size()));
}

//清空采样数据
void Profiler::Clear()
{
    DeleteSampleTree(headSample);

    currentSample = nullptr;
    headSample = nullptr;
    tailSample = nullptr;
}

//开始作用域采样
AutoProfile::AutoProfile(const char* str)
    : name(str)
{
    sample = Profiler::StartSample(name);
    startTime = std::chrono::steady_clock::now();
}

//结束作用域采样
AutoProfile::~AutoProfile()
{
    std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now();
    int64 elapsedTime = static_cast<int64>(std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count());

    Profiler::StoreSample(name, elapsedTime, sample);
}
