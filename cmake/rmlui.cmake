# ---------------------------------------------------------------------------
# RmlUi, the HTML/CSS-style UI library the port's own menus are built in, and
# FreeType, its font engine.  Both are fetched and built from source, the same
# pins MikuPan uses.
#
# FreeType comes first and is handed to RmlUi as Freetype::Freetype: RmlUi's
# find_package(Freetype) is not REQUIRED, and it only checks that the target
# exists, so a host without a system FreeType still builds -- and one with it
# does not mix the two.  Every optional FreeType codec is off; RmlUi hands it
# plain TrueType/OpenType files and nothing compressed.
#
# The UI's typeface ships out of the RmlUi checkout rather than being committed
# here: Lato, under the SIL Open Font License, with the licence file beside it.
# MIOPAN_RMLUI_FONT_FILES is what CMakeLists.txt copies next to the executable.
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-3
)

set(FT_DISABLE_ZLIB ON CACHE BOOL "Disable zlib for FreeType." FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "Disable bzip2 for FreeType." FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "Disable png for FreeType." FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "Disable harfbuzz for FreeType." FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "Disable brotli for FreeType." FORCE)

FetchContent_MakeAvailable(freetype)

if(TARGET freetype AND NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "Font engine used by RmlUi." FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "Disable RmlUi samples." FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "Disable RmlUi Lua bindings." FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "Disable RmlUi Lottie plugin." FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "Disable RmlUi SVG plugin." FORCE)
set(RMLUI_HARFBUZZ_SAMPLE OFF CACHE BOOL "Disable RmlUi HarfBuzz sample." FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "Disable RmlUi precompiled headers." FORCE)

FetchContent_Declare(
    rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG 6.2
)

FetchContent_MakeAvailable(rmlui)

set(MIOPAN_RMLUI_FONT_FILES
    "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf"
    "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Bold.ttf"
    "${rmlui_SOURCE_DIR}/Samples/assets/LICENSE.txt"
)
