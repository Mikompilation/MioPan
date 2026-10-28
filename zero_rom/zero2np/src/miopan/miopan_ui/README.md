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
- `miopan_rml_title.cpp` -- the title screen: PRESS START, then the menu as
  one column, with the port's own Exit Game (and Chapter Select / Debug Menu
  under `[developer] debug_menu`).
- `miopan_rml_load.cpp` -- the title's Load Game: the five saves with their
  rooms' pictures, the chosen one's chapter, room, play time and clears, and
  the game's memory-card messages.
- `miopan_rml_save.cpp` -- the save screen, wherever the game saves (save
  points, the clear menu, Mission Mode): the same five saves, "save here?",
  and the card's own questions as yes / no windows.
- `miopan_rml_saves.cpp` -- the five saves and the chosen one's detail, as
  the load and save screens both show them.
- `miopan_rml_savepoint.cpp` -- the save point's menu: Save Game, Album and
  Resume Game in the game's words, with its questions.
- `miopan_rml_setup.cpp` -- the setup menu a cleared save opens: Story,
  Mission or back to the title, and the costume, accessories and difficulty,
  the Xbox extras among them once imported.
- `miopan_rml_menu_top.cpp` -- the in-game menu's hub, composed as the game
  composes it: the pages on their gold bars, the status plate, the play data
  on its sheet, the chapter's pillar, the caption window.
- `miopan_rml_menu_item.cpp` -- the in-game menu's Item page, composed the
  same way: the list on its row bars with the scrollbar, the item's picture
  cross-fading under the magnifier, its description, the game's windows.
- `miopan_rml_menu_status.cpp` -- the status plate as the menu pages show it:
  Mio's portrait in its leaves, her health, the Power-Up Lenses with their
  levels, the film loaded.
- `miopan_rml_menu_art.cpp` -- how the in-game menu pages are composed: the
  game's own art placed where its sprite tables put it, in its 4:3 picture,
  with RmlUi's text over it.
- `miopan_rml_sprite.cpp` -- the game's own 2D art (an icon, a portrait) cut
  out of the pak it lives in, or a lone TIM2 like an item's picture, for a
  screen to show as an image -- mirrored, turned or added as the game draws
  it.
- `miopan_rml_common.cpp` -- what the screens share: the game's menu sounds,
  button prompts, the yes / no window.
- `miopan_game_text.cpp` -- the game's own message strings as UTF-8, for the
  screens that show its words: the message font's glyphs as its atlas draws
  them, and the control codes' lengths as the ROM's printers step them.

The title, load, save, save point, setup and menu screens stand in for
screens the game still runs: each frame the phase asks for the screen and
acts on the player's answer with its own code, and a frame that does not ask
hides it. With RmlUi unavailable the game's own screens run as they always
did. Pictures made at run time, like a save's room snapshot decoded from its
TIM2 or a sprite cut out of a pak, reach RmlUi as `gen:` images
(`MioPanRml::SetGeneratedImage`).

The look they share is `resources/rml/theme.rcss`, taken from the game's own
windows; its images are drawn by `tools/rml_art.py`. RmlUi, FreeType and the
Crimson Text font come from `cmake/rmlui.cmake`, and `resources/rml` is
deployed next to the executable on every build.
