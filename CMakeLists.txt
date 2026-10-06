# ==========================================================================
#  zero2np — PC port build
#
#  Fatal Frame II (Crimson Butterfly, Feb 6 2004 prototype) reconstruction,
#  retargeted from PS2 EE to the host PC.  There is NO PS2 toolchain: this
#  builds the reconstructed sources with the native host compiler (MSVC / GCC
#  / Clang).
#
#  The target is a host executable. Missing PS2 SDK symbols (sceXxx, libgraph,
#  VU0/GS/DMA, ...) are shimmed for the PC under src/sdk as the port
#  progresses; unreconstructed game-owned entry points live in their original
#  source paths as narrow placeholders until those files are fully reversed.
#
#  Configure + build (from this directory):
#     cmake -S . -B build
#     cmake --build build
# ==========================================================================

cmake_minimum_required(VERSION 3.20)
project(MioPan LANGUAGES CXX)

# The reconstructed .c files use a few post-C99 conveniences (e.g. `auto`);
# ask for the newest C the host compiler offers.  C++ files stay on C++20.
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32 AND NOT MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "-static")
endif()

if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/extern/icon.rc")
endif()

set(BUILD_SHARED_LIBS OFF)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/MioPan)

include(sdl3)
include(spdlog)
include(cglm)
include(imgui)
include(implot)
include(shadercross)
include(ffmpeg)

# ---- sources ------------------------------------------------------------
file(GLOB_RECURSE MIOPAN_TRANSLATION_UNITS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/zero_rom/zero2np/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/zero_rom/zero2np/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/zero_rom/zero2np/src/*.cc"
)
file(GLOB_RECURSE MIOPAN_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/zero_rom/zero2np/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/zero_rom/zero2np/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/zero_rom/zero2np/src/*.inc"
)
set(MIOPAN_SOURCES ${MIOPAN_TRANSLATION_UNITS} ${MIOPAN_HEADERS})
set_source_files_properties(${MIOPAN_HEADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

# ---- include search paths (every directory that holds a header) ---------
set(MIOPAN_INCLUDE_DIRS
    zero_rom/zero2np/src
    zero_rom/zero2np/src/sdk
    zero_rom/zero2np/src/main
    zero_rom/zero2np/src/common
    zero_rom/zero2np/src/outgame
    zero_rom/zero2np/src/debug
    zero_rom/zero2np/src/album/prg
    zero_rom/zero2np/src/save_load/prg
    zero_rom/zero2np/src/system/os
    zero_rom/zero2np/src/system/eeiop
    zero_rom/zero2np/src/system/iop
    zero_rom/zero2np/src/ingame/loading
    zero_rom/zero2np/src/ingame/menu
    zero_rom/zero2np/src/ingame/photo
    zero_rom/zero2np/src/graphics/graph2d
    zero_rom/zero2np/src/graphics/graph3d
    zero_rom/zero2np/src/graphics/graph3d/ctl
    zero_rom/zero2np/src/graphics/movie
)
# NOTE: src/graphics/graph3d/custom is deliberately NOT an include dir — its
# libstdc++-v2 port ships a `stdlib.h` that would shadow the host <stdlib.h>.
# The custom string code reaches those headers via "custom/..." relative paths.

# The reconstruction is effectively C++ (bare `enum` names used as types,
# `auto`, ...), so compile every translation unit — including the .c files —
# as C++ regardless of extension.
set_source_files_properties(${MIOPAN_TRANSLATION_UNITS} PROPERTIES LANGUAGE CXX)

add_executable(MioPan ${MIOPAN_SOURCES} ${APP_ICON_RESOURCE_WINDOWS})
add_dependencies(MioPan compile_shaders)
target_include_directories(MioPan PRIVATE ${MIOPAN_INCLUDE_DIRS})
target_link_libraries(MioPan PRIVATE
    SDL3::SDL3
    spdlog::spdlog
    cglm
    imgui::imgui
    implot::implot
)
#
# The MPEG-2 decoder behind sceMpegGetPicture(); see cmake/ffmpeg.cmake.  When
# the prebuilt tree is absent the whole thing compiles out and movies play
# their audio over a black screen.
if(MIOPAN_HAVE_FFMPEG)
    target_compile_definitions(MioPan PRIVATE MIOPAN_HAVE_FFMPEG=1)
    target_include_directories(MioPan PRIVATE "${FFMPEG_INCLUDE_DIR}")
    target_link_libraries(MioPan PRIVATE avcodec avutil swscale)
    # The import libraries resolve against DLLs that must sit next to the exe.
    if(WIN32)
        add_custom_command(TARGET MioPan POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${MIOPAN_FFMPEG_ROOT}/bin"
                    "$<TARGET_FILE_DIR:MioPan>"
        )
    endif()
endif()

add_custom_command(TARGET MioPan POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:MioPan>/resources"
)

# ---- build stamp for the log banner -------------------------------------
# The first question any bug report has to answer is "which build is this",
# so miopan_log.cpp prints the commit and the configuration in its banner.
#
# Read at CONFIGURE time, not build time: a build-time stamp would need a
# custom command re-run on every build, and the commit is not worth that.
# Re-run cmake (or touch any CMakeLists) after committing to refresh it.
#
# Scoped to the one source file rather than the target.  As a target-wide
# definition every new commit would invalidate all ~1100 translation units;
# this way it rebuilds exactly the file that prints it.
set(MIOPAN_BUILD_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE MIOPAN_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE MIOPAN_GIT_DIRTY
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(MIOPAN_GIT_COMMIT)
        set(MIOPAN_BUILD_COMMIT "${MIOPAN_GIT_COMMIT}")
        if(NOT MIOPAN_GIT_DIRTY STREQUAL "")
            string(APPEND MIOPAN_BUILD_COMMIT "-dirty")
        endif()
    endif()
endif()
message(STATUS "Build stamp: ${MIOPAN_BUILD_COMMIT}")

set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/zero_rom/zero2np/src/miopan/io/miopan_log.cpp"
    PROPERTIES COMPILE_DEFINITIONS
        "MIOPAN_BUILD_COMMIT=\"${MIOPAN_BUILD_COMMIT}\";MIOPAN_BUILD_TYPE=\"${CMAKE_BUILD_TYPE}\""
)

# Force-include the EE base-type shim into every TU so u_char/u_int/u_long
# (64-bit) and NULL are ambient exactly as the PS2 SDK's ubiquitous includes
# made them — without editing each reconstructed source.
set(MIOPAN_PREFIX_HEADER
    "${CMAKE_CURRENT_SOURCE_DIR}/zero_rom/zero2np/src/sdk/pc_prefix.h")
if(MSVC)
    target_compile_options(MioPan PRIVATE "/FI${MIOPAN_PREFIX_HEADER}")
else()
    target_compile_options(MioPan PRIVATE -include "${MIOPAN_PREFIX_HEADER}")
endif()

# ---- aliasing ------------------------------------------------------------
# NOT OPTIONAL, and not a debugging convenience.  The reconstruction reads the
# ROM's file formats in place: SGDSELF32<T>::get() manufactures a T* out of a
# byte buffer, sgdRemap() then writes those same words back through u_int*
# (MappingCoordinateData((u_int *)pPUHead, ...), vuvnprim = (u_int *)pPUHead),
# and DMA packet walks and GetFileInPak() casts do the same everywhere else.
# That is a strict-aliasing violation which GCC and Clang only exploit from
# -O2 up, and the symptom is a release-only SIGSEGV in whichever chain walker
# reads the stale value -- gra3dsgdDrawPresetDataObject re-reading iCategory
# and pNext being the one that shows first.
#
# This lived only in cmake-build-relwithdebinfo's CMakeCache.txt for a long
# time, so every other build directory quietly built without it.  Keep it here
# so it applies to every build type and every fresh configure.
if(NOT MSVC)
    target_compile_options(MioPan PRIVATE -fno-strict-aliasing)
endif()
# MSVC performs no type-based alias analysis unless /Oa is asked for, so it
# needs no equivalent flag.

# ---- buffer-overflow checks in Debug --------------------------------------
# The reconstruction keeps the ROM's own overruns -- a memset sized for one
# struct aimed at a smaller one, strcpy("PS2D") into a four-byte field -- and
# whether a host build notices depends on the toolchain.  MinGW never fortifies
# on its own, so Windows ran over them silently; Ubuntu's GCC enables
# _FORTIFY_SOURCE whenever it optimises, so the Linux release build aborted on
# the first room drawn with "*** buffer overflow detected ***".
#
# Fortifying Debug makes that abort happen at the desk, under a debugger, with
# the overrunning call on the stack.  It needs optimisation to do anything --
# glibc and MinGW both switch it off when __OPTIMIZE__ is unset, and the object
# sizes it checks against come from optimiser passes -- so Debug builds at -Og,
# the level GCC designs for debugging, instead of -O0.  Some locals will read
# <optimized out>; configure with -DMIOPAN_DEBUG_FORTIFY=OFF for -O0 again.
#
# -Og catches overruns visible at the call itself: a member, a local, a static.
# One routed through an out-of-line helper needs the inlining of -O2 and up --
# _ClearMaterialData() only ever sees a void * -- so -D_FORTIFY_SOURCE=3 on a
# RelWithDebInfo build is the wider net.
#
# No extra library is needed: MinGW-w64's CRT supplies __chk_fail and the
# *_chk family.  (Linking libssp swaps in its __chk_fail, a silent trap.)
option(MIOPAN_DEBUG_FORTIFY
       "Build the GCC/Clang Debug config at -Og with _FORTIFY_SOURCE=3" ON)
if(NOT MSVC AND MIOPAN_DEBUG_FORTIFY)
    target_compile_options(MioPan PRIVATE $<$<CONFIG:Debug>:-Og>)
    target_compile_definitions(MioPan PRIVATE $<$<CONFIG:Debug>:_FORTIFY_SOURCE=3>)
endif()

# ---- host-compiler quirks ----------------------------------------------
if(MSVC)
    # PS2 sources are GCC-flavoured C; keep MSVC lenient so the signal is
    # "real" portability problems rather than pedantic conformance noise.
    target_compile_options(MioPan PRIVATE
        /permissive        # accept more GCC-isms
        /W1
        /wd4996            # non-deprecating: POSIX/CRT "unsafe" name warnings
        /D_CRT_SECURE_NO_WARNINGS
        /Zc:preprocessor
    )
else()
    target_compile_options(MioPan PRIVATE -w -fpermissive)
endif()
