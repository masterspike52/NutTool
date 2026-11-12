cmake_minimum_required(VERSION 3.25)
project(nuttool LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "NutTool builds on Windows only")
endif()

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
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/W4;/permissive-;/utf-8;/EHsc>")
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:-Wall;-Wextra>")
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
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
    src/gamescript.cpp
    src/media.cpp
    src/model.cpp
    src/animkeys.cpp
    src/modelimport.cpp
    src/nbpatch.cpp
    src/objparams.cpp
    src/renutpart.cpp
    src/renutmodel.cpp
    src/renutvehicle.cpp
    src/renutmake.cpp
    src/renutchars.cpp
    src/renutworld.cpp
    src/renutnpc.cpp
    src/gltfobj.cpp
    src/collada.cpp
    src/blockmodel.cpp
    src/npcmodel.cpp
    src/scriptgraph.cpp
    src/scriptgraph_nodes.cpp
    src/ops.cpp
    src/xui.cpp
    src/commands.cpp
    src/script.cpp
    src/fbx.cpp
    src/audio.cpp
)
target_include_directories(nutcore PUBLIC src)
nuttool_warnings(nutcore)

# Lua scripting (vcpkg port "lua" with feature "cpp": Lua compiled as C++, so errors unwind).
find_package(unofficial-lua CONFIG QUIET)
if(TARGET unofficial::lua::lua-cpp)
    target_link_libraries(nutcore PUBLIC unofficial::lua::lua-cpp)
    target_compile_definitions(nutcore PRIVATE NUTTOOL_LUA=1)
else()
    message(STATUS "Lua not found: scripting disabled")
endif()

# stb_image (vcpkg port "stb") reads JPG, BMP, TGA and the PNGs nutcore's own decoder doesn't.
find_path(NUTTOOL_STB_INCLUDE stb_image.h)
if(NUTTOOL_STB_INCLUDE)
    target_include_directories(nutcore PRIVATE ${NUTTOOL_STB_INCLUDE})
    target_compile_definitions(nutcore PRIVATE NUTTOOL_STB=1)
else()
    message(STATUS "stb not found: images other than PNG and DDS cannot be imported")
endif()

# FFmpeg decodes the XMA audio. vcpkg's FindFFMPEG looks the Windows SDK libraries up with
# find_library, which fails outside a Visual Studio prompt, so link vcpkg's static libraries
# directly and the SDK ones by name (from the .pc files' Libs lines).
set(NUTTOOL_HAVE_FFMPEG FALSE)
find_path(NUTTOOL_FFMPEG_INCLUDE libavcodec/avcodec.h)
find_library(NUTTOOL_AVFORMAT avformat)
find_library(NUTTOOL_AVCODEC avcodec)
find_library(NUTTOOL_SWRESAMPLE swresample)
find_library(NUTTOOL_AVUTIL avutil)
if(NUTTOOL_FFMPEG_INCLUDE AND NUTTOOL_AVFORMAT AND NUTTOOL_AVCODEC AND NUTTOOL_SWRESAMPLE AND NUTTOOL_AVUTIL)
    target_include_directories(nutcore PUBLIC ${NUTTOOL_FFMPEG_INCLUDE})
    target_link_libraries(nutcore PUBLIC ${NUTTOOL_AVFORMAT} ${NUTTOOL_AVCODEC} ${NUTTOOL_SWRESAMPLE} ${NUTTOOL_AVUTIL}
                          mfuuid ole32 strmiids user32 secur32 ncrypt crypt32 ws2_32 bcrypt)
    set(NUTTOOL_HAVE_FFMPEG TRUE)
endif()
if(NUTTOOL_HAVE_FFMPEG)
    target_compile_definitions(nutcore PRIVATE NUTTOOL_FFMPEG=1)
else()
    message(STATUS "FFmpeg not found: XMA audio cannot be decoded")
endif()

# The program icon: icon/nuttool.png made into an .ico while building, in both executables' resources.
add_executable(nuttool-make-icon tools/make_icon.cpp)
target_link_libraries(nuttool-make-icon PRIVATE nutcore)
nuttool_warnings(nuttool-make-icon)
set(NUTTOOL_ICON_PNG ${CMAKE_CURRENT_SOURCE_DIR}/icon/nuttool.png)
set(NUTTOOL_ICO ${CMAKE_CURRENT_BINARY_DIR}/nuttool.ico)
add_custom_command(OUTPUT ${NUTTOOL_ICO}
    COMMAND nuttool-make-icon ${NUTTOOL_ICON_PNG} ${NUTTOOL_ICO}
    DEPENDS nuttool-make-icon ${NUTTOOL_ICON_PNG}
    COMMENT "Making nuttool.ico"
    VERBATIM)
configure_file(gui/nuttool.rc.in ${CMAKE_CURRENT_BINARY_DIR}/nuttool.rc @ONLY)
set(NUTTOOL_RC ${CMAKE_CURRENT_BINARY_DIR}/nuttool.rc)
set_source_files_properties(${NUTTOOL_RC} PROPERTIES OBJECT_DEPENDS ${NUTTOOL_ICO})

add_executable(nuttool src/main.cpp ${NUTTOOL_RC})
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
        gui/doc_script.cpp
        gui/doc_texture.cpp
        gui/texture_edit.cpp
        gui/doc_model.cpp
        gui/script_window.cpp
        gui/script_graph.cpp
        gui/tools.cpp
        gui/doc_audio.cpp
        gui/audio_view.cpp
        gui/gl_view.cpp
        gui/video_player.cpp
        ${NUTTOOL_RC}
    )
    if(TARGET SDL3::SDL3)
        set(NUTTOOL_SDL SDL3::SDL3)
    else()
        set(NUTTOOL_SDL SDL3::SDL3-static)
    endif()
    target_link_libraries(nuttool-gui PRIVATE nutcore imgui::imgui ${NUTTOOL_SDL} glad::glad)
    # The script graph editor's canvas (vcpkg port "imgui-node-editor").
    find_package(unofficial-imgui-node-editor CONFIG QUIET)
    if(TARGET unofficial::imgui-node-editor::imgui-node-editor)
        target_link_libraries(nuttool-gui PRIVATE unofficial::imgui-node-editor::imgui-node-editor)
        target_compile_definitions(nuttool-gui PRIVATE NUTTOOL_NODE_EDITOR=1)
    else()
        message(STATUS "imgui-node-editor not found: the script graph editor is disabled")
    endif()
    # Media Foundation decodes the WMV movies.
    target_sources(nuttool-gui PRIVATE gui/video_player_mf.cpp)
    target_link_libraries(nuttool-gui PRIVATE opengl32 mfplat mfreadwrite mfuuid shlwapi ole32)
    nuttool_warnings(nuttool-gui)
    message(STATUS "nuttool-gui: enabled")
elseif(NUTTOOL_GUI)
    message(STATUS "nuttool-gui: SDL3/imgui/glad not found (configure with a CMakePresets.json preset), building the CLI only")
endif()
