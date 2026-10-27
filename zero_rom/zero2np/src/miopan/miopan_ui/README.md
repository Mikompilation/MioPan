# MioPan UI

MioPan's Dear ImGui C++ integration and application UI code belongs in this
directory. The Dear ImGui core and SDL3/SDL_GPU backends are provided by the
`imgui::imgui` CMake target.

The renderer owns the bootstrap lifecycle. Add application panels to
`MioPanUi::Draw()`; the initial panel mirrors the renderer statistics sampled for
the SDL window title.

## RmlUi

The player-facing menus are RmlUi documents (`resources/rml/*.rml` and
`.rcss`), separate from the ImGui developer overlay. `miopan_rmlui.cpp` is the
backend -- SDL_GPU rendering through `rmlui.vert`/`rmlui.frag`, input, and the
one `Rml::Context` -- and each screen gets a `miopan_rml_*.cpp` of its own that
loads its document and handles its events (`miopan_rml_hello.cpp` is the first).
RmlUi and FreeType come from `cmake/rmlui.cmake`.
