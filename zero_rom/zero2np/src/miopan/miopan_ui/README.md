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
backend -- SDL_GPU rendering through `rmlui.vert`/`rmlui.frag`, input (keys,
mouse, and the pad as keys), and the one `Rml::Context`. Each screen is a file
of its own that loads its document and handles its events:

- `miopan_rml_options.cpp` -- the options screen, the game's own options and
  the port's settings; replaces the title's Options and opens from the pause
  screen.
- `miopan_rml_pause.cpp` -- the in-game pause screen: Continue, Options,
  Return to Title.
- `miopan_rml_common.cpp` -- what the screens share: the game's menu sounds,
  button prompts, the yes / no window.

The look they share is `resources/rml/theme.rcss`, taken from the game's own
windows; its images are drawn by `tools/rml_art.py`. RmlUi, FreeType and the
Crimson Text font come from `cmake/rmlui.cmake`, and `resources/rml` is
deployed next to the executable on every build.
