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
    static void DeleteObject(std::nullptr_t) {}
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
};

class PoolAllocator
{
};

class SlabAllocator
{
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

