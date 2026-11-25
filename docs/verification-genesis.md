# Genesis v0.1.0 verification report

Date: **2026-11-25**. Results below were executed locally. CI configuration is
included, but no remote repository, GitHub Actions run or release publication
was created. Physical hardware was not available.

## Environment

Linux x86-64 / Ubuntu 24.04, GCC 13.3.0, CMake 3.28.3, Ninja and Python 3.12.3.
System SDL2 is 2.30.0. SQLite 3.50.4 and LVGL 9.2.2 were built from hash-verified
upstream archives. A separate Release configuration also built SDL2 2.30.0 from
its pinned archive, exercising the same dependency path used by CI.

## Executed checks

| Check | Actual result |
|---|---|
| `cmake --preset desktop` and `cmake --build --preset desktop` | PASS, first-party C++20 with warnings treated as errors |
| `ctest --preset desktop` | PASS, 2/2 CTest entries |
| `./build/desktop/daemon_tests` | PASS, 16 named cases, 952 checks, zero failures |
| SDL/LVGL process integration | PASS, actual SDL mouse input, multiple process restarts and framebuffer assertions |
| `cmake --preset sanitize`, build and `ctest --preset sanitize` | PASS, 2/2, ASan + UBSan + LeakSanitizer |
| Release build with `-DDAEMON_SYSTEM_SDL=OFF` | PASS, SQLite/LVGL/SDL built from pinned source |
| `ctest --test-dir build/release --output-on-failure` | PASS, 2/2 |
| GCC `-fanalyzer` on `daemon_core` | PASS, no remaining diagnostics |
| `python3 tools/check_architecture.py` | PASS, portable include boundaries and protocol JSON syntax |
| Non-headless SDL X11 smoke run | PASS, 90 frames, 46 watch presentations, clean shutdown |
| Screenshot review | PASS, launcher, creature and separate lab images inspected and layout overlap corrected |

LeakSanitizer initially failed because the restricted execution sandbox uses
ptrace. The complete sanitizer suite was then executed outside that traced
sandbox and passed with leak detection enabled. A separate in-sandbox run with
`ASAN_OPTIONS=detect_leaks=0` also passed; that run alone would not prove leak
checking. X11 access likewise required execution outside the restricted sandbox.
These are environment constraints, not suppressed application failures.

Domain/integration cases cover bounded queue saturation and reentrancy,
subscription lifetime, service lookup, application lifecycle/navigation,
deterministic time partitioning and offline caps, wall-clock rollback, autonomous
sleep, power decisions, bounded scheduler replacement, malformed/oversized/
unknown cognitive input, 500 deterministic parser noise inputs, capability
rejection, request timeout/backoff, incremental mock responses and cancellation.

Persistence tests cover empty initialization, full round trips, settings,
pagination, literal text/tag/entity filters, export/reopen, a triggered SQL
failure rolling back state and memory together, migration from schema 1 to 2,
newer-schema refusal, corrupt/incomplete state, checkpoint checksum rejection,
RAM continuity during storage faults, queue overflow telemetry, reconnect drain,
restart catch-up, minimal checkpoint transfer and refusal to silently replace
an inaccessible identity.

The Python integration test launches four real simulator processes, drives clock
and settings navigation plus feeding/petting/rest, injects movement/charging/
connectivity, completes a mock stream, verifies 410 x 502 nonblank bitmaps,
restarts with elapsed wall time, checks the SQLite export, heals a storage fault,
then corrupts the stopped database and verifies checkpoint identity recovery.
It does not emulate a physical SD power interruption.

## Visual evidence

These are actual LVGL framebuffer captures, converted losslessly from BMP to PNG
for repository review. The debug panel is separate from the watch display.

| Launcher | Creature | Lab |
|---|---|---|
| [Screenshot](screenshots/genesis/launcher.png) | [Screenshot](screenshots/genesis/creature.png) | [Screenshot](screenshots/genesis/lab.png) |

Headless screenshots are captured before brightness modulation, use a fixed
timestamp and seed, and contain test state only. A non-headless X11 run also
opened both windows and processed the scripted interactions. No automated test
asserts visual aesthetics; the saved images were inspected directly.

## Host resource observations

`daemon_resources` reports host ABI sizes, in bytes:

| Object | Size |
|---|---:|
| Event | 8 |
| EventBus | 936 |
| CreatureState | 208 |
| Memory | 872 |
| MemoryPage | 6984 |
| Runtime | 23040 |
| ContextBuilder | 4104 |
| ResponseParser | 2064 |
| Scheduler | 776 |

A Release run with source-built SDL, dummy video, 120 headless frames and a
fresh state directory reported **13,268 KiB maximum RSS**, **0.02 s user CPU**,
**2.00 s wall time** and 40 watch presentations using `/usr/bin/time -v`.
Timing is environment-dependent and includes initialization, persistence and
sandbox scheduling. It is not a frame-rate or battery benchmark.

The x86-64 Release executable is approximately 5.1 MiB. GNU `size` reported
4,632,688 text bytes, 98,064 data bytes and 2,123,648 BSS bytes, including the
desktop LVGL arena and desktop dependencies. These numbers do not predict
Xtensa binary size, ESP32 SRAM fit or a production partition layout.

## Explicitly not validated / open debt

- ESP-IDF integration seed was not compiled: SDK/toolchain is absent. No flashing
  or real peripheral access was attempted.
- No CO5300/CST9217/PMIC/IMU/GNSS/NFC/radio/audio/BLE/Wi-Fi driver or current
  consumption was validated. No battery endurance target is claimed.
- SQLite on ESP32/FatFS, media power interruption, brownout recovery, NVS wear,
  DMA/PSRAM placement and real BLE fragmentation/throughput remain hardware gates.
- Android, production transports/authentication, STT/TTS, codecs, real inference,
  OTA, personality evolution and full app resource supervision remain future work.
- Database encryption, automatic salvage/import, recall counter updates and
  indexed large-history retrieval are not implemented. Fault-buffered memories
  can be lost on process death; the minimal checkpoint does not contain history.

The next recommended milestone is **v0.2 Habitat**. See
[the roadmap](roadmap/roadmap.md) and [ADRs](adr/0001-portable-genesis.md).
