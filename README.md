# Xstream TV

The Xstream TV app written in C, for TV, tablet and Windows from one code base.

- **Windows**: download `Xstream-TV-windows.zip` from the latest release, unzip, run `xstream.exe`.
- **Controls**: arrow keys move, Enter selects, Esc goes back, Space plays/pauses, F11 full screen.
- **Addons**: catalogs come from Cinemeta. Add stream addon links, one per line, to `addons.txt`
  in the app's data folder (Windows: `%APPDATA%\xe\xe\addons.txt`).

## Layout

| Folder | What it is |
|---|---|
| `include/xe`, `src/` | The engine: platform, renderer, text, images, network, UI, data, screens, player |
| `src/app/app.c` | Ties the screens together (Home, Detail, Search, Sources, Player) |
| `hosts/desktop` | SDL2 window host for Windows and Linux |
| `android/` | Android host and ExoPlayer bridge (not wired up yet) |
| `assets/` | Fonts bundled with the app |
| `third_party/` | cJSON, stb and mpv headers (permissive licences) |
| `docs/parts/` | The original P1–P12 hand-backs, kept for reference |

## Building

CI builds both on every push: **Windows build** (vcpkg + Visual Studio, zip artifact) and
**Linux build + screenshots** (runs the app under a virtual screen and saves screenshots to the
`ci-linux` branch).

Locally: `cmake -S . -B build && cmake --build build` with SDL2, FreeType, HarfBuzz and libcurl
installed (on Windows, pass the vcpkg toolchain file).
