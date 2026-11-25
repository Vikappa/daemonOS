# Habitat v0.2.0 verification report

Date: **2026-11-25**. Baseline: Genesis `69d8c68`. All results below were executed
locally against Habitat source. CI was updated but no remote repository, Actions
run, push or release publication was created. No physical watch is available.
[Genesis results are archived](verification-genesis.md).

## Acceptance and actual checks

| Check | Result |
|---|---|
| Desktop Debug configure/build | PASS, C++20, first-party warnings treated as errors |
| Desktop Debug CTest | PASS, **4/4** entries |
| Genesis regression executable | PASS, **16 cases / 954 checks** |
| Habitat executable | PASS, **9 cases / 115 checks** |
| Native Release, source-built pinned SDL2 | PASS, build + **4/4** CTest entries |
| ASan + UBSan + LeakSanitizer | PASS, **4/4**, leak detection enabled |
| ESP-IDF v5.5.1 / Xtensa GCC 14.2.0 cross-build | PASS, real ELF/bin/map generated |
| Firmware report/map placement/OTA slot checks | PASS, common runtime/view linked and LVGL arena in external BSS |
| Clang 18 C++ static analyzer | PASS, **17** first-party core/adapter/presentation units |
| Core-only analysis build | PASS, **10** portable units |
| Architecture boundaries and protocol JSON syntax | PASS |
| Pinned local SDK/Python package check | PASS; also checked without a mutable IDF constraints file |
| Non-headless X11 smoke | PASS, two windows, 40 iterations, 9 watch presentations, three mouse interactions and clean shutdown |
| Actual framebuffer review | PASS, launcher, familiar and separate lab inspected |

LeakSanitizer and X11 ran outside the restricted traced sandbox because that
sandbox prevents leak tracing/display access. Sanitizer checks were not merely
rerun with leak detection disabled. GCC 13 `-fanalyzer` produced uninitialized
return-object diagnostics on initialized move-only handles; its documented scope
is C. Habitat instead uses Clang for C++ without suppressing first-party analyzer
warnings (ADR 0007). No hardware/peripheral test is inferred from these results.

The four CTest entries are the original domain/persistence suite, Habitat
power/resources/presentation suite, original SDL restart/fault scenario and a new
three-day SDL suspension/configuration/pressure scenario. The two C++ executables
total **25 cases and 1069 checks**; Python assertions are additional.

## Behavioral evidence

Genesis regression coverage remains: bounded events/reentrancy, registry and
navigation lifecycle, deterministic partitioned needs, offline caps, clock
rollback, power/scheduler behavior, 500 deterministic parser noise inputs,
transaction rollback, SQLite migrations/pagination/export, corrupt state,
checksum recovery, storage faults and identity continuity.

Habitat adds service power coordination, allowed resource masks, RAII lock
move/reset/expiry/reuse, Listening/Speaking transitions, real buffer allocation
caps, pressure cancellation/reclamation, scheduled work during sleep and local
agent/name configuration. A 10,000-call quiescence test after a three-day time
jump performs no needs updates or writes until wake. Depleted-battery recovery
preserves health; ten elapsed days exercise the existing seven-day catch-up cap.
A 5,000-row SQLite history test hits the VM work limit and returns an empty budget
error rather than scanning without bound.

The **actual shared watch view** is unit-tested through a real LVGL display and
flush callback. A thousand sleeping update/timer calls produce no flushes, then
wake redraws. SDL tests drive real mouse events, not direct state mutation, and
verify persistent identity/name/settings, storage healing, export and corrupted-card
checkpoint recovery. The new scenario checks zero asleep flushes, bounded
simulation calls, released cognitive memory and a five-second speaking lock.
V2 parsing covers silence, voice/proposals, invalid UTF-8/escapes, unknown actions,
duplicate versions and capability denial before effects.

Screenshots are actual LVGL buffers converted losslessly to PNG:
[launcher](screenshots/launcher.png), [familiar](screenshots/creature.png),
[lab](screenshots/lab.png). A fresh 100-frame headless interaction capture reports
18 watch presentations, zero asleep flushes, 3 simulation materializations, queue
peak 1, internal reservation 32,768 bytes and external peak 1,738,424 bytes. These
are simulator reservation observations, not measured ESP SRAM/current.

## Firmware image and memory evidence

Outputs: `build/firmware/daemonos_firmware.elf`, `.bin`, `.map`, bootloader,
partition table and clean `size-summary.json` / `size-components.json`.
[Committed machine-readable evidence](verification/habitat-firmware-size.json)
records the pinned SDK/compiler, sections and selected component contributions.

| Link/build observation | Bytes |
|---|---:|
| Application binary, padded | 944,544 |
| IDF image size before binary padding | 944,295 |
| One OTA slot | 4,194,304 |
| Free space in each OTA slot | 3,249,760 |
| Flash text | 670,540 |
| Flash read-only data | 194,372 |
| Internal data | 15,512 |
| Internal BSS | 4,288 |
| IRAM vectors + IRAM text, map | 63,615 |
| External BSS / PSRAM | 549,904 |

Internal static data+BSS+IRAM code/vectors total **83,415 bytes**.
IDF reports 67,031 bytes used in its shared DIRAM region and 274,729 bytes left
there before dynamic SDK/application heaps. A separate 16,384-byte IRAM subregion
is fully used; this is not a claim that all nominal 512 KiB SRAM is exhausted.
External BSS is the 524,288-byte LVGL arena plus the 25,616-byte runtime object.

Known dynamic allocations include 65,600 bytes of PSRAM draw buffer and a fixed
196,608-byte SQLite arena, plus a 32 KiB internal main task stack and platform
queue. SDK heap, stacks, cache/DMA constraints and high-water marks require actual
execution. The link map demonstrates placement, not runtime memory qualification.
The report separates external NOBITS data from flash image bytes; a formatted
IDF table grouping it with other data must not be read as flash consumption.

Selected archive flash contributions: SQLite 348,617 bytes, LVGL 265,960 bytes,
portable core 22,712 bytes and shared presentation 7,894 bytes. SQLite itself
accounts for substantial flash and must stay subject to the embedded gate.
No physical SQLite probe, NVS write, display flush, radio operation, sleep current
or wake latency was executed on hardware.

## Reproducibility and open exceptions

Host: Ubuntu 24.04.5 x86-64, GCC 13.3.0, Clang 18.1.3, CMake 3.28.3, Ninja 1.11.1,
Python 3.12.3. ESP-IDF v5.5.1 is pinned to
`fcae32885b0296b32044cb99ecbdc50d98dddb83`; Xtensa is
`esp-14.2.0_20241119`. LVGL/SQLite/SDL archives retain Genesis hashes. SDK/tools,
venv and caches are project-local. Python versions are locked, not wheel hashes
or an entire bit-reproducible host image.

Habitat's desktop and cross-build software criteria are satisfied with explicit
physical gates: unsupported display/touch/audio/radio/sensor/RTC/deep-sleep
operations; no usable watch firmware, no FatFS SQLite VFS or SD history, and no
hardware NVS durability/wear qualification. The compiled NVS minimum adapter and
memory-only SQLite readiness probe are not runtime hardware validation.

Other debt: seven-day catch-up cap, lossy RAM experience queue on crash,
unindexed histories may hit the query budget, ABI-specific NVS records lack
historical migration/encryption, v2 Unicode escape support is deferred, and
personality/intention execution plus real inference remain Awakening work.

The [energy model](architecture/energy-budget.md) uses nominal 1100 mAh and
explicit hypothetical currents. Its 5.2071-day sample projection is not a measured
result. Five days is the design goal, three days the desirable minimum.
Next milestone: **Awakening v0.3.0**.
