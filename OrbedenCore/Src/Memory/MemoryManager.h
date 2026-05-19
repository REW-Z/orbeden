#pragma once
#include "Defines/types.h"
#include <cstddef>
#include <string>

class HeapAllocator;





class Memory
{
public:
    static HeapAllocator* GetHeapAllocator();
};


/// <summary>
/// 堆内存块
/// </summary>
class Block
{
public:
    Block* prevBlock;
    Block* nextBlock;

    uint32 dataSize;
    uint32 alignment;
    bool isArray;

    char info[16];
};

/// <summary>
/// 堆分配器  
/// </summary>
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

    Block* headBlock;
    Block* tailBlock;
    static Block* lastInsertBlock;

    void* Allocate(uint32 size, uint32 alignment = 0, bool isArray = false);
    
    void Deallocate(std::byte* addr, uint32 alignment = 0, bool isArray = false);
    
    
    void Insert(Block* blockPointer);
    
    void Remove(Block* blockPointer);
    
    static void WriteInfo(const char* cstr);
    
    uint32 GetBlockCount();

    void Analysis();

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

