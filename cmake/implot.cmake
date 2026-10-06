include(FetchContent)

FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    # ImPlot v1.0.  Pin the release commit so profiler builds stay reproducible.
    GIT_TAG 524f9fcd48d76c13fdf94c5ffbba8787a1ff7e39
)

FetchContent_MakeAvailable(implot)

add_library(implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)
add_library(implot::implot ALIAS implot)

target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui::imgui)
