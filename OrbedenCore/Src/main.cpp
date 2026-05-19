
#include "Defines/types.h"
#include <iostream>
#include "Log/Log.h"
#include "Memory/MemoryManager.h"

int main()
{
    Log::Info("测试中文");
    Log::Info("aaa");
    Log::Error("222");
    Memory::GetHeapAllocator()->Allocate(10, 10);
    Memory::GetHeapAllocator()->Allocate(20, 20);
    Memory::GetHeapAllocator()->Analysis();


    return 0;
}