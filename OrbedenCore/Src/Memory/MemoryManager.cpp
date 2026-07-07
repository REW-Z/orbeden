#include "MemoryManager.h"

#include "Log/Log.h"

#include <cassert>
#include <cstdlib>
#include <string>

namespace
{
    //判断是否为2的幂
    bool IsPowerOfTwo(uint32 value)
    {
        return value && ((value & (value - 1)) == 0);
    }

    //获取默认对齐
    uint32 GetDefaultAlignment(uint32 alignment)
    {
        return alignment ? alignment : static_cast<uint32>(alignof(std::max_align_t));
    }

    //向上对齐整数
    uint32 AlignUp(uint32 value, uint32 alignment)
    {
        uint32 mask = alignment - 1;
        return (value + mask) & ~mask;
    }

    //计算地址对齐偏移
    uint32 GetAlignmentAdjustment(std::byte* addr, uint32 alignment)
    {
        uint32 mask = alignment - 1;
        uint32 misAlignment = static_cast<uint32>(reinterpret_cast<uintptr>(addr) & mask);
        return (misAlignment == 0) ? 0 : (alignment - misAlignment);
    }
}

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
        uint32 misAlignment = static_cast<uint32>(reinterpret_cast<uintptr>(tmpAddr) & mask);
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

//创建线性分配器
LinearAllocator::LinearAllocator(uint32 size)
{
    Initialize(size);
}

//销毁线性分配器
LinearAllocator::~LinearAllocator()
{
    Release();
}

//释放内部缓冲区
void LinearAllocator::Release()
{
    if (buffer && ownsBuffer)
    {
        Memory::GetHeapAllocator()->Deallocate(buffer);
    }

    buffer = nullptr;
    capacity = 0;
    offset = 0;
    ownsBuffer = false;
}

//初始化内部缓冲区
void LinearAllocator::Initialize(uint32 size)
{
    Release();
    if (size == 0) return;

    buffer = static_cast<std::byte*>(Memory::GetHeapAllocator()->Allocate(size, static_cast<uint32>(alignof(std::max_align_t))));
    capacity = size;
    offset = 0;
    ownsBuffer = true;
}

//使用外部缓冲区
void LinearAllocator::Initialize(void* memory, uint32 size)
{
    Release();

    buffer = static_cast<std::byte*>(memory);
    capacity = size;
    offset = 0;
    ownsBuffer = false;
}

//线性分配内存
void* LinearAllocator::Allocate(uint32 size, uint32 alignment, bool)
{
    if (!buffer || size == 0) return nullptr;

    uint32 finalAlignment = GetDefaultAlignment(alignment);
    assert(IsPowerOfTwo(finalAlignment));

    uint32 adjustment = GetAlignmentAdjustment(buffer + offset, finalAlignment);
    if (offset + adjustment + size > capacity) return nullptr;

    std::byte* result = buffer + offset + adjustment;
    offset += adjustment + size;
    return result;
}

//线性分配器不支持单块释放
void LinearAllocator::Deallocate(std::byte*, uint32, bool)
{
}

//重置所有线性分配
void LinearAllocator::Reset()
{
    offset = 0;
}

//获取已使用大小
uint32 LinearAllocator::GetUsedSize() const
{
    return offset;
}

//获取容量
uint32 LinearAllocator::GetCapacity() const
{
    return capacity;
}

//创建连续Chunk槽位分配器
ChunkSlotAllocator::ChunkSlotAllocator(uint32 size, uint32 count, uint32 alignment)
{
    Initialize(size, count, alignment);
}

//销毁连续Chunk槽位分配器
ChunkSlotAllocator::~ChunkSlotAllocator()
{
    Release();
}

//释放内部缓冲区
void ChunkSlotAllocator::Release()
{
    if (buffer)
    {
        Memory::GetHeapAllocator()->Deallocate(buffer);
    }

    if (aliveSlots)
    {
        Memory::GetHeapAllocator()->Deallocate(reinterpret_cast<std::byte*>(aliveSlots));
    }

    buffer = nullptr;
    aliveSlots = nullptr;
    slotSize = 0;
    slotStride = 0;
    slotAlignment = 0;
    slotCount = 0;
    aliveCount = 0;
    freeList = nullptr;
}

//获取槽位索引
uint32 ChunkSlotAllocator::GetSlotIndex(std::byte* address) const
{
    assert(buffer);
    assert(address);
    assert(Contains(address));

    uint32 offset = static_cast<uint32>(address - buffer);
    assert(slotStride > 0);
    assert(offset % slotStride == 0);
    return offset / slotStride;
}

//初始化连续Chunk槽位
void ChunkSlotAllocator::Initialize(uint32 size, uint32 count, uint32 alignment)
{
    Release();
    if (size == 0 || count == 0) return;

    uint32 finalAlignment = GetDefaultAlignment(alignment);
    if (finalAlignment < static_cast<uint32>(alignof(FreeSlot)))
    {
        finalAlignment = static_cast<uint32>(alignof(FreeSlot));
    }

    assert(IsPowerOfTwo(finalAlignment));

    uint32 minSlotSize = static_cast<uint32>(sizeof(FreeSlot));
    slotSize = (size > minSlotSize) ? size : minSlotSize;
    slotAlignment = finalAlignment;
    slotStride = AlignUp(slotSize, finalAlignment);
    slotCount = count;

    uint32 totalSize = slotStride * slotCount;
    buffer = static_cast<std::byte*>(Memory::GetHeapAllocator()->Allocate(totalSize, finalAlignment));
    aliveSlots = static_cast<uint8*>(Memory::GetHeapAllocator()->Allocate(slotCount, static_cast<uint32>(alignof(uint8))));

    Reset();
}

//分配一个槽位
void* ChunkSlotAllocator::AllocateSlot()
{
    if (!freeList) return nullptr;

    FreeSlot* slot = freeList;
    freeList = slot->next;

    uint32 slotIndex = GetSlotIndex(reinterpret_cast<std::byte*>(slot));
    assert(aliveSlots[slotIndex] == 0);
    aliveSlots[slotIndex] = 1;
    aliveCount++;
    return slot;
}

//释放一个槽位
void ChunkSlotAllocator::DeallocateSlot(void* address)
{
    if (!address) return;
    assert(buffer);

    std::byte* addr = static_cast<std::byte*>(address);
    uint32 slotIndex = GetSlotIndex(addr);
    assert(aliveSlots[slotIndex] != 0);
    assert(aliveCount > 0);

    aliveSlots[slotIndex] = 0;
    aliveCount--;

    FreeSlot* slot = reinterpret_cast<FreeSlot*>(addr);
    slot->next = freeList;
    freeList = slot;
}

//重置所有槽位
void ChunkSlotAllocator::Reset()
{
    freeList = nullptr;
    aliveCount = 0;

    for (uint32 i = 0; i < slotCount; i++)
    {
        aliveSlots[i] = 0;

        FreeSlot* slot = reinterpret_cast<FreeSlot*>(buffer + slotStride * i);
        slot->next = freeList;
        freeList = slot;
    }
}

//获取槽位数量
uint32 ChunkSlotAllocator::GetSlotCount() const
{
    return slotCount;
}

//获取存活槽位数量
uint32 ChunkSlotAllocator::GetAliveCount() const
{
    return aliveCount;
}

//判断地址是否属于当前Chunk
bool ChunkSlotAllocator::Contains(void* address) const
{
    if (!buffer || !address) return false;

    std::byte* addr = static_cast<std::byte*>(address);
    std::byte* end = buffer + slotStride * slotCount;
    return addr >= buffer && addr < end;
}

//按槽位顺序遍历存活地址
void ChunkSlotAllocator::VisitAliveSlots(ChunkSlotVisitorFunction visitor, void* userData) const
{
    if (!visitor || !buffer || !aliveSlots) return;

    for (uint32 i = 0; i < slotCount; i++)
    {
        if (!aliveSlots[i]) continue;

        visitor(buffer + slotStride * i, userData);
    }
}

//临时使用堆分配，后续再按尺寸分桶
void* SlabAllocator::Allocate(uint32 size, uint32 alignment, bool isArray)
{
    return Memory::GetHeapAllocator()->Allocate(size, alignment, isArray);
}

//释放临时堆分配
void SlabAllocator::Deallocate(std::byte* addr, uint32 alignment, bool isArray)
{
    Memory::GetHeapAllocator()->Deallocate(addr, alignment, isArray);
}
