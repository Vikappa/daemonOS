# Habitat resource budgets and placement

Nominal hardware is 512 KiB internal SRAM, 8 MiB PSRAM and 16 MiB flash. Nominal
capacity is not application-available memory. `ResourceManager` caps application
reservations at **128 KiB internal** and **6 MiB external**, reserving headroom
for SDK, caches, drivers and future fragmentation. Link-map/static usage and
runtime free/largest-block telemetry must still be checked on hardware.

| Category | Embedded cap | Desktop profile | Current placement/use |
|---|---:|---:|---|
| Runtime/task claims | 64 KiB | 64 KiB | 32 KiB task stack + platform queue internal; runtime static PSRAM |
| Graphics | 768 KiB | 768 KiB | 512 KiB LVGL arena + 40-row partial draw buffer(s) |
| Audio | 64 KiB | 64 KiB | unallocated; microphone off |
| Asset cache | 128 KiB | 128 KiB | unallocated; original sprite in flash |
| Database | 192 KiB | 1 MiB | fixed SQLite memsys5 heap in PSRAM / native aligned arena |
| Cognitive workspace quota | 32 KiB | 32 KiB | bounded context/response/page reservation while a request is active |

The desktop LVGL arena is a fixed native BSS buffer reserved in the same ledger;
its two actual draw buffers use the budgeted allocator. Native SDL framebuffers,
textures, C++/OS heaps and the debug window are **host-only overhead**, not SRAM
estimates. Embedded LVGL arena/runtime use external BSS, verified in the map;
its draw/SQLite heaps use `heap_caps_aligned_alloc` with SPIRAM capability.

A 410 × 40 ARGB8888 watch buffer is **65,600 bytes**; the desktop lab adds
460 × 40 × 4 = 73,600 bytes. Neither target requires an embedded full framebuffer.
The arena is 524,288 bytes. RGB565/DMA staging is a future physical driver decision;
no arbitrary PSRAM buffer is declared DMA-safe. Reserve internal DMA-compatible
memory when actual QSPI/I2S drivers establish their alignment/cache constraints.

The firmware platform queue (64 × 8-byte events plus metadata) and 32 KiB task
stack are internal. The core's non-ISR queue is part of its PSRAM runtime object.
SDK allocations and the configured 64 KiB internal malloc reserve remain outside
application accounting. The 32 KiB stack includes bounded temporary memory pages
and response parsing. The cognitive quota is a conservative additional workspace
reservation; it is not a claim that all temporary stack data lives in PSRAM.

| Bounded structure / policy | Limit |
|---|---:|
| Event payload / queue / subscribers | 8 bytes / 64 / 16 |
| Services / wake locks / scheduler tasks | 9 / 16 / 16 |
| App registry / navigation | 8 / 8 |
| Buffered unsaved experiences | 16, drop-new with telemetry |
| Retrieved memory page | 8; content <=383 bytes |
| Query text / offset / VM instructions | 96 bytes / 10000 / ~100,000 |
| Context / raw response | 4096 / 2048 bytes |
| agent.md / speech | 1023 / 511 UTF-8 bytes |
| Actions / proposals per kind | 4 / 2 |
| Cognitive interval / timeout / failure backoff | 60 s / 10 s / up to 32 min |
| Native SQLite cache / busy timeout | 128 KiB / 100 ms |
| Embedded SQLite cache / hard arena | 32 KiB / 192 KiB |
| State save / checkpoint cadence while active | 60 s / 300 s + sleep/shutdown boundaries |

RAII leases enforce category/pool bounds and track reservation peaks/rejections.
Allocation failure rolls accounting back. Pressure rejects optional reservations,
cancels/reclaims cognitive work and requests SQLite cache release; essential
runtime reservations remain possible within their cap. Fixed arenas stay resident
while their owners live. No asset/audio cache is falsely claimed reclaimed.
No heap allocation occurs in event posting, minute simulation, deadline selection
or bounded lock bookkeeping. Native database, UI construction and diagnostics
remain controlled cold/desktop paths. See the verification report for actual ELF
sections; these design caps are not measured firmware high-water marks.
