# Reproducible dependencies

| Dependency | Pin | SHA-256 archive | License |
|---|---|---|---|
| LVGL | v9.2.2 | `129b4e00e06639fa79d7e8a6cab3c1ecce2445b1a246652ccd34f22e7b17ad6f` | MIT |
| SQLite amalgamation | 3.50.4 / 3500400 | `1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032` | public domain |
| SDL2 source | 2.30.0 | `36e2e41557e0fa4a1519315c0f5958a87ccb27e25c51776beb6f1239526447b0` | zlib |
| ESP-IDF | v5.5.1, future firmware baseline | not fetched by desktop | Apache-2.0 |

The archives came from upstream HTTPS URLs and their digests were computed at
initialization. `cmake/Dependencies.cmake` enforces these exact hashes. This pins
the received artifacts; it is not a claim of independently verified signatures.
Upstream repositories: [LVGL](https://github.com/lvgl/lvgl),
[SQLite](https://github.com/sqlite/sqlite), [SDL](https://github.com/libsdl-org/SDL),
[ESP-IDF](https://github.com/espressif/esp-idf).

Default desktop configuration requires system SDL2 2.30.0 exactly, matching the
available Ubuntu 24.04 environment. Rebuild from the pinned source instead with
`-DDAEMON_SYSTEM_SDL=OFF`; CI uses this path. Its native display backends depend
on available system development headers. A dummy SDL driver can run without X11.
The system compiler and display libraries are environment prerequisites, not
bit-for-bit pinned toolchains.

For offline configure, place archives with these exact filenames under
`.cache/downloads/`: `lvgl-v9.2.2.tar.gz`, `sqlite-amalgamation-3500400.zip`,
`SDL2-2.30.0.tar.gz` (last one needed only when building SDL locally). CMake
verifies and extracts them into the preset's `build/.../_deps` directory.
Otherwise CMake downloads from the fixed HTTPS URLs. Cache and build outputs are
Git-ignored. Dependency license files remain in the fetched source directories;
redistributors must preserve the relevant upstream notices.

No Arduino, NimBLE, Mbed TLS or Opus binaries are linked into Genesis desktop.
ESP-IDF supplies FreeRTOS/FatFS/NVS/Mbed TLS for later firmware. NimBLE and
LilyGoLib integration must receive explicit pins and compatibility ADRs when
actually integrated. Opus remains a measurement candidate rather than a codec
commitment. No extra firmware runtime is introduced.
