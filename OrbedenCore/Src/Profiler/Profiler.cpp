#include "Profiler.h"

#include "FileSystem/Utf8Path.h"
#include "Memory/MemoryManager.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

ProfileSample* Profiler::currentSample = nullptr;
ProfileSample* Profiler::headSample = nullptr;
ProfileSample* Profiler::tailSample = nullptr;

namespace
{
    //保留的帧数：走势图与单帧明细共用同一段历史，看得见的帧都点得开
    constexpr int32 FrameWindowCapacity = 1024;

    //每帧事件上限，超出只丢事件并计数，分类耗时不受影响
    constexpr int32 FrameEventCapacity = 128;

    //名字驻留表容量
    constexpr int32 NameCapacity = 512;
    constexpr int32 NameTextCapacity = 64;

    //名字驻留项
    struct NameEntry
    {
        char text[NameTextCapacity] = {};
        int32 length = 0;
        int32 category = 0;
    };

    //名字驻留表。采样树里的名字指针一律指向这里，且只追加不清空：
    //采样名通常是游戏模块的字符串字面量，模块热重载卸载后原指针会失效。
    struct NameTable
    {
        NameEntry entries[NameCapacity];
        int32 count = 0;
    };

    //按帧采集状态
    struct CaptureState
    {
        bool capturing = false;
        ProfileFrameSummary* frames = nullptr;
        ProfileEvent* events = nullptr;

        int64 nextFrameIndex = 0;
        int64 currentFrameIndex = -1;
        int32 currentFrameSlot = -1;
        int32 eventCount = 0;

        std::chrono::steady_clock::time_point frameStartTime;
        std::thread::id mainThreadId;
        bool mainThreadBound = false;
    };

    //获取名字驻留表
    NameTable& GetNameTable()
    {
        static NameTable table;
        return table;
    }

    //获取按帧采集状态
    CaptureState& GetCaptureState()
    {
        static CaptureState state;
        return state;
    }

    //获取下一个采样节点编号
    int32& GetNextNodeId()
    {
        static int32 nextNodeId = 0;
        return nextNodeId;
    }

    //判断采样名前缀
    bool HasPrefix(const char* name, const char* prefix)
    {
        return std::strncmp(name, prefix, std::strlen(prefix)) == 0;
    }

    //按采样名前缀推导分类
    ProfileCategory ClassifySample(const char* name)
    {
        if (!name) return ProfileCategory::Other;

        if (HasPrefix(name, "Script/")) return ProfileCategory::Script;
        if (HasPrefix(name, "Render/")) return ProfileCategory::Render;
        if (HasPrefix(name, "Physics/")) return ProfileCategory::Physics;
        if (HasPrefix(name, "FileIO/")) return ProfileCategory::FileIO;
        if (HasPrefix(name, "Editor/")) return ProfileCategory::Editor;

        return ProfileCategory::Other;
    }

    //读取采样名的分类
    int32 GetNameCategory(int32 nameId)
    {
        NameTable& table = GetNameTable();
        if (nameId < 0 || nameId >= table.count) return static_cast<int32>(ProfileCategory::Other);

        return table.entries[nameId].category;
    }

    //驻留采样名，返回表内拷贝；表满时退回原指针
    const char* InternName(const char* name, int32& nameId)
    {
        nameId = -1;
        if (!name) return nullptr;

        NameTable& table = GetNameTable();
        for (int32 index = 0; index < table.count; index++)
        {
            if (std::strcmp(table.entries[index].text, name) == 0)
            {
                nameId = index;
                return table.entries[index].text;
            }
        }

        if (table.count >= NameCapacity) return name;

        NameEntry& entry = table.entries[table.count];
        usize length = std::strlen(name);
        if (length > NameTextCapacity - 1)
        {
            length = NameTextCapacity - 1;
        }

        std::memcpy(entry.text, name, length);
        entry.text[length] = '\0';
        entry.length = static_cast<int32>(length);
        entry.category = static_cast<int32>(ClassifySample(name));

        nameId = table.count;
        table.count++;
        return entry.text;
    }

    //记录本次调用的事件
    void BeginEvent(ProfileSample* sample)
    {
        sample->frameEventIndex = -1;
        sample->frameEventSlot = -1;

        CaptureState& state = GetCaptureState();
        if (!state.capturing || !state.events || state.currentFrameSlot < 0) return;

        //按帧采集只跟随主线程，工作线程上的采样直接丢弃
        if (state.mainThreadBound && state.mainThreadId != std::this_thread::get_id()) return;

        sample->frameEventSlot = state.currentFrameSlot;

        //池子写满时只丢事件，分类耗时仍在 StoreSample 里照常累加
        if (state.eventCount >= FrameEventCapacity)
        {
            state.frames[state.currentFrameSlot].droppedEventCount++;
            return;
        }

        int32 index = state.currentFrameSlot * FrameEventCapacity + state.eventCount;
        state.eventCount++;
        state.frames[state.currentFrameSlot].eventCount = state.eventCount;

        ProfileEvent& profileEvent = state.events[index];
        profileEvent.nameId = sample->nameId;
        profileEvent.nodeId = sample->nodeId;
        profileEvent.parentNodeId = sample->parent ? sample->parent->nodeId : -1;
        profileEvent.category = GetNameCategory(sample->nameId);
        profileEvent.depth = sample->depth;
        profileEvent.startMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - state.frameStartTime).count();
        profileEvent.durationMicroseconds = 0;

        sample->frameEventIndex = index;
    }

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

static_assert(sizeof(ProfileEvent) == 40);
static_assert(sizeof(ProfileFrameSummary) == 88);

//应用关闭时写出并清空采样数据
void Profiler::OnShutdown()
{
    //先把采集缓冲还掉，再写日志：缓冲是裸数组，不进引擎分配器的泄漏统计
    ReleaseCapture();
    WriteProfileLog();
    Clear();
}

//开始采样
ProfileSample* Profiler::StartSample(const char* name)
{
    //查找当前层级采样
    ProfileSample* parent = currentSample;
    ProfileSample* sample = FindSample(parent ? parent->firstChild : headSample, name);
    if (!sample)
    {
        //创建新采样块
        sample = NEW(ProfileSample)ProfileSample();

        int32 nameId = -1;
        sample->name = InternName(name, nameId);
        sample->nameId = nameId;
        sample->nodeId = GetNextNodeId()++;
        sample->parent = parent;
        sample->depth = parent ? parent->depth + 1 : 0;

        if (parent)
        {
            AppendSample(parent->firstChild, parent->lastChild, sample);
        }
        else
        {
            AppendSample(headSample, tailSample, sample);
        }
    }

    currentSample = sample;
    BeginEvent(sample);
    return sample;
}

//保存采样
void Profiler::StoreSample(const char*, int64 elapsedMicroseconds, ProfileSample* sample)
{
    if (!sample) return;

    sample->timeTotalMicroseconds += elapsedMicroseconds;
    sample->invokeCount++;

    //补齐本帧事件并累加分类耗时
    CaptureState& state = GetCaptureState();
    if (state.capturing && state.currentFrameSlot >= 0
        && sample->frameEventSlot >= 0 && sample->frameEventSlot == state.currentFrameSlot)
    {
        if (sample->frameEventIndex >= 0 && state.events)
        {
            state.events[sample->frameEventIndex].durationMicroseconds = elapsedMicroseconds;
        }

        //分类耗时只统计帧的顶层阶段，避免嵌套重复计入
        if (sample->depth == 0)
        {
            ProfileFrameSummary& summary = state.frames[state.currentFrameSlot];
            summary.rootMicroseconds += elapsedMicroseconds;

            int32 category = GetNameCategory(sample->nameId);
            if (category >= 0 && category < static_cast<int32>(ProfileCategory::Capacity))
            {
                summary.categoryMicroseconds[category] += elapsedMicroseconds;
            }
        }

        sample->frameEventIndex = -1;
        sample->frameEventSlot = -1;
    }

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
    std::filesystem::path filePath = Utf8Path::FromUtf8(path);
    if (filePath.has_parent_path())
    {
        std::filesystem::create_directories(filePath.parent_path());
    }

    std::ofstream file(filePath, std::ios::out);
    file.write(output.c_str(), static_cast<std::streamsize>(output.size()));
}

//清空采样数据与帧历史
void Profiler::Clear()
{
    DeleteSampleTree(headSample);

    currentSample = nullptr;
    headSample = nullptr;
    tailSample = nullptr;

    GetNextNodeId() = 0;
    ClearFrames();
}

//开始新的一帧
void Profiler::NewFrame()
{
    CaptureState& state = GetCaptureState();
    if (!state.capturing || !state.frames) return;

    //按帧采集只跟随主线程
    std::thread::id threadId = std::this_thread::get_id();
    if (!state.mainThreadBound)
    {
        state.mainThreadId = threadId;
        state.mainThreadBound = true;
    }
    else if (state.mainThreadId != threadId)
    {
        return;
    }

    //切换到下一帧
    int64 frameIndex = state.nextFrameIndex++;
    int32 slot = static_cast<int32>(frameIndex % FrameWindowCapacity);

    state.frames[slot] = ProfileFrameSummary();
    state.frames[slot].frameIndex = frameIndex;

    state.currentFrameIndex = frameIndex;
    state.currentFrameSlot = slot;
    state.eventCount = 0;
    state.frameStartTime = std::chrono::steady_clock::now();
}

//结束当前帧，记录这一帧自身的耗时
void Profiler::EndFrame()
{
    CaptureState& state = GetCaptureState();
    if (!state.capturing || state.currentFrameSlot < 0) return;

    //用本帧自己的跨度，不借用外部传入的帧间隔：
    //空闲唤醒的帧拿到的是 0，会把有耗时的帧写成 0ms
    state.frames[state.currentFrameSlot].deltaTimeMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - state.frameStartTime).count();
}

//累加当前帧的限帧等待时间
void Profiler::AddFrameWait(int64 microseconds)
{
    CaptureState& state = GetCaptureState();
    if (!state.capturing || state.currentFrameSlot < 0 || microseconds <= 0) return;

    state.frames[state.currentFrameSlot].waitMicroseconds += microseconds;
}

//开关按帧采集，停止时保留已采集的数据供查看
void Profiler::SetCapturing(bool value)
{
    CaptureState& state = GetCaptureState();
    if (state.capturing == value) return;

    if (value)
    {
        //采集缓冲用裸数组分配，避开引擎分配器的泄漏统计
        if (!state.frames)
        {
            state.frames = new ProfileFrameSummary[FrameWindowCapacity];
        }
        if (!state.events)
        {
            state.events = new ProfileEvent[FrameWindowCapacity * FrameEventCapacity];
        }

        state.mainThreadBound = false;
        state.capturing = true;
    }
    else
    {
        state.capturing = false;
    }

    //丢掉进行中的帧：恢复采集时不能把停采期间的墙钟时间算到上一帧头上
    state.currentFrameIndex = -1;
    state.currentFrameSlot = -1;
    state.eventCount = 0;
}

//释放采集缓冲
void Profiler::ReleaseCapture()
{
    CaptureState& state = GetCaptureState();

    state.capturing = false;
    state.currentFrameIndex = -1;
    state.currentFrameSlot = -1;
    state.eventCount = 0;
    state.mainThreadBound = false;
    state.nextFrameIndex = 0;

    delete[] state.frames;
    delete[] state.events;
    state.frames = nullptr;
    state.events = nullptr;
}

//判断是否正在按帧采集
bool Profiler::IsCapturing()
{
    return GetCaptureState().capturing;
}

//清空帧历史
void Profiler::ClearFrames()
{
    CaptureState& state = GetCaptureState();
    state.nextFrameIndex = 0;
    state.currentFrameIndex = -1;
    state.currentFrameSlot = -1;
    state.eventCount = 0;
}

//读取帧历史的帧序号范围
int32 Profiler::GetFrameRange(int64& oldestFrame, int64& newestFrame)
{
    CaptureState& state = GetCaptureState();
    if (!state.frames || state.nextFrameIndex <= 0)
    {
        oldestFrame = 0;
        newestFrame = -1;
        return 0;
    }

    int64 retained = state.nextFrameIndex < FrameWindowCapacity ? state.nextFrameIndex : FrameWindowCapacity;
    oldestFrame = state.nextFrameIndex - retained;
    newestFrame = state.nextFrameIndex - 1;
    return static_cast<int32>(retained);
}

//拷贝帧摘要
int32 Profiler::CopyFrameSummaries(ProfileFrameSummary* output, int32 capacity)
{
    CaptureState& state = GetCaptureState();
    if (!output || !state.frames || capacity <= 0) return 0;

    int64 oldestFrame = 0;
    int64 newestFrame = -1;
    int32 retained = GetFrameRange(oldestFrame, newestFrame);
    if (retained <= 0) return 0;

    int32 written = 0;
    for (int64 frameIndex = oldestFrame; frameIndex <= newestFrame && written < capacity; frameIndex++)
    {
        output[written] = state.frames[frameIndex % FrameWindowCapacity];
        written++;
    }

    return written;
}

//拷贝指定帧的采样事件
int32 Profiler::CopyFrameEvents(int64 frameIndex, ProfileEvent* output, int32 capacity)
{
    CaptureState& state = GetCaptureState();
    if (!output || !state.frames || !state.events || capacity <= 0) return 0;

    int64 oldestFrame = 0;
    int64 newestFrame = -1;
    if (GetFrameRange(oldestFrame, newestFrame) <= 0) return 0;
    if (frameIndex < oldestFrame || frameIndex > newestFrame) return 0;

    int32 slot = static_cast<int32>(frameIndex % FrameWindowCapacity);
    const ProfileFrameSummary& summary = state.frames[slot];
    int32 count = summary.eventCount < capacity ? summary.eventCount : capacity;

    int32 firstEvent = slot * FrameEventCapacity;
    for (int32 index = 0; index < count; index++)
    {
        output[index] = state.events[firstEvent + index];
    }

    return count;
}

//读取名字驻留表的条数
int32 Profiler::GetNameCount()
{
    return GetNameTable().count;
}

//按编号拷贝采样名
int32 Profiler::CopyName(int32 nameId, char* text, int32 capacity)
{
    NameTable& table = GetNameTable();
    if (nameId < 0 || nameId >= table.count) return -1;

    const NameEntry& entry = table.entries[nameId];
    if (text != nullptr && capacity > 0)
    {
        int32 written = entry.length < capacity - 1 ? entry.length : capacity - 1;
        std::memcpy(text, entry.text, static_cast<usize>(written));
        text[written] = '\0';
    }

    return entry.length;
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
