#pragma once

#include "Defines/types.h"

#include <cstddef>
#include <new>

class HeapAllocator;

//内存系统入口
class Memory
{
public:
    //获取默认堆分配器
    static HeapAllocator* GetHeapAllocator();

    //析构对象并释放堆内存
    template<typename T>
    static void DeleteObject(T*& ptr);

    //允许 DELETE(nullptr)
    static void DeleteObject(decltype(nullptr)) {}
};

//堆内存块
class Block
{
public:
    Block* prevBlock = nullptr;
    Block* nextBlock = nullptr;

    uint32 dataSize = 0;
    uint32 alignment = 0;
    bool isArray = false;

    char info[16] = {};
};

//堆分配器
class HeapAllocator
{
private:
    uint32 allocateCount = 0;
    uint32 deallocateCount = 0;

public:
    enum
    {
        MARK_BEG = 0xAAAAAAAA,
        MARK_END = 0xAAAAAAAA,
    };

    Block* headBlock = nullptr;
    Block* tailBlock = nullptr;
    static Block* lastInsertBlock;

    //堆分配内存
    void* Allocate(uint32 size, uint32 alignment = 0, bool isArray = false);

    //堆释放内存
    void Deallocate(std::byte* addr, uint32 alignment = 0, bool isArray = false);

    //插入内存块节点
    void Insert(Block* blockPointer);

    //移除内存块节点
    void Remove(Block* blockPointer);

    //写入最近一次分配的内存块注释
    static void WriteInfo(const char* cstr);

    //获取当前内存块数量
    uint32 GetBlockCount();

    //输出当前内存分配情况
    void Analysis();

    //输出内存泄露分析
    ~HeapAllocator();
};

class LinearAllocator
{
private:
    std::byte* buffer = nullptr;
    uint32 capacity = 0;
    uint32 offset = 0;
    bool ownsBuffer = false;

    //释放内部缓冲区
    void Release();

public:
    LinearAllocator() = default;
    explicit LinearAllocator(uint32 size);
    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;
    ~LinearAllocator();

    //初始化内部缓冲区
    void Initialize(uint32 size);

    //使用外部缓冲区
    void Initialize(void* memory, uint32 size);

    //线性分配内存
    void* Allocate(uint32 size, uint32 alignment = 0, bool isArray = false);

    //线性分配器不支持单块释放
    void Deallocate(std::byte* addr = nullptr, uint32 alignment = 0, bool isArray = false);

    //重置所有线性分配
    void Reset();

    //获取已使用大小
    uint32 GetUsedSize() const;

    //获取容量
    uint32 GetCapacity() const;
};

typedef void (*ChunkSlotVisitorFunction)(void* address, void* userData);

//连续Chunk固定槽位分配器，支持复用Slot并按顺序遍历存活Slot
class ChunkSlotAllocator
{
private:
    struct FreeSlot
    {
        FreeSlot* next = nullptr;
    };

    std::byte* buffer = nullptr;
    uint8* aliveSlots = nullptr;
    uint32 slotSize = 0;
    uint32 slotStride = 0;
    uint32 slotAlignment = 0;
    uint32 slotCount = 0;
    uint32 aliveCount = 0;
    FreeSlot* freeList = nullptr;

    //释放内部缓冲区
    void Release();

    //获取槽位索引
    uint32 GetSlotIndex(std::byte* address) const;

public:
    ChunkSlotAllocator() = default;
    ChunkSlotAllocator(uint32 size, uint32 count, uint32 alignment = 0);
    ChunkSlotAllocator(const ChunkSlotAllocator&) = delete;
    ChunkSlotAllocator& operator=(const ChunkSlotAllocator&) = delete;
    ~ChunkSlotAllocator();

    //初始化连续Chunk槽位
    void Initialize(uint32 size, uint32 count, uint32 alignment = 0);

    //分配一个槽位
    void* AllocateSlot();

    //释放一个槽位
    void DeallocateSlot(void* address);

    //重置所有槽位
    void Reset();

    //获取槽位数量
    uint32 GetSlotCount() const;

    //获取存活槽位数量
    uint32 GetAliveCount() const;

    //判断地址是否属于当前Chunk
    bool Contains(void* address) const;

    //按槽位顺序遍历存活地址
    void VisitAliveSlots(ChunkSlotVisitorFunction visitor, void* userData) const;
};

class SlabAllocator
{
public:
    //临时使用堆分配，后续再按尺寸分桶
    void* Allocate(uint32 size, uint32 alignment = 0, bool isArray = false);

    //释放临时堆分配
    void Deallocate(std::byte* addr, uint32 alignment = 0, bool isArray = false);
};

//析构对象并释放堆内存
template<typename T>
void Memory::DeleteObject(T*& ptr)
{
    if (!ptr) return;

    ptr->~T();
    Memory::GetHeapAllocator()->Deallocate(reinterpret_cast<std::byte*>(ptr));
    ptr = nullptr;
}

//常用内存操作宏
#define MALLOC(size) Memory::GetHeapAllocator()->Allocate(size)
#define FREE(ptr) Memory::GetHeapAllocator()->Deallocate(reinterpret_cast<std::byte*>(ptr))
#define NEW(T) new (Memory::GetHeapAllocator()->Allocate(sizeof(T), alignof(T), false))
#define DELETE(ptr) Memory::DeleteObject(ptr)

