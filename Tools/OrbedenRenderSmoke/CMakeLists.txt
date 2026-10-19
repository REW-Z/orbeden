cmake_minimum_required(VERSION 3.24)
project(OrbedenRenderSmoke LANGUAGES CXX)

get_filename_component(ORBEDEN_ROOT "../.." ABSOLUTE)
set(ORBEDEN_CORE "${ORBEDEN_ROOT}/OrbedenCore/Src")
set(ORBEDEN_SDK "${ORBEDEN_ROOT}/OrbedenEditor/Sdk/Native/WindowsX64")
add_executable(OrbedenRenderSmoke Main.cpp)
target_compile_features(OrbedenRenderSmoke PRIVATE cxx_std_20)
target_compile_options(OrbedenRenderSmoke PRIVATE /utf-8)
target_compile_definitions(OrbedenRenderSmoke PRIVATE GLFW_DLL "IMGUI_API=__declspec(dllimport)")
target_include_directories(OrbedenRenderSmoke PRIVATE "${ORBEDEN_CORE}" "${ORBEDEN_CORE}/ThirdParty/imgui" "${ORBEDEN_CORE}/ThirdParty/glfw/include")
target_link_libraries(OrbedenRenderSmoke PRIVATE "${ORBEDEN_SDK}/$<CONFIG>/OrbedenCore.lib" "${ORBEDEN_CORE}/ThirdParty/glfw/lib/WindowsX64/$<CONFIG>/glfw3dll.lib")
add_library(OrbedenPlayerEntryCheck OBJECT "${ORBEDEN_ROOT}/OrbedenGame/Src/game_main.cpp")
target_compile_features(OrbedenPlayerEntryCheck PRIVATE cxx_std_20)
target_compile_options(OrbedenPlayerEntryCheck PRIVATE /utf-8)
target_compile_definitions(OrbedenPlayerEntryCheck PRIVATE GLFW_DLL)
target_include_directories(OrbedenPlayerEntryCheck PRIVATE "${ORBEDEN_CORE}")
add_custom_command(TARGET OrbedenRenderSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORBEDEN_SDK}/$<CONFIG>/OrbedenCore.dll" "$<TARGET_FILE_DIR:OrbedenRenderSmoke>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORBEDEN_CORE}/ThirdParty/glfw/lib/WindowsX64/$<CONFIG>/glfw3.dll" "$<TARGET_FILE_DIR:OrbedenRenderSmoke>")

enable_testing()
add_test(NAME OpenGLRenderSmoke COMMAND OrbedenRenderSmoke --graphics-api opengl)
add_test(NAME VulkanRenderSmoke COMMAND OrbedenRenderSmoke --graphics-api vulkan)
set_tests_properties(OpenGLRenderSmoke VulkanRenderSmoke PROPERTIES WORKING_DIRECTORY "${ORBEDEN_ROOT}" LABELS "render;gpu;smoke")

option(ORBEDEN_EDITOR_BACKEND_SMOKE "Build the Editor backend switching smoke (requires a built Editor)" OFF)
if(ORBEDEN_EDITOR_BACKEND_SMOKE)
    file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS "${ORBEDEN_ROOT}/OrbedenEditor/Src/Editor/*.cpp")
    add_executable(OrbedenEditorBackendSmoke EditorBackendMain.cpp ${EDITOR_SOURCES})
    target_compile_features(OrbedenEditorBackendSmoke PRIVATE cxx_std_20)
    target_compile_options(OrbedenEditorBackendSmoke PRIVATE /utf-8 /MP)
    target_compile_definitions(OrbedenEditorBackendSmoke PRIVATE ORBEDEN_EDITOR GLFW_DLL "IMGUI_API=__declspec(dllimport)")
    target_include_directories(OrbedenEditorBackendSmoke PRIVATE
        "${ORBEDEN_ROOT}/OrbedenEditor/Src" "${ORBEDEN_CORE}" "${ORBEDEN_CORE}/ThirdParty/imgui"
        "${ORBEDEN_CORE}/ThirdParty/imgui/backends" "${ORBEDEN_CORE}/ThirdParty/glfw/include"
        "${ORBEDEN_ROOT}/OrbedenEditor/Src/ThirdParty/dotnet/include")
    target_link_libraries(OrbedenEditorBackendSmoke PRIVATE
        "${ORBEDEN_SDK}/$<CONFIG>/OrbedenCore.lib" "${ORBEDEN_CORE}/ThirdParty/glfw/lib/WindowsX64/$<CONFIG>/glfw3dll.lib"
        "${ORBEDEN_ROOT}/OrbedenEditor/Src/ThirdParty/dotnet/lib/win-x64/nethost.lib"
        gdi32 user32 shell32 advapi32 ole32 uuid dwmapi dbghelp)
    add_custom_command(TARGET OrbedenEditorBackendSmoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORBEDEN_SDK}/$<CONFIG>/OrbedenCore.dll" "$<TARGET_FILE_DIR:OrbedenEditorBackendSmoke>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORBEDEN_CORE}/ThirdParty/glfw/lib/WindowsX64/$<CONFIG>/glfw3.dll" "$<TARGET_FILE_DIR:OrbedenEditorBackendSmoke>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORBEDEN_ROOT}/OrbedenEditor/Src/ThirdParty/dotnet/lib/win-x64/nethost.dll" "$<TARGET_FILE_DIR:OrbedenEditorBackendSmoke>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORBEDEN_ROOT}/OrbedenEditor/Managed/Orbeden.Editor/OrbedenEditor.runtimeconfig.json" "$<TARGET_FILE_DIR:OrbedenEditorBackendSmoke>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ORBEDEN_ROOT}/OrbedenEditor/x64/$<CONFIG>/Managed" "$<TARGET_FILE_DIR:OrbedenEditorBackendSmoke>/Managed"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ORBEDEN_ROOT}/OrbedenEditor/Resources" "$<TARGET_FILE_DIR:OrbedenEditorBackendSmoke>/Resources")
    add_test(NAME EditorBackendSwitchSmoke COMMAND OrbedenEditorBackendSmoke
        "${ORBEDEN_ROOT}/OrbedenEditor/x64/$<CONFIG>/OrbedenEditor.exe" "${CMAKE_CURRENT_BINARY_DIR}/EditorBackendProject")
    set_tests_properties(EditorBackendSwitchSmoke PROPERTIES WORKING_DIRECTORY "${ORBEDEN_ROOT}" LABELS "render;gpu;smoke;editor")
endif()
