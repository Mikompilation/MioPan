# FFmpeg -- the MPEG-2 video decoder behind sceMpegGetPicture().
#
# The PSS movies on the disc are MPEG-2 (sequence_extension on every sequence
# header, picture_coding_extension on every picture, full IPB GOPs), so an
# MPEG-1 decoder cannot be used.  Only three libraries are needed:
#
#   avcodec   the decoder itself
#   avutil    its frame/buffer types
#   swscale   YUV420P -> RGBA, which is the format the IPU produced
#
# avformat and swresample are deliberately absent: sceMpegDemuxPss() in
# sdk/libmpeg.cpp is a full PSS demultiplexer already, so avcodec is fed raw
# elementary-stream bytes, and the audio is plain PCM that needs no resampling.
#
# Layout follows MikuPan's: a prebuilt tree under extern/ffmpeg with include/,
# lib/ and bin/.  Unlike MikuPan's this module does NOT hard-fail when the tree
# is absent -- it reports MIOPAN_HAVE_FFMPEG OFF and the build continues with
# video decoding compiled out, so the project still builds for anyone who has
# not fetched the binaries.

set(MIOPAN_FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/extern/ffmpeg"
        CACHE PATH "Root of the prebuilt FFmpeg tree (include/, lib/, bin/).")

set(MIOPAN_FFMPEG_LIBS avcodec avutil swscale)
set(MIOPAN_HAVE_FFMPEG OFF)

if(NOT EXISTS "${MIOPAN_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    message(STATUS
            "FFmpeg not found at ${MIOPAN_FFMPEG_ROOT} -- building without a "
            "video decoder.  Movies will play their audio over a black screen.")
    return()
endif()

set(FFMPEG_INCLUDE_DIR "${MIOPAN_FFMPEG_ROOT}/include")

# Pick the import library flavour that matches the toolchain.  MinGW can
# usually link an MSVC .lib for a C DLL, but its own .dll.a is the path that
# always works, so prefer it when both are shipped.
function(_miopan_ffmpeg_implib out_var lib)
    set(candidates)
    if(WIN32)
        if(MSVC)
            list(APPEND candidates
                    "${MIOPAN_FFMPEG_ROOT}/lib/${lib}.lib"
                    "${MIOPAN_FFMPEG_ROOT}/lib/lib${lib}.dll.a")
        else()
            list(APPEND candidates
                    "${MIOPAN_FFMPEG_ROOT}/lib/lib${lib}.dll.a"
                    "${MIOPAN_FFMPEG_ROOT}/lib/${lib}.lib")
        endif()
    else()
        list(APPEND candidates
                "${MIOPAN_FFMPEG_ROOT}/lib/lib${lib}.so"
                "${MIOPAN_FFMPEG_ROOT}/lib/lib${lib}.a")
    endif()

    foreach(candidate IN LISTS candidates)
        if(EXISTS "${candidate}")
            set(${out_var} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

foreach(ffmpeg_lib IN LISTS MIOPAN_FFMPEG_LIBS)
    _miopan_ffmpeg_implib(ffmpeg_implib "${ffmpeg_lib}")
    if(NOT ffmpeg_implib)
        message(STATUS
                "FFmpeg tree at ${MIOPAN_FFMPEG_ROOT} has headers but no import "
                "library for ${ffmpeg_lib} -- building without a video decoder.")
        return()
    endif()

    add_library(${ffmpeg_lib} SHARED IMPORTED)
    if(WIN32)
        set_target_properties(${ffmpeg_lib} PROPERTIES
                IMPORTED_IMPLIB "${ffmpeg_implib}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}")
    else()
        set_target_properties(${ffmpeg_lib} PROPERTIES
                IMPORTED_LOCATION "${ffmpeg_implib}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}")
    endif()
endforeach()

set(MIOPAN_HAVE_FFMPEG ON)
message(STATUS "FFmpeg found at ${MIOPAN_FFMPEG_ROOT} -- video decoding enabled.")
