cmake_minimum_required(VERSION 3.26)

project(OrbedenNativeScriptModuleTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(ORBEDEN_ENGINE_ROOT "" CACHE PATH "Orbeden repository root")
set(ORBEDEN_CORE_LIB "" CACHE FILEPATH "OrbedenCore import library")
set(GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Generated")
set(GENERATED_REFLECTION "${GENERATED_DIRECTORY}/Reflection.Generated.cpp")

add_custom_command(
    OUTPUT "${GENERATED_REFLECTION}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIRECTORY}"
    COMMAND dotnet run --project "${ORBEDEN_ENGINE_ROOT}/Tools/OrbedenMetaGen/OrbedenMetaGen.csproj" --
        "${CMAKE_CURRENT_SOURCE_DIR}" "${GENERATED_DIRECTORY}" --game-module
    DEPENDS TestNativeBehaviour.h TestNativeBehaviour.cpp GameModule.cpp
    VERBATIM)

add_library(OrbedenNativeScriptModuleTest SHARED
    TestNativeBehaviour.cpp
    GameModule.cpp
    "${GENERATED_REFLECTION}")
target_include_directories(OrbedenNativeScriptModuleTest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${ORBEDEN_ENGINE_ROOT}/OrbedenCore/Src")
target_link_libraries(OrbedenNativeScriptModuleTest PRIVATE "${ORBEDEN_CORE_LIB}")
target_compile_options(OrbedenNativeScriptModuleTest PRIVATE /utf-8)
set_target_properties(OrbedenNativeScriptModuleTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/bin")

add_library(OrbedenNativeScriptDuplicateTest SHARED DuplicateType.cpp)
target_include_directories(OrbedenNativeScriptDuplicateTest PRIVATE "${ORBEDEN_ENGINE_ROOT}/OrbedenCore/Src")
target_link_libraries(OrbedenNativeScriptDuplicateTest PRIVATE "${ORBEDEN_CORE_LIB}")
target_compile_options(OrbedenNativeScriptDuplicateTest PRIVATE /utf-8)
set_target_properties(OrbedenNativeScriptDuplicateTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/bin")

add_executable(OrbedenNativeScriptModuleHost NativeModuleHost.cpp)
target_include_directories(OrbedenNativeScriptModuleHost PRIVATE "${ORBEDEN_ENGINE_ROOT}/OrbedenCore/Src")
target_link_libraries(OrbedenNativeScriptModuleHost PRIVATE "${ORBEDEN_CORE_LIB}")
target_compile_options(OrbedenNativeScriptModuleHost PRIVATE /utf-8)
set_target_properties(OrbedenNativeScriptModuleHost PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/bin")
get_filename_component(ORBEDEN_CORE_LIBRARY_DIRECTORY "${ORBEDEN_CORE_LIB}" DIRECTORY)
add_custom_command(TARGET OrbedenNativeScriptModuleHost POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${ORBEDEN_CORE_LIBRARY_DIRECTORY}/OrbedenCore.dll"
        "$<TARGET_FILE_DIR:OrbedenNativeScriptModuleHost>/OrbedenCore.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${ORBEDEN_ENGINE_ROOT}/OrbedenCore/Src/ThirdParty/glfw/lib-vc2022/glfw3.dll"
        "$<TARGET_FILE_DIR:OrbedenNativeScriptModuleHost>/glfw3.dll")
