# MioPan UI

MioPan's Dear ImGui C++ integration and application UI code belongs in this
directory. The Dear ImGui core and SDL3/SDL_GPU backends are provided by the
`imgui::imgui` CMake target.

The renderer owns the bootstrap lifecycle. Add application panels to
`MioPanUi::Draw()`; the initial panel mirrors the renderer statistics sampled for
the SDL window title.
