# ADR 0006: one watch presentation on both targets

Status: accepted, 2026-11-25; supersedes simulator-local presentation debt in ADR 0003.

Extract the existing working layout into `daemon_presentation`, with a read-only
`WatchState`, controller posting core events, and LVGL view. Navigation/domain
state stay in core. SDL and lab controls stay in simulator; firmware supplies
only display/input/platform backends. Both link the same watch source files.

Compile the original replaceable palette resource into common flash-resident
pixels. No desktop filesystem loader or second embedded UI interpretation is
required. Asset changes rebuild the resource without changing creature logic.
Use one common LVGL configuration, a 512 KiB arena and 40-row partial draw buffer.
The arena and draw buffer are PSRAM on ESP32-S3. Actual DMA/color/rotation remains
a future backend decision; ARGB8888 is currently shared and bounded.

Explicit refresh processes LVGL's dirty regions after state/input/animation/time
changes. Background refresh and pointer-read timers are paused. Sleeping views
perform no updates or flushes. Desktop keeps an independent lab display and a
2 Hz diagnostics sample, never watch-visible developer controls.
