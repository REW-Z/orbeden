cmake_minimum_required(VERSION 3.26)

project(OrbedenPlayer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ORBEDEN_PROJECT_DIR "" CACHE PATH "Game project directory.")
set(ORBEDEN_GAME_AOT_LIB "" CACHE FILEPATH "NativeAOT game link library.")
set(ORBEDEN_GLFW_ROOT "" CACHE PATH "Optional GLFW root for clang/gcc player builds.")
set(ORBEDEN_PLAYER_TARGET "windows-x64" CACHE STRING "Orbeden player target platform.")
set(ORBEDEN_PHYSX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/OrbedenCore/Src/ThirdParty/PhysX" CACHE PATH "Orbeden PhysX CPU-only package root.")

if(NOT ORBEDEN_GAME_AOT_LIB)
    message(FATAL_ERROR "ORBEDEN_GAME_AOT_LIB is required. Build the game C# NativeAOT library first.")
endif()

if(NOT ORBEDEN_PROJECT_DIR)
    message(FATAL_ERROR "ORBEDEN_PROJECT_DIR is required. Build Player from an open Editor project.")
endif()

get_filename_component(ORBEDEN_PROJECT_DIR "${ORBEDEN_PROJECT_DIR}" ABSOLUTE)
if(NOT IS_DIRECTORY "${ORBEDEN_PROJECT_DIR}")
    message(FATAL_ERROR "ORBEDEN_PROJECT_DIR does not exist: ${ORBEDEN_PROJECT_DIR}")
endif()
file(TO_CMAKE_PATH "${ORBEDEN_PROJECT_DIR}" ORBEDEN_PROJECT_DIR)

if(NOT EXISTS "${ORBEDEN_GAME_AOT_LIB}")
    message(FATAL_ERROR "NativeAOT game library was not found: ${ORBEDEN_GAME_AOT_LIB}")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(ORBEDEN_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/OrbedenCore")
set(ORBEDEN_GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/OrbedenGame")

set(ORBEDEN_CORE_SOURCES
    OrbedenCore/Src/Application.cpp
    OrbedenCore/Src/FileSystem/FileSystem.cpp
    OrbedenCore/Src/FileSystem/PathDefines.cpp
    OrbedenCore/Src/Log/Log.cpp
    OrbedenCore/Src/Memory/MemoryManager.cpp
    OrbedenCore/Src/Physics/CharacterControllerComponent.cpp
    OrbedenCore/Src/Physics/ColliderComponent.cpp
    OrbedenCore/Src/Physics/PhysicsReflection.cpp
    OrbedenCore/Src/Physics/PhysicsSystem.cpp
    OrbedenCore/Src/Physics/RigidBodyComponent.cpp
    OrbedenCore/Src/Platform/DynamicLibrary.cpp
    OrbedenCore/Src/Platform/GlfwWindow.cpp
    OrbedenCore/Src/Platform/InputManager.cpp
    OrbedenCore/Src/Rendering/Backend/OpenGLContext.cpp
    OrbedenCore/Src/Rendering/Backend/OpenGLRenderBackend.cpp
    OrbedenCore/Src/Rendering/ForwardPipeline.cpp
    OrbedenCore/Src/Rendering/GpuResourceManager.cpp
    OrbedenCore/Src/Rendering/ImGuiLayer.cpp
    OrbedenCore/Src/Rendering/RenderItemSorter.cpp
    OrbedenCore/Src/Rendering/RenderMath.cpp
    OrbedenCore/Src/Rendering/RenderScene.cpp
    OrbedenCore/Src/Rendering/RenderSystem.cpp
    OrbedenCore/Src/Rendering/SceneCuller.cpp
    OrbedenCore/Src/Rendering/TransformCache.cpp
    OrbedenCore/Src/Scripting/ScriptSystem.cpp
    OrbedenCore/Src/Scripting/ScriptInterop.cpp
    OrbedenCore/Src/Scripting/ScriptBehaviour.cpp
    OrbedenCore/Src/Runtime/AssetPipeline.cpp
    OrbedenCore/Src/Runtime/ComponentStorage.cpp
    OrbedenCore/Src/Runtime/Ens.cpp
    OrbedenCore/Src/Runtime/EnsId.cpp
    OrbedenCore/Src/Runtime/Generated/Reflection.Generated.cpp
    OrbedenCore/Src/Runtime/Gui/RuntimeGuiBridge.cpp
    OrbedenCore/Src/Runtime/Native/OrbedenNativeApi.cpp
    OrbedenCore/Src/Runtime/Native/RuntimeComponentBinds.cpp
    OrbedenCore/Src/Runtime/Native/RuntimeResourceBinds.cpp
    OrbedenCore/Src/Runtime/Object/Camera.cpp
    OrbedenCore/Src/Runtime/Object/DirectionalLight.cpp
    OrbedenCore/Src/Runtime/Object/Material.cpp
    OrbedenCore/Src/Runtime/Object/Mesh.cpp
    OrbedenCore/Src/Runtime/Object/Object.cpp
    OrbedenCore/Src/Runtime/Object/Shader.cpp
    OrbedenCore/Src/Runtime/Object/Skybox.cpp
    OrbedenCore/Src/Runtime/Object/TransformComponent.cpp
    OrbedenCore/Src/Runtime/Object/StaticMeshRenderer.cpp
    OrbedenCore/Src/Runtime/Object/Texture2D.cpp
    OrbedenCore/Src/Runtime/Reflection.cpp
    OrbedenCore/Src/Runtime/ResourceManager.cpp
    OrbedenCore/Src/Runtime/World.cpp
    OrbedenCore/Src/Runtime/WorldSerializer.cpp
    OrbedenCore/Src/Profiler/Profiler.cpp
    OrbedenCore/Src/ThirdParty/glad/src/gl.c
    OrbedenCore/Src/ThirdParty/imgui/backends/imgui_impl_glfw.cpp
    OrbedenCore/Src/ThirdParty/imgui/backends/imgui_impl_opengl3_orbeden.cpp
    OrbedenCore/Src/ThirdParty/imgui/imgui.cpp
    OrbedenCore/Src/ThirdParty/imgui/imgui_draw.cpp
    OrbedenCore/Src/ThirdParty/imgui/imgui_tables.cpp
    OrbedenCore/Src/ThirdParty/imgui/imgui_widgets.cpp
)

set(ORBEDEN_GAME_SOURCES
    OrbedenGame/Src/game_main.cpp
)

#旧项目没有 Native 目录时保持纯 C#；新项目把原生脚本直接静态链接进 Player。
set(ORBEDEN_PROJECT_NATIVE_DIR "${ORBEDEN_PROJECT_DIR}/Native")
if(EXISTS "${ORBEDEN_PROJECT_NATIVE_DIR}/CMakeLists.txt")
    file(GLOB_RECURSE ORBEDEN_PROJECT_NATIVE_SOURCES CONFIGURE_DEPENDS
        "${ORBEDEN_PROJECT_NATIVE_DIR}/*.cpp"
        "${ORBEDEN_PROJECT_NATIVE_DIR}/*.h")
    list(FILTER ORBEDEN_PROJECT_NATIVE_SOURCES EXCLUDE REGEX "[/\\]Build[/\\]")
    list(FILTER ORBEDEN_PROJECT_NATIVE_SOURCES EXCLUDE REGEX "[/\\]GameModule\\.cpp$")

    set(ORBEDEN_PROJECT_NATIVE_GENERATED_DIR "${CMAKE_BINARY_DIR}/Generated/ProjectNative")
    set(ORBEDEN_PROJECT_NATIVE_REFLECTION "${ORBEDEN_PROJECT_NATIVE_GENERATED_DIR}/Reflection.Generated.cpp")
    add_custom_command(
        OUTPUT "${ORBEDEN_PROJECT_NATIVE_REFLECTION}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ORBEDEN_PROJECT_NATIVE_GENERATED_DIR}"
        COMMAND dotnet run --project "${CMAKE_CURRENT_SOURCE_DIR}/Tools/OrbedenMetaGen/OrbedenMetaGen.csproj" --
            "${ORBEDEN_PROJECT_NATIVE_DIR}" "${ORBEDEN_PROJECT_NATIVE_GENERATED_DIR}" --game-module
        DEPENDS ${ORBEDEN_PROJECT_NATIVE_SOURCES} Tools/OrbedenMetaGen/Program.cs
        VERBATIM)
    list(APPEND ORBEDEN_GAME_SOURCES ${ORBEDEN_PROJECT_NATIVE_SOURCES} "${ORBEDEN_PROJECT_NATIVE_REFLECTION}")
    set(ORBEDEN_HAS_NATIVE_GAME TRUE)
endif()

get_filename_component(ORBEDEN_GAME_AOT_DIRECTORY "${ORBEDEN_GAME_AOT_LIB}" DIRECTORY)
set(ORBEDEN_AOT_SHARED FALSE)
if(WIN32)
    get_filename_component(ORBEDEN_GAME_AOT_NAME "${ORBEDEN_GAME_AOT_LIB}" NAME_WE)
    set(ORBEDEN_GAME_AOT_SHARED_LIBRARY "${ORBEDEN_GAME_AOT_DIRECTORY}/${ORBEDEN_GAME_AOT_NAME}.dll")
    if(EXISTS "${ORBEDEN_GAME_AOT_SHARED_LIBRARY}")
        set(ORBEDEN_AOT_SHARED TRUE)
    endif()
endif()

set(ORBEDEN_AOT_NATIVE_FILES)
set(ORBEDEN_AOT_SYSTEM_LIBRARIES)
if(NOT ORBEDEN_AOT_SHARED)
    set(ORBEDEN_AOT_RUNTIME_DIR "${ORBEDEN_GAME_AOT_DIRECTORY}/orbeden-aot-runtime")
    set(ORBEDEN_AOT_NATIVE_FILES_MANIFEST "${ORBEDEN_AOT_RUNTIME_DIR}/native-files.txt")
    set(ORBEDEN_AOT_SYSTEM_LIBRARIES_MANIFEST "${ORBEDEN_AOT_RUNTIME_DIR}/system-libraries.txt")
    if(NOT EXISTS "${ORBEDEN_AOT_NATIVE_FILES_MANIFEST}")
        message(FATAL_ERROR "NativeAOT runtime dependency manifest was not found: ${ORBEDEN_AOT_NATIVE_FILES_MANIFEST}")
    endif()

    file(STRINGS "${ORBEDEN_AOT_NATIVE_FILES_MANIFEST}" ORBEDEN_AOT_NATIVE_FILE_NAMES)
    foreach(_name IN LISTS ORBEDEN_AOT_NATIVE_FILE_NAMES)
        set(_path "${ORBEDEN_AOT_RUNTIME_DIR}/${_name}")
        if(NOT EXISTS "${_path}")
            message(FATAL_ERROR "NativeAOT runtime dependency was not found: ${_path}")
        endif()
        list(APPEND ORBEDEN_AOT_NATIVE_FILES "${_path}")
    endforeach()
    if(EXISTS "${ORBEDEN_AOT_SYSTEM_LIBRARIES_MANIFEST}")
        file(STRINGS "${ORBEDEN_AOT_SYSTEM_LIBRARIES_MANIFEST}" ORBEDEN_AOT_SYSTEM_LIBRARIES)
    endif()
endif()

add_library(orbeden_core STATIC ${ORBEDEN_CORE_SOURCES})
target_include_directories(orbeden_core PUBLIC
    "${ORBEDEN_CORE_DIR}/Src"
    "${ORBEDEN_PHYSX_ROOT}/include"
    "${ORBEDEN_CORE_DIR}/Src/ThirdParty/glfw/include"
    "${ORBEDEN_CORE_DIR}/Src/ThirdParty/glad/include"
    "${ORBEDEN_CORE_DIR}/Src/ThirdParty/imgui"
    "${ORBEDEN_CORE_DIR}/Src/ThirdParty/imgui/backends"
    "${ORBEDEN_CORE_DIR}/Src/ThirdParty/cgltf"
)
target_compile_definitions(orbeden_core PUBLIC PX_PHYSX_STATIC_LIB DISABLE_CUDA_PHYSX)
target_compile_definitions(orbeden_core PRIVATE ORBEDEN_PLAYER)

if(NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "PhysX packages are provided for Debug and Release only; CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif()

set(_orbeden_physx_names
    PhysX_static_64
    PhysXCommon_static_64
    PhysXFoundation_static_64
    PhysXExtensions_static_64
    PhysXPvdSDK_static_64
    PhysXCooking_static_64
    PhysXCharacterKinematic_static_64
    PhysXVehicle_static_64
)

if(WIN32)
    set(_orbeden_physx_lib_dir "${ORBEDEN_PHYSX_ROOT}/lib/WindowsX64/${CMAKE_BUILD_TYPE}")
    foreach(_name IN LISTS _orbeden_physx_names)
        list(APPEND _orbeden_physx_archives "${_orbeden_physx_lib_dir}/${_name}.lib")
    endforeach()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(_orbeden_physx_compiler "Clang")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_orbeden_physx_compiler "GCC")
    else()
        message(FATAL_ERROR "PhysX Linux packages support Clang and GCC only.")
    endif()
    set(_orbeden_physx_lib_dir "${ORBEDEN_PHYSX_ROOT}/lib/LinuxX64/${_orbeden_physx_compiler}/${CMAKE_BUILD_TYPE}")
    foreach(_name IN LISTS _orbeden_physx_names)
        list(APPEND _orbeden_physx_archives "${_orbeden_physx_lib_dir}/lib${_name}.a")
    endforeach()
else()
    message(FATAL_ERROR "PhysX is currently verified only for Windows x64 and Linux x64: ${CMAKE_SYSTEM_NAME}")
endif()

foreach(_archive IN LISTS _orbeden_physx_archives)
    if(NOT EXISTS "${_archive}")
        message(FATAL_ERROR "PhysX CPU-only static library is missing: ${_archive}")
    endif()
endforeach()

if(WIN32)
    target_link_libraries(orbeden_core PUBLIC ${_orbeden_physx_archives})
else()
    target_link_libraries(orbeden_core PUBLIC "$<LINK_GROUP:RESCAN,${_orbeden_physx_archives}>")
endif()

if(MSVC)
    target_compile_options(orbeden_core PRIVATE /utf-8)
else()
    target_compile_options(orbeden_core PRIVATE -Wall -Wextra)
endif()

find_package(OpenGL REQUIRED)

if(WIN32)
    if(ORBEDEN_GLFW_ROOT)
        set(_orbeden_glfw_search_paths "${ORBEDEN_GLFW_ROOT}/lib" "${ORBEDEN_GLFW_ROOT}/lib64")
    else()
        set(_orbeden_glfw_search_paths "${ORBEDEN_CORE_DIR}/Src/ThirdParty/glfw/lib-vc2022")
    endif()

    find_library(ORBEDEN_GLFW_LIBRARY NAMES glfw3_mt glfw3 PATHS ${_orbeden_glfw_search_paths} NO_DEFAULT_PATH)
    if(NOT ORBEDEN_GLFW_LIBRARY)
        message(FATAL_ERROR "GLFW static library was not found. Set ORBEDEN_GLFW_ROOT or provide lib-vc2022/glfw3_mt.lib.")
    endif()

    target_link_libraries(orbeden_core PUBLIC "${ORBEDEN_GLFW_LIBRARY}" OpenGL::GL)
    target_link_libraries(orbeden_core PUBLIC gdi32 user32 shell32 advapi32 ole32 uuid dwmapi winmm imm32 version setupapi ws2_32)
else()
    find_package(Threads REQUIRED)
    find_package(glfw3 CONFIG QUIET)

    if(TARGET glfw)
        target_link_libraries(orbeden_core PUBLIC glfw)
    elseif(TARGET glfw3::glfw)
        target_link_libraries(orbeden_core PUBLIC glfw3::glfw)
    else()
        if(ORBEDEN_GLFW_ROOT)
            find_library(ORBEDEN_GLFW_LIBRARY NAMES glfw glfw3 PATHS "${ORBEDEN_GLFW_ROOT}/lib" "${ORBEDEN_GLFW_ROOT}/lib64" NO_DEFAULT_PATH)
        else()
            find_library(ORBEDEN_GLFW_LIBRARY NAMES glfw glfw3)
        endif()

        if(NOT ORBEDEN_GLFW_LIBRARY)
            message(FATAL_ERROR "GLFW was not found. Install GLFW locally or set ORBEDEN_GLFW_ROOT.")
        endif()

        target_link_libraries(orbeden_core PUBLIC "${ORBEDEN_GLFW_LIBRARY}")
    endif()

    target_link_libraries(orbeden_core PUBLIC OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS} rt)
endif()

add_executable(orbeden_game ${ORBEDEN_GAME_SOURCES})
set_target_properties(orbeden_game PROPERTIES OUTPUT_NAME "OrbedenGame")
target_include_directories(orbeden_game PRIVATE
    "${ORBEDEN_GAME_DIR}/Src"
    "${ORBEDEN_CORE_DIR}/Src"
    "${ORBEDEN_PROJECT_NATIVE_DIR}"
)
if(ORBEDEN_AOT_SHARED)
    target_link_libraries(orbeden_game PRIVATE orbeden_core "${ORBEDEN_GAME_AOT_LIB}")
    add_custom_command(TARGET orbeden_game POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ORBEDEN_GAME_AOT_SHARED_LIBRARY}"
            "$<TARGET_FILE_DIR:orbeden_game>")
elseif(WIN32)
    target_link_libraries(orbeden_game PRIVATE
        orbeden_core
        "${ORBEDEN_GAME_AOT_LIB}"
        ${ORBEDEN_AOT_NATIVE_FILES}
        ${ORBEDEN_AOT_SYSTEM_LIBRARIES})
else()
    target_link_libraries(orbeden_game PRIVATE
        orbeden_core
        "$<LINK_GROUP:RESCAN,${ORBEDEN_GAME_AOT_LIB},${ORBEDEN_AOT_NATIVE_FILES}>"
        ${ORBEDEN_AOT_SYSTEM_LIBRARIES})
endif()
target_compile_definitions(orbeden_game PRIVATE ORBEDEN_PROJECT_DIR="${ORBEDEN_PROJECT_DIR}")
if(ORBEDEN_HAS_NATIVE_GAME)
    target_compile_definitions(orbeden_game PRIVATE ORBEDEN_HAS_NATIVE_GAME)
endif()

if(MSVC)
    target_compile_options(orbeden_game PRIVATE /utf-8)
    if(NOT ORBEDEN_AOT_SHARED)
        target_link_options(orbeden_game PRIVATE
            /INCREMENTAL:NO
            /MERGE:.managedcode=.text
            /MERGE:hydrated=.bss
            /NODEFAULTLIB:libucrt.lib
            /DEFAULTLIB:ucrt.lib)
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(orbeden_game PRIVATE
        -Wl,--export-dynamic
        -Wl,--build-id=sha1
        -Wl,--as-needed
        -pthread)
endif()
