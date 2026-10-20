# Third-party code

| Library | Where | Licence | How it's used |
|---|---|---|---|
| cJSON | `third_party/cjson` | MIT | compiled in |
| stb_image, stb_image_write | `third_party/stb` | MIT / public domain | compiled in |
| mpv client headers | `third_party/mpv/include` | ISC | headers only; libmpv is loaded at runtime |
| libmpv (Windows) | downloaded in CI (LGPL build) | LGPL 2.1+ | separate DLL next to the exe |
| SDL2 | vcpkg / system | zlib | linked |
| FreeType | vcpkg / system | FreeType licence (FTL) | linked |
| HarfBuzz | vcpkg / system | MIT | linked |
| libcurl | vcpkg / system | curl licence | linked |
| Noto Sans fonts | `assets/fonts` | SIL OFL 1.1 | bundled, see `assets/fonts/OFL.txt` |

No GPL code is included.
