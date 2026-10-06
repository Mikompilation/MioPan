include(FetchContent)

FetchContent_Declare(
        cglm
        GIT_REPOSITORY https://github.com/recp/cglm.git
        GIT_TAG v0.9.6
)
set(CGLM_SHARED OFF)
set(CGLM_STATIC ON)
FetchContent_MakeAvailable(cglm)

# Reconstructed PS2 vectors and matrices are plain float arrays embedded in
# byte-compatible engine structures.  They are not guaranteed to satisfy
# cglm's default 16-byte SSE alignment, so optimized builds must use the
# library's unaligned-safe load/store path.
target_compile_definitions(cglm PUBLIC CGLM_ALL_UNALIGNED)
