# OpenA2Racer

Open-source reimplementation of the 1997 Davilex game A2 Racer for macOS and
Windows (C++20, SDL3, OpenGL 3.2 core, FreeType, libsmacker). GPL-3.0
(`LICENSE`). Repository: https://github.com/bjorncm/OpenA2Racer.

## Rules

- Never commit original game files, derived assets, decompiler output or
  analysis databases. The user's copy lives in `CD-ROM/` (Git-ignored). At
  runtime the game copies it from a user-chosen folder into the preference
  folder (`game-data`), or uses a folder given on the command line as it is.
- Decompiling and disassembling the original modules (`A2.EXE`, `A2MENU.DAT`,
  `A2SPEL.DAT`, `A2MOVIE.DAT`) and porting their logic is allowed; prefer
  confirming facts from the binary over guessing.
- `docs/` holds local reverse-engineering notes and is Git-ignored (like
  `CD-ROM/`); it may be missing in a fresh checkout. Don't reference it from
  code, comments or other published files.
- Facts about the original go into `docs/menu-notes.md` when it is present,
  tagged by source ([asset], [text], [binary] with the function or address,
  [guess]).
- Add every user-visible difference from the original to `CHANGELOG.md`.
  The version number is in `CMakeLists.txt` (`project(... VERSION ...)`).

## Build, test, run

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/opena2                                     # first start: copy from CD, start window
./build/opena2 "CD-ROM/Image/Image"                # game menu, no copy, no start window
./build/opena2 "CD-ROM/Image/Image" --no-intro --capture /tmp/shot.bmp 2
./build/opena2_asset_browser                       # asset inspection tool
```

On macOS `opena2` is built as `build/OpenA2Racer.app`; `build/opena2` is a link to
the program inside it. `cmake --install build --prefix dist/macos` makes a
self-contained `OpenA2Racer.app` (`cmake/bundle-mac-libraries.sh` copies the
Homebrew libraries in and signs it ad hoc) and the installer
`OpenA2Racer.dmg` (`cmake/make-mac-dmg.sh`). Windows x86 and ARM64 are
cross-compiled with llvm-mingw (`cmake/toolchains/windows-*.cmake`, see
README) into `dist/windows-x86` and `dist/windows-arm64`. The icons
(`assets/OpenA2Racer.icns`, `.ico`) are made from `assets/OpenA2Racer_icon.png`
with `tools/make_icons.py`. Windows' `windows.h` defines `near` and `far`:
don't use them as names.

`--capture` saves the rendered frame at window pixel resolution; use it to
check screens visually. The first configure downloads libsmacker (pinned
commit); `-DOPENA2_LIBSMACKER_SOURCE_DIR=<dir>` uses a local checkout.

Player and score saves go to `~/Library/Application Support/OpenA2/OpenA2/`
on macOS, beside the copied game files (`game-data/`) and the start window's
`launcher.ini`; remove test data from there after manual testing.

## Layout

- `include/opena2/`, `src/*.cpp`: `opena2_data` library (archives, PCX/indexed
  images, WAV, D3D meshes, Smacker movies, player profiles, scores, menu
  button state, screen transform, copying the CD and the start window's
  settings in `installation`). Unit-tested in `tests/`.
- `src/game/`: the `opena2` menu application (GL loader, assets, painter,
  audio mixer, menu screens, start window and copy progress in
  `start_window`). `src/game_main.cpp` is only the entry point.
- `include/opena2/race/`, `src/race/`: the race logic, without SDL or GL.
- `src/main.cpp`: the separate asset browser.
- `tools/`: `make_icons.py` and the Ghidra scripts (`tools/ghidra/`).
- `.github/workflows/build.yml`: CI, builds and tests on macOS.

## Rendering

The menu lays out in logical 640x480 coordinates (`ScreenTransform`) but
rasterizes at the window's real pixel resolution: 8-bit textures with a
palette lookup in the shader, and FreeType text at the displayed size. Keep
new drawing in logical coordinates so the art can later be replaced by
vector images.

## Hand-off

When the local `docs/` folder is present, `docs/STATUS.md` has the state and
hand-off notes and `docs/reverse-engineering/` the specs decoded from the
original binaries. The Ghidra setup is in `tools/ghidra/README.md`.
