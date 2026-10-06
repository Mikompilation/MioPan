# What is MioPan?
MioPan is an in-progress port of Fatal Frame II: Crimson Butterfly (PS2) to PC,
running natively with no emulation. It is the sister project of
[MikuPan](https://github.com/Mikompilation/MikuPan), the Fatal Frame 1 port.

![logo](resources/miopan.png)

[![Discord](https://badgen.net/badge/icon/discord?icon=discord&label)](https://discord.gg/Ap4Sfcmwd9)
![Downloads](https://img.shields.io/github/downloads/Mikompilation/MioPan/total)

The game code is reconstructed function by function from a February 6, 2004
prototype build (`SLES_523.84`), using its debug symbols, source line tables and
link map. The reconstructed sources are compiled with a normal host compiler.
The PS2 SDK (graphics, VU0/VU1, DMA, sound, memory card, IOP) is replaced by PC
shims built on SDL3. Still very early work: expect crashes, missing effects and
rendering differences.

# About the use of AI
**MioPan is built with heavy use of AI.** Most of the reconstruction and porting
work in this repository was done with AI coding assistants, guided and reviewed
by a human.

This applies to **MioPan only**. It is the one project in the Mikompilation
organization developed this way. The other Mikompilation projects, including
MikuPan, are not.

What this means in practice:
- Code may contain mistakes that look plausible. Accuracy to the original game is
  the goal, so a function that differs from the ROM is a bug and worth
  reporting.
- Comments and notes in the sources were written as part of that process. They
  record what was checked against the ROM and why, and can be long.

# Build
The build fetches its dependencies automatically (SDL3, SDL_shadercross, spdlog,
cglm, Dear ImGui, ImPlot). FFmpeg, used for the game's movies, comes from the
prebuilt tree in `extern/ffmpeg` (Windows binaries). Without it the build still
works, but movies play their audio over a black screen.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target MioPan
```

The executable is placed in `build/MioPan`.

## Windows
Build with `MinGW` (MSYS2 or the one bundled with CLion) or `MSVC`, plus `CMake`.
MinGW is the toolchain the nightly builds use. If every MinGW compile fails with
no error message, MinGW's `bin` folder is missing from `PATH`.

## Linux
`GCC`, `CMake` and `Ninja` are required, plus SDL3's system development packages
(X11/Wayland, ALSA/PulseAudio and so on). See the
[nightly workflow](.github/workflows/Nightly.yml) for the full `apt` list.

# Game files
MioPan does not include any game data. You must provide `IMG_BD.BIN` from your own
copy of the game. Data from versions other than the one listed above is untested.

Place `IMG_BD.BIN` next to the `MioPan` executable, in a `data` folder beside it,
or in any parent folder. To use another location, set the `MIOPAN_DATA_DIR`
environment variable or `data_folder` under `[paths]` in `miopan.ini`.

Settings, logs and memory card saves are stored in the user's application data
folder (`%APPDATA%\Mikompilation\MioPan` on Windows).

# License
MioPan is licensed under the [GNU AGPL v3](LICENSE). Fatal Frame II is the property
of its respective owners. This project is not affiliated with or endorsed by them.
