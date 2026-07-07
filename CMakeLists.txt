cmake_minimum_required(VERSION 3.26)

project(OrbedenPlayer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ORBEDEN_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ExampleProject" CACHE PATH "Game project directory.")
set(ORBEDEN_GAME_AOT_LIB "" CACHE FILEPATH "NativeAOT static game library.")
set(ORBEDEN_GLFW_ROOT "" CACHE PATH "Optional GLFW root for clang/gcc player builds.")
set(ORBEDEN_PLAYER_TARGET "windows-x64" CACHE STRING "Orbeden player target platform.")

if(NOT ORBEDEN_GAME_AOT_LIB)
    message(FATAL_ERROR "ORBEDEN_GAME_AOT_LIB is required. Build the game C# NativeAOT static library first.")
endif()

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
    OrbedenCore/Src/Rendering/RenderSceneBuilder.cpp
    OrbedenCore/Src/Rendering/RenderSystem.cpp
    OrbedenCore/Src/Rendering/SceneCuller.cpp
    OrbedenCore/Src/Rendering/SpaceCache.cpp
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
    OrbedenCore/Src/Runtime/Object/SpaceComponent.cpp
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
    OrbedenGame/Src/ScriptModule.cpp
)

add_library(orbeden_core STATIC ${ORBEDEN_CORE_SOURCES})
target_include_directories(orbeden_core PUBLIC
    "${ORBEDEN_CORE_DIR}/Src"
    "${ORBEDEN_CORE_DIR}/Src/ThirdParty/glfw/include"
    "${ORBEDEN_CORE_DIR}/Src/ThirdParty/glad/include"
    "${ORBEDEN_CORE_DIR}/Src/ThirdParty/imgui"
    "${ORBEDEN_CORE_DIR}/Src/ThirdParty/imgui/backends"
)

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
    target_link_libraries(orbeden_core PUBLIC gdi32 user32 shell32 advapi32 ole32 uuid dwmapi winmm imm32 version setupapi)
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

    target_link_libraries(orbeden_core PUBLIC OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
endif()

add_executable(orbeden_game ${ORBEDEN_GAME_SOURCES})
set_target_properties(orbeden_game PROPERTIES OUTPUT_NAME "OrbedenGame")
target_include_directories(orbeden_game PRIVATE
    "${ORBEDEN_GAME_DIR}/Src"
    "${ORBEDEN_CORE_DIR}/Src"
)
target_link_libraries(orbeden_game PRIVATE orbeden_core "${ORBEDEN_GAME_AOT_LIB}")

if(MSVC)
    target_compile_options(orbeden_game PRIVATE /utf-8)
endif()
