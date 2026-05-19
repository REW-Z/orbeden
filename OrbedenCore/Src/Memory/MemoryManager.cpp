#include <cassert>
#include <iostream>
#include <string>
#include <cstring>
#include "Defines/types.h"
#include "Log/Log.h"
#include "MemoryManager.h"

using namespace std;

/// ============================================
/// Allocator
/// ============================================

HeapAllocator* Memory::GetHeapAllocator()
{
    static HeapAllocator instance;
    return &instance;
}






/// ============================================
/// HeapAllocator
/// ============================================

Block* HeapAllocator::lastInsertBlock = NULL;

//堆分配内存  
void* HeapAllocator::Allocate(uint32 size, uint32 alignment, bool isArray)
{
    //   | (Block) | BEG(uint) |  adjustment with ajustmentMark  |  allocSize    | END(uint) |

    //申请的总空间 (包含冗余的填充用于字节对齐)  （CRE：按理说alignment改为(alignment-1)也可以 ）
    uint32 totalSize = sizeof(Block) + sizeof(uint32) + (alignment + sizeof(uint32) + size) + sizeof(uint32);

    //(分配内存空间)
    byte* addr = (byte*)malloc(totalSize);//addr ----->  内存块起始

    assert(addr);

    //Block信息 
    Block* blockPtr = (Block*)addr;
    blockPtr->dataSize = size;
    blockPtr->alignment = alignment;
    blockPtr->isArray = isArray;

    for (int i = 0; i < static_cast<int>(sizeof(blockPtr->info) / sizeof(blockPtr->info[0])); i++)
        blockPtr->info[i] = '\0';

    //Block插入
    Insert(blockPtr);
    addr += sizeof(Block);//addr ----->  Block后

    //数据段的前后标记
    uint32* beginMark = (uint32*)(addr);
    *beginMark = HeapAllocator::MARK_BEG;
    addr += sizeof(uint32);//addr ----->  实际分配内存的起始（rawAddr）

    uint32* endMark = (uint32*)(addr + size + alignment + sizeof(uint32));
    *endMark = HeapAllocator::MARK_END;

    //临时地址用于计算偏移 （预留偏移字节数的存放位置）
    byte* tmpAddr = addr + sizeof(uint32);

    //计算对齐的地址
    uint32 byteAdjustment = 0;//0字节对齐时的偏移为0
    if (alignment > 0)
    {
        //***对齐计算***
        uint32 mask = (alignment - 1);
        uint32 misAlignment = ((uint32)tmpAddr & mask);
        unsigned  adjustment = alignment - misAlignment;
        //**************

        byteAdjustment = adjustment;
    }

    byte* alignedAddr = tmpAddr + byteAdjustment;

    //已对齐地址的前面4字节--用于存放偏移字节数
    uint32* adjustmentMark = (uint32*)(alignedAddr - 4);
    *adjustmentMark = byteAdjustment + 4;


    //分配记录
    this->allocateCount++;

    assert(alignedAddr != 0);

    return (void*)alignedAddr;
}


//堆释放内存  
void HeapAllocator::Deallocate(byte* addr, uint32 alignment, bool isArray)
{
    if (!addr) return;


    //取回偏移值 (存放在前面4字节)
    uint32* adjustmentMark = (uint32*)(addr - 4);
    uint32 totalAdjustment = *adjustmentMark;

    //raw Addr位置
    addr -= totalAdjustment;		//addr ---------> raw Addr位置

    //判断
    addr -= sizeof(uint32);  //addr ---------> BEG位置
    uint32* beginMark = (uint32*)(addr);
    assert(*beginMark == HeapAllocator::MARK_BEG);

    addr -= sizeof(Block);		//addr ---------> Block位置
    Block* blockPtr = (Block*)addr;
    assert(blockPtr->alignment == alignment);
    assert(blockPtr->isArray == isArray);

    uint32* endMark = (uint32*)(addr + sizeof(Block) + sizeof(uint32) + alignment + sizeof(uint32) + blockPtr->dataSize);
    assert(*endMark == HeapAllocator::MARK_END);

    //删除节点
    Remove(blockPtr);

    //释放空间
    free(addr);


    //释放次数记录
    this->deallocateCount++;
}



/// 写入内存块注释
void HeapAllocator::WriteInfo(const char* cstr)
{
    Block* lastInsert = HeapAllocator::lastInsertBlock;
    if (lastInsert && cstr)
    {
        uint32 infoLength = static_cast<uint32>(sizeof(lastInsert->info) / sizeof(lastInsert->info[0]));
        uint32 sourceLength = static_cast<uint32>(std::char_traits<char>::length(cstr));
        uint32 copyLength = (sourceLength < (infoLength - 1)) ? sourceLength : (infoLength - 1);

        for (uint32 i = 0; i < copyLength; i++)
        {
            lastInsert->info[i] = cstr[i];
        }

        lastInsert->info[copyLength] = '\0';
    }
}


/// 插入内存块节点
void HeapAllocator::Insert(Block* blockPtr)
{
    //Any Blocks?
    if (tailBlock)
    {
        //Set As Last Node
        blockPtr->prevBlock = tailBlock;
        blockPtr->nextBlock = 0;
        this->tailBlock->nextBlock = blockPtr;
        this->tailBlock = blockPtr;

        //as last insert block
        HeapAllocator::lastInsertBlock = blockPtr;
    }
    //No Blocks
    else
    {
        //New Node As Prev&Tail
        blockPtr->prevBlock = 0;
        blockPtr->nextBlock = 0;
        this->headBlock = blockPtr;
        this->tailBlock = blockPtr;

        //as last insert block
        HeapAllocator::lastInsertBlock = blockPtr;
    }
}

/// 移除内存块节点
void HeapAllocator::Remove(Block* blockPtr)
{
    //有前置节点
    if (blockPtr->prevBlock)
    {
        blockPtr->prevBlock->nextBlock = blockPtr->nextBlock;
    }
    else
    {
        this->headBlock = blockPtr->nextBlock;
    }

    //有后置节点
    if (blockPtr->nextBlock)
    {
        blockPtr->nextBlock->prevBlock = blockPtr->prevBlock;
    }
    else
    {
        this->tailBlock = blockPtr->prevBlock;
    }
}

/// 获取当前内存块数量
uint32 HeapAllocator::GetBlockCount()
{
    uint32 count = 0;
    Block* b = this->headBlock;
    while (b)
    {
        count++;
        b = b->nextBlock;
    }
    return count;
}




/// 当前内存分配情况
void HeapAllocator::Analysis()
{
    Log::Info("分析内存分配情况...");
    Block* b = tailBlock;
    while (b)
    {
        std::string str = "\n[Block]";
        str += "\ninfo:";
        str += b->info;

        str += "\n size: ";
        str += std::to_string(b->dataSize);
        str += "\n";

        Log::Info(str.c_str());

        b = b->prevBlock;
    }
}



/// <summary> 析构函数 </summary> 
HeapAllocator::~HeapAllocator()
{
    Log::Info("分析内存泄露...");

    std::string allocateText = "总分配次数：" + std::to_string(this->allocateCount);
    std::string deallocateText = "总释放次数：" + std::to_string(this->deallocateCount);
    Log::Info(allocateText.c_str());
    Log::Info(deallocateText.c_str());

    this->Analysis();
}