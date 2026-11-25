# Resource policy

These are explicit design budgets, not measured ESP32 memory usage. Host resource
measurements are recorded separately in `docs/verification.md`.

| Resource | Genesis limit / allocation | Intended embedded placement |
|---|---:|---|
| Event queue | 64 value events | internal SRAM |
| Subscribers / service entries | 16 / 16 | internal SRAM |
| Scheduler tasks | 16 | internal SRAM |
| App registry / navigation | 8 / 8 | internal SRAM |
| Pending experiences during faults | 16, drop-new with counter | PSRAM |
| Memory result page | 8, content <=383 bytes each | PSRAM |
| Cognitive context / raw response | 4096 / 2048 bytes | PSRAM |
| Parsed speech / action count | 511 bytes / 4 | PSRAM |
| Error detail | 191 bytes, no heap allocation | caller stack |
| Mock cognitive interval / timeout | 60 s / 10 s | runtime policy |
| Mock output tokens | 256 | provider contract |
| Failure backoff | interval x2..x32 | monotonic deadlines |
| SQLite page cache | 128 KiB suggested cap | PSRAM after VFS validation |
| SQLite DB size | 16384 pages, normally 64 MiB | MicroSD |
| SQLite row / SQL parser limit | 1 MiB / 64 KiB | defense for desktop adapter |
| Memory query input / offset | 96 bytes / 10000 | bounded result; scans can still grow |
| Flash-equivalent checkpoint | <=1024 bytes, CRC32 | future NVS A/B records |
| Automatic state save / checkpoint | 60 s / 300 s | batch policy |
| Watch partial draw buffer | 410 x 40 x 4 = 65,600 bytes | PSRAM / DMA staging to validate |
| Debug partial draw buffer | 460 x 40 x 4 = 73,600 bytes | desktop only |
| Watch framebuffer | 410 x 502 x 4 = 823,280 bytes | desktop only; embedded needs review |
| Debug framebuffer | 460 x 670 x 4 = 1,232,800 bytes | desktop only |
| LVGL fixed heap | 2 MiB, both desktop displays | desktop profile, not SRAM budget |

Host SDL textures, framebuffers, debug objects, native SQLite allocations and
standard-library overhead are not an embedded memory profile. The 2 MiB LVGL
arena must move to a smaller watch-only configuration in PSRAM before firmware
integration. Plan a 410 x 40 RGB565 draw buffer (32,800 bytes), with separate
DMA-compatible internal staging only if required by the validated driver.
Do not assume arbitrary PSRAM is DMA-safe.

Reserve internal SRAM for driver/ISR/task stacks and critical buffers; measure
stack high-water marks and heap largest free blocks. Target provisional ceiling:
384 KiB committed internal RAM, leaving 128 KiB headroom; 6 MiB committed PSRAM,
leaving 2 MiB headroom. These ceilings require a real link map and runtime traces.
Firmware partitions should allow dual OTA slots; no production partition layout
is fixed until the binary and asset sizes are known.

No new/delete occurs in event posting, simulation updates or bounded scheduling.
SQLite calls, error messages, startup, sprite loading and UI construction may
allocate. Rebuilding the small UI occurs on navigation, not on every frame.
Minute simulation catch-up loops at most 10080 times per update. Seven-day jumps
are capped and excess elapsed time is intentionally discarded.

The simulated battery is an input percentage; there is no invented discharge
model or runtime estimate. Radio duty cycles, CPU scaling, OLED luminance/current,
PDM/I2S buffers, SD write energy and cold-start inference latency require hardware.
Audio candidates: 16 kHz mono signed 16-bit PCM (32,000 bytes/s), IMA-ADPCM
(approximately 8,000 bytes/s plus headers), Opus (configurable bitrate, CPU and
working memory need profiling). No codec is selected. A future 32 KiB capture
ring is a starting budget, not a validated design.
