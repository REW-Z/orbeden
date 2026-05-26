#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Profiler/Profiler.h"
#include "Application.h"
#include "Runtime/Ens.h"
#include "Runtime/World.h"


int main()
{
    Application app;
    app.Initialize();
    app.LoadWorld("Worlds/default.world");
    app.Run();

    Profiler::WriteProfileLog();
    Profiler::Clear();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
