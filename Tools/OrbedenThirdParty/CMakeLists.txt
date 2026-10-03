# Orbeden 第三方库生成器。
#
# 本项目只产出静态库，供主工程以库依赖方式使用；主工程永远不编译这里的源码。
# 由 OrbedenThirdParty.vcxproj 驱动，日常开发不需要手动运行。
# 新增第三方库时在这里 add_subdirectory，并把产物路径登记到工程文件的输出项里。

cmake_minimum_required(VERSION 3.20)
project(OrbedenThirdParty C CXX)

# 一律静态库；不做安装、不产出可执行文件。
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)
# 所有产物落在同一目录，主工程按固定路径取用。
set(ORBEDEN_THIRDPARTY_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/Build/lib" CACHE PATH "" FORCE)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${ORBEDEN_THIRDPARTY_OUTPUT}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${ORBEDEN_THIRDPARTY_OUTPUT}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ORBEDEN_THIRDPARTY_OUTPUT}")

# ---- FreeType ----
# 关掉全部外部依赖：只保留自带模块，不引入 zlib/bzip2/libpng/harfbuzz/brotli。
set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
set(FT_ENABLE_ERROR_STRINGS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/freetype freetype)

# ---- msdfgen ----
# core-only：不建 standalone、不接 Skia、不开 OpenMP、不产 SVG/PNG 辅助功能。
set(MSDFGEN_CORE_ONLY ON CACHE BOOL "" FORCE)
set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_VCPKG OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_SKIA OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_OPENMP OFF CACHE BOOL "" FORCE)
set(MSDFGEN_INSTALL OFF CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_SVG ON CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(MSDFGEN_USE_CPP11 OFF CACHE BOOL "" FORCE)
# 运行时库必须与引擎一致（/MDd 与 /MD）；msdfgen 默认静态运行时，这里显式打开动态运行时。
set(MSDFGEN_DYNAMIC_RUNTIME ON CACHE BOOL "" FORCE)
add_subdirectory(vendor/msdfgen msdfgen)

# msdfgen 只在开启安装目标时才生成 msdfgen-config.h，而安装目标是我们明确不要的。
# 这里按同一份模板自己生成一份，保证 MSDFGEN_INSTALL 保持关闭。
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/msdfgen/vcpkg.json" MSDFGEN_VCPKG_JSON)
string(REGEX MATCH "\"version\"[ \t\n\r]*:[ \t\n\r]*\"([^\"]*)\"" _ "${MSDFGEN_VCPKG_JSON}")
set(MSDFGEN_VERSION "${CMAKE_MATCH_1}")
string(REGEX REPLACE "^([0-9]*)\\.([0-9]*)\\.([0-9]*)" "\\1" MSDFGEN_VERSION_MAJOR "${MSDFGEN_VERSION}")
string(REGEX REPLACE "^([0-9]*)\\.([0-9]*)\\.([0-9]*)" "\\2" MSDFGEN_VERSION_MINOR "${MSDFGEN_VERSION}")
string(REGEX REPLACE "^([0-9]*)\\.([0-9]*)\\.([0-9]*)" "\\3" MSDFGEN_VERSION_REVISION "${MSDFGEN_VERSION}")
set(MSDFGEN_PUBLIC_MACRO_VALUE "")
set(MSDFGEN_ADDITIONAL_DEFINES "")
string(TIMESTAMP MSDFGEN_COPYRIGHT_YEAR "%Y")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/vendor/msdfgen/cmake/msdfgen-config.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/msdfgen-config.h")

# ---- GLFW ----
# 必须是动态库：GLFW 的窗口表与平台后端是进程内单例，而 OrbedenCore.dll 与编辑器 exe
# 都会直接调用它。静态库会给两者各一份互不相通的状态，编辑器建窗必崩。
set(GLFW_LIBRARY_TYPE SHARED CACHE STRING "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/glfw glfw)

# ---- Dear ImGui ----
# 上游文件原样进 vendor/；imgui_impl_opengl3.cpp 由 src/ 下的 Orbeden 包装 include，
# 以便使用引擎的 GLAD2 而不是 imgui 自带的加载器。
add_library(imgui STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/imgui_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/imgui_tables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/imgui_widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/misc/cpp/imgui_stdlib.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/backends/imgui_impl_glfw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_impl_opengl3_orbeden.cpp")
target_include_directories(imgui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/misc/cpp")
# ImGui 的上下文（GImGui）必须全进程唯一：编辑器 exe 建上下文、OrbedenCore.dll 渲染，
# 各持一份就等于两个互不相干的 UI 世界。这里按 dllexport 编译，由 OrbedenCore.dll 导出、
# 编辑器从导入库取；静态宿主（Player）下 dllexport 只是多一张导出表，无副作用。
target_compile_definitions(imgui PRIVATE "IMGUI_API=__declspec(dllexport)")

# ---- glad ----
# 生成产物直接入库；引擎侧只链接，不编译。
# glad 的 GL 函数指针表同样是全局状态，只允许 OrbedenCore.dll 里初始化过的那一份：
# 这里按 dllexport 编译，编辑器用 GLAD_API_CALL_EXPORT（dllimport）从 DLL 取。
add_library(glad STATIC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/src/gl.c")
target_include_directories(glad PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/include")
target_compile_definitions(glad PRIVATE GLAD_API_CALL_EXPORT GLAD_API_CALL_EXPORT_BUILD)

# imgui 的 GLFW 与 OpenGL3 后端分别要这两家的头文件与库。
target_link_libraries(imgui PUBLIC glfw glad)

# ---- cgltf / stb_image ----
# 单头库：实现放在 src/ 下的编译单元里，引擎只 include 头文件。
add_library(cgltf STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/cgltf_impl.c")
target_include_directories(cgltf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/cgltf")

add_library(stb STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/stb_image_impl.c")
target_include_directories(stb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb")
