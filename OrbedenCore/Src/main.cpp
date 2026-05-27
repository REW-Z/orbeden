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


namespace examples
{
	void ExampleReflection();
}

int main()
{
    examples::ExampleReflection();
    return 0;

    Application app;
    app.Initialize();
    app.LoadWorld("Worlds/default.world");
    app.Run();

    Profiler::WriteProfileLog();
    Profiler::Clear();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
