
#include "Defines/types.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include "Log/Log.h"
#include "Memory/MemoryManager.h"

namespace
{
    struct TestObject
    {
        static int destructorCount;

        int value;

        TestObject()
            : value(0)
        {
        }

        TestObject(int input)
            : value(input)
        {
        }

        ~TestObject()
        {
            destructorCount++;
        }
    };

    int TestObject::destructorCount = 0;

    struct alignas(16) AlignedObject
    {
        float data[4];
    };
}

int main()
{
    Log::Info("测试中文");
    Log::Info("aaa");
    Log::Error("222");

    auto defaultObject = NEW(TestObject)TestObject();
    auto valueObject = NEW(TestObject)TestObject(42);
    auto alignedObject = NEW(AlignedObject)AlignedObject();

    assert(valueObject->value == 42);
    assert(reinterpret_cast<std::uintptr_t>(alignedObject) % alignof(AlignedObject) == 0);

    DELETE(defaultObject);
    DELETE(valueObject);
    DELETE(alignedObject);
    DELETE(nullptr);

    assert(defaultObject == nullptr);
    assert(valueObject == nullptr);
    assert(alignedObject == nullptr);
    assert(TestObject::destructorCount == 2);

    Memory::GetHeapAllocator()->Analysis();


    return 0;
}
