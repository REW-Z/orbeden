#include "MemoryManager.h"

#include "Log/Log.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>

//获取默认堆分配器
HeapAllocator* Memory::GetHeapAllocator()
{
    static HeapAllocator instance;
    return &instance;
}

Block* HeapAllocator::lastInsertBlock = nullptr;

//堆分配内存
void* HeapAllocator::Allocate(uint32 size, uint32 alignment, bool isArray)
{
    //   | (Block) | BEG(uint) |  adjustment with ajustmentMark  |  allocSize    | END(uint) |

    //申请完整内存块
    uint32 totalSize = sizeof(Block) + sizeof(uint32) + (alignment + sizeof(uint32) + size) + sizeof(uint32);
    std::byte* blockAddr = static_cast<std::byte*>(std::malloc(totalSize));
    assert(blockAddr);

    //构造并写入块信息
    Block* blockPtr = new (blockAddr) Block;
    blockPtr->dataSize = size;
    blockPtr->alignment = alignment;
    blockPtr->isArray = isArray;

    //插入内存块链表
    Insert(blockPtr);

    //写入数据段头尾标记
    std::byte* beginMarkAddr = blockAddr + sizeof(Block);
    uint32* beginMark = reinterpret_cast<uint32*>(beginMarkAddr);
    *beginMark = HeapAllocator::MARK_BEG;

    std::byte* rawAddr = beginMarkAddr + sizeof(uint32);
    uint32* endMark = reinterpret_cast<uint32*>(rawAddr + size + alignment + sizeof(uint32));
    *endMark = HeapAllocator::MARK_END;

    //计算对齐后的数据地址
    std::byte* tmpAddr = rawAddr + sizeof(uint32);
    uint32 byteAdjustment = 0;
    if (alignment > 0)
    {
        uint32 mask = (alignment - 1);
        uint32 misAlignment = static_cast<uint32>(reinterpret_cast<std::uintptr_t>(tmpAddr) & mask);
        byteAdjustment = (misAlignment == 0) ? 0 : (alignment - misAlignment);
    }

    std::byte* alignedAddr = tmpAddr + byteAdjustment;

    //记录释放时回退到原始地址所需的偏移
    uint32* adjustmentMark = reinterpret_cast<uint32*>(alignedAddr - sizeof(uint32));
    *adjustmentMark = byteAdjustment + static_cast<uint32>(sizeof(uint32));

    //更新分配记录
    allocateCount++;

    assert(alignedAddr != 0);
    return alignedAddr;
}

//堆释放内存
void HeapAllocator::Deallocate(std::byte* addr, uint32, bool)
{
    if (!addr) return;

    //取回偏移值
    uint32* adjustmentMark = reinterpret_cast<uint32*>(addr - sizeof(uint32));
    uint32 totalAdjustment = *adjustmentMark;
    std::byte* rawAddr = addr - totalAdjustment;

    //检查头标记
    std::byte* beginMarkAddr = rawAddr - sizeof(uint32);
    uint32* beginMark = reinterpret_cast<uint32*>(beginMarkAddr);
    assert(*beginMark == HeapAllocator::MARK_BEG);

    //读取内存块信息
    std::byte* blockAddr = beginMarkAddr - sizeof(Block);
    Block* blockPtr = reinterpret_cast<Block*>(blockAddr);
    uint32 alignment = blockPtr->alignment;

    //检查尾标记
    uint32* endMark = reinterpret_cast<uint32*>(blockAddr + sizeof(Block) + sizeof(uint32) + alignment + sizeof(uint32) + blockPtr->dataSize);
    assert(*endMark == HeapAllocator::MARK_END);

    //释放内存块
    Remove(blockPtr);
    blockPtr->~Block();
    std::free(blockAddr);

    deallocateCount++;
}

//写入最近一次分配的内存块注释
void HeapAllocator::WriteInfo(const char* cstr)
{
    Block* lastInsert = HeapAllocator::lastInsertBlock;
    if (!lastInsert || !cstr) return;

    //截断并写入说明
    uint32 infoLength = static_cast<uint32>(sizeof(lastInsert->info) / sizeof(lastInsert->info[0]));
    uint32 sourceLength = static_cast<uint32>(std::char_traits<char>::length(cstr));
    uint32 copyLength = (sourceLength < (infoLength - 1)) ? sourceLength : (infoLength - 1);

    for (uint32 i = 0; i < copyLength; i++)
    {
        lastInsert->info[i] = cstr[i];
    }

    lastInsert->info[copyLength] = '\0';
}

//插入内存块节点
void HeapAllocator::Insert(Block* blockPtr)
{
    assert(blockPtr);

    //插入链表尾部
    blockPtr->prevBlock = tailBlock;
    blockPtr->nextBlock = nullptr;

    if (tailBlock)
    {
        tailBlock->nextBlock = blockPtr;
    }
    else
    {
        headBlock = blockPtr;
    }

    tailBlock = blockPtr;
    HeapAllocator::lastInsertBlock = blockPtr;
}

//移除内存块节点
void HeapAllocator::Remove(Block* blockPtr)
{
    assert(blockPtr);

    //连接前后节点
    if (blockPtr->prevBlock)
    {
        blockPtr->prevBlock->nextBlock = blockPtr->nextBlock;
    }
    else
    {
        headBlock = blockPtr->nextBlock;
    }

    if (blockPtr->nextBlock)
    {
        blockPtr->nextBlock->prevBlock = blockPtr->prevBlock;
    }
    else
    {
        tailBlock = blockPtr->prevBlock;
    }

    blockPtr->prevBlock = nullptr;
    blockPtr->nextBlock = nullptr;
}

//获取当前内存块数量
uint32 HeapAllocator::GetBlockCount()
{
    uint32 count = 0;
    Block* block = headBlock;

    while (block)
    {
        count++;
        block = block->nextBlock;
    }

    return count;
}

//输出当前内存分配情况
void HeapAllocator::Analysis()
{
    Log::Info("分析内存分配情况...");

    //从尾部向前输出分配块
    Block* block = tailBlock;
    while (block)
    {
        std::string str = "\n[Block]";
        str += "\ninfo:";
        str += block->info;

        str += "\n size: ";
        str += std::to_string(block->dataSize);
        str += "\n";

        Log::Info(str.c_str());

        block = block->prevBlock;
    }
}

//输出内存泄露分析
HeapAllocator::~HeapAllocator()
{
    Log::Info("分析内存泄露...");

    //输出分配统计
    std::string allocateText = "总分配次数：" + std::to_string(allocateCount);
    std::string deallocateText = "总释放次数：" + std::to_string(deallocateCount);
    Log::Info(allocateText.c_str());
    Log::Info(deallocateText.c_str());

    Analysis();
}
