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
# The UI's typeface is Crimson Text, an old-style serif under the SIL Open Font
# License -- the closest free face to the game's own menu lettering.  It is
# downloaded at configure time from a pinned google/fonts commit and checked
# against its SHA-256 rather than committed here; a file already present with
# the right hash is not fetched again.  MIOPAN_RMLUI_FONT_FILES is what
# CMakeLists.txt copies next to the executable, licence included.
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

set(MIOPAN_UI_FONT_URL
    "https://raw.githubusercontent.com/google/fonts/7380f7cebbf2250460c23fd301b51bcbab162edf/ofl/crimsontext")
set(MIOPAN_UI_FONT_DIR "${CMAKE_BINARY_DIR}/fonts")
set(MIOPAN_RMLUI_FONT_FILES)
foreach(font
        "CrimsonText-Regular.ttf|CrimsonText-Regular.ttf|48e6c5d5ad1d01599d374ecb817e15890d1feb3b8a3a88e527d44c90389e1f06"
        "CrimsonText-SemiBold.ttf|CrimsonText-SemiBold.ttf|802e84000740fec2a9fbe0ae09b6b6811bd86a78a0173b15d44450a1530e9410"
        "CrimsonText-Bold.ttf|CrimsonText-Bold.ttf|a3a0765fc5e8d0b49b540a23aefe0184887dd79f06a0bdf4db7035cea6befa93"
        "CrimsonText-Italic.ttf|CrimsonText-Italic.ttf|4ed1699ac7c64e8b3d33f6bb8323c3d7206b0d7bacb9ee2d65c697e6014d29de"
        "OFL.txt|CrimsonText-OFL.txt|50fd67cddc097377a5c871e8452b778bc5aedfa3480a705cb27c5e3a078218df")
    string(REPLACE "|" ";" font_parts "${font}")
    list(GET font_parts 0 font_remote)
    list(GET font_parts 1 font_local)
    list(GET font_parts 2 font_hash)
    file(DOWNLOAD "${MIOPAN_UI_FONT_URL}/${font_remote}"
         "${MIOPAN_UI_FONT_DIR}/${font_local}"
         EXPECTED_HASH SHA256=${font_hash}
         TLS_VERIFY ON)
    list(APPEND MIOPAN_RMLUI_FONT_FILES "${MIOPAN_UI_FONT_DIR}/${font_local}")
endforeach()
