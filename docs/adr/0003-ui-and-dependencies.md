# ADR 0003: one LVGL context, two SDL displays, pinned local dependencies

Status: accepted, 2026-11-25.
The original resource-sprite decision below is superseded by ADR 0007 compiled
LCD masks; display/dependency choices remain applicable.

Use LVGL 9.2.2 and SDL2 2.30.0. A small custom SDL adapter implements the portable
display/touch contracts; this avoids coupling application logic to LVGL's SDL
driver. A 410 x 502 watch and separate 460 x 670 lab share one LVGL owner thread.
The same input route supports mouse and headless SDL event injection.

Use source archives verified by SHA-256 through CMake FetchContent. SQLite is
compiled from its pinned amalgamation because native development headers were
not installed. SDL may use the exact Ubuntu 24.04 system version or be compiled
locally from the same pinned version. No package manager or global install is
required. Reproducibility covers dependency source, not the entire host toolchain.

Use an original ASCII pixel palette sprite loaded at startup, rather than adding
an image decoder/generator dependency. Replacing the resource does not alter
creature behavior. ARGB8888 and a two-display 2 MiB LVGL arena simplify the host;
they must not be mistaken for the embedded graphics budget. Firmware will need
a smaller watch-only RGB565/PSRAM configuration and measured DMA staging.
