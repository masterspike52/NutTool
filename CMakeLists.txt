cmake_minimum_required(VERSION 3.25)
project(nuttool LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The GUI needs SDL3, Dear ImGui (SDL3 + OpenGL3 backends) and glad, normally provided by the
# vcpkg manifest (see CMakePresets.json). Without them only the command-line tool is built.
option(NUTTOOL_GUI "Build the nuttool-gui application" ON)
if(NUTTOOL_GUI)
    find_package(SDL3 CONFIG QUIET)
    find_package(imgui CONFIG QUIET)
    find_package(glad CONFIG QUIET)
endif()
set(NUTTOOL_HAVE_GUI_DEPS FALSE)
if(NUTTOOL_GUI AND SDL3_FOUND AND imgui_FOUND AND glad_FOUND)
    set(NUTTOOL_HAVE_GUI_DEPS TRUE)
endif()
# vcpkg's x64-windows-static triplet uses the static MSVC runtime; match it.
if(VCPKG_TARGET_TRIPLET MATCHES "static")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

function(nuttool_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

add_library(nutcore STATIC
    src/common.cpp
    src/json.cpp
    src/deflate.cpp
    src/image.cpp
    src/lzx.cpp
    src/xctd.cpp
    src/caff.cpp
    src/streambundle.cpp
    src/assetdb.cpp
    src/container.cpp
    src/texture.cpp
    src/loctext.cpp
    src/font.cpp
    src/media.cpp
    src/model.cpp
    src/nbpatch.cpp
    src/ops.cpp
)
target_include_directories(nutcore PUBLIC src)
nuttool_warnings(nutcore)

add_executable(nuttool src/main.cpp src/commands.cpp)
target_link_libraries(nuttool PRIVATE nutcore)
nuttool_warnings(nuttool)

if(NUTTOOL_HAVE_GUI_DEPS)
    add_executable(nuttool-gui WIN32
        gui/gui_main.cpp
        gui/app.cpp
        gui/catalog.cpp
        gui/doc_caff.cpp
        gui/doc_text.cpp
        gui/doc_font.cpp
        gui/doc_misc.cpp
        gui/doc_texture.cpp
        gui/doc_model.cpp
        gui/gl_view.cpp
        gui/video_player.cpp
    )
    if(TARGET SDL3::SDL3)
        set(NUTTOOL_SDL SDL3::SDL3)
    else()
        set(NUTTOOL_SDL SDL3::SDL3-static)
    endif()
    target_link_libraries(nuttool-gui PRIVATE nutcore imgui::imgui ${NUTTOOL_SDL} glad::glad)
    if(WIN32)
        # Media Foundation decodes the WMV movies.
        target_link_libraries(nuttool-gui PRIVATE opengl32 mfplat mfreadwrite mfuuid shlwapi ole32)
    endif()
    nuttool_warnings(nuttool-gui)
    message(STATUS "nuttool-gui: enabled")
elseif(NUTTOOL_GUI)
    message(STATUS "nuttool-gui: SDL3/imgui/glad not found (configure with a CMakePresets.json preset), building the CLI only")
endif()
