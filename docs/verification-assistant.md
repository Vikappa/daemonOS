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
| [Screenshot](screenshots/launcher.png) | [Screenshot](screenshots/creature.png) | [Screenshot](screenshots/lab.png) |

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

## Deferred assistant tools extension — 2026-11-25

Tested on Linux x86-64 with GCC 14.2 and a workspace-local CMake 3.31.6;
SDL 2.30.0, LVGL 9.2.2 and SQLite 3.50.4 used the repository's pinned sources.
No system packages or project dependency pins were changed.

- `cmake -S . -B build/check -G 'Unix Makefiles' -DDAEMON_SYSTEM_SDL=OFF -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build/check -j 4`
- `ctest --test-dir build/check --output-on-failure`: all three suites pass.
  Domain suite: 16 cases, 954 checks; assistant suite: six cases, 489 checks.
  Existing SDL/LVGL restart/navigation/export/fault scenario also passes.
- `cmake -S . -B build/sanitize-check -G 'Unix Makefiles' -DDAEMON_BUILD_SIMULATOR=OFF -DDAEMON_SANITIZE=ON`
  followed by build and
  `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize-check --output-on-failure`:
  both domain and assistant suites pass with AddressSanitizer/UBSan.
  The initial default run failed at LeakSanitizer startup because this execution
  environment uses ptrace. Leak detection is therefore **not validated here**;
  it was disabled only for the local rerun, with no change to project/CI settings.
- A separate `DAEMON_BUILD_SIMULATOR=OFF`, `DAEMON_STATIC_ANALYSIS=ON` configuration
  built `daemon_core` with GCC `-fanalyzer` and warnings as errors.
- `python3 tools/check_architecture.py` and `git diff --check` pass.
- A 410 x 502 headless LVGL screenshot after a real SDL Pet click was inspected:
  the new Italian reply fits the existing message area. No sprite/layout redesign
  is included in this extension.

Assistant tests cover bounded nested parsing, forbidden tools/arguments, capability
rejection before enqueue, transaction rollback, duplicate IDs/conflicting reuse,
restart before execution, overdue reminders, wall-clock changes, snooze/complete,
offline deferred dispatch, local message confirmation, cancellation, expiry,
unknown/duplicate phone results, result retention during a database fault,
interrupted delivery without replay, and inference-to-local-tool-to-result context.

Host `sizeof(Runtime)` is 24,344 bytes, not an ESP32 ABI or total heap measurement.
The production inference adapter, assistant/approval UI, Android executor, recipient
resolver, wire authentication/serialization, hardware RTC wake/haptics and physical
power/storage behavior remain unimplemented or unvalidated. No SMS or phone alarm
was created by these tests.

## Scheduled agent workflows and service interfaces — 2026-11-25

The same pinned dependencies/GCC 14.2 workspace build was reused.

- `cmake --build build/check -j 4` and
  `ctest --test-dir build/check --output-on-failure`: four suites pass, including
  the existing real SDL/LVGL restart scenario and the new scheduler suite
  (10 cases, 187 checks).
- `cmake --build build/sanitize-check -j 4` and
  `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize-check --output-on-failure`:
  all three core/domain suites pass with AddressSanitizer/UBSan. The prior ptrace
  restriction still prevents a valid LeakSanitizer claim; CI defaults are unchanged.
- `cmake --build build/analyze-check --target daemon_core -j 2`: GCC static analysis
  passes with warnings as errors. `python3 tools/check_architecture.py` and
  `git diff --check` pass. JSON Schema validation passes for all schemas and the
  new scheduler, service-catalog and service-result examples.

New tests verify no inference before due time, research/result/source continuation,
simulated wake, task creation from structured inference, unread result recovery,
restart during uncommitted inference, atomic task/job rollback, immutable grants,
offline expiry, cancellation of pending sends, bounded agent loops, binding
validation, complete confirmation payloads, duplicate external-effect prevention,
maximum bounded context composition and preservation of v3 jobs during migration.
The scripted provider/fake bridge perform no real search, mail or messaging calls.

Host object sizes: Runtime 32,552 bytes; ContextBuilder 8,200; ResponseParser 4,112;
ToolCall 616; ToolPage 4,728. These are host ABI values, not embedded memory totals.
APK API mappings, authenticated wire codecs, real inference, RTC wake, audio/TTS,
complete assistant/approval UI, recurrence/DST and automatic history archival are
not claimed implemented or physically validated.

## Approved LCD activity faces and speech motion — 2026-11-25

Integrated the approved 13 x 12 filled cat masks, then added seven operational
expressions and playback-driven mouth poses. Existing body asset loading was
removed. A 16-frame animated preview was rendered from `daemon_face_frames`;
actual 410 x 502 SDL/LVGL calm/speaking captures were inspected, including identical
LCD color outside the head and inside eye/mouth cutouts. Documentation captures
were refreshed from the actual simulator.

- `cmake --build build/check -j 4` and
  `ctest --test-dir build/check --output-on-failure`: all five suites pass.
- Expanded actual SDL/LVGL tests verify all seven activity faces are distinct,
  true provider-wait rendering with injected mock latency, mouth movement during
  playback, unchanged head/eyes while speaking, and restoration after stop.
- `creature_faces_and_speech` covers every face/phase/mouth within the approved
  silhouette, distinct poses, feature-only animation, playback duration/silence,
  stale utterance callbacks, status routing and no mouth activity from model text.
  The persistence regression also checks that a local store failure is not
  misreported as an unreachable inference service.
- `cmake --build build/sanitize-check -j 4` and
  `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize-check --output-on-failure`:
  all four core/domain/portrait suites pass with AddressSanitizer/UBSan. The
  previously documented ptrace/LeakSanitizer restriction remains; GUI sanitizers
  and leak detection are not claimed executed in this configuration.
- `cmake --build build/analyze-check --target daemon_core daemon_portrait -j 2`,
  architecture boundaries and `git diff --check` pass.

Host Event is now 12 bytes, EventBus 1192, Runtime 32888 and SpeechMotion 40;
these are host ABI values, not embedded fit or task-stack measurements. Activity
injection is simulated: real installation, phone/network, microphone, TTS/audio,
RTC wake and physical battery behavior still require their respective adapters.

## Voice directory and independent authorization — 2026-11-25

The existing workspace-local CMake (`.cache/tools/cmake/data/bin/cmake`), pinned
SQLite/LVGL/SDL sources and GCC 14.2 builds were reused. No global installation,
real microphone, voice model, phone account or external service was used.

- `cmake --build build/check -j 4` and
  `ctest --test-dir build/check --output-on-failure`: all six suites pass, including
  actual SDL/LVGL restart/recovery and the new voice suite (203 checks).
- `cmake --build build/sanitize-check -j 4` and
  `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize-check --output-on-failure`:
  all five non-GUI suites pass with AddressSanitizer/UBSan. Leak detection is still
  disabled because of the previously documented ptrace restriction; no leak-check
  or GUI-sanitizer claim is made.
- `cmake --build build/analyze-check --target daemon_core daemon_portrait -j 2`
  passes with GCC analyzer and warnings as errors. The changed SQLite translation
  unit also passes its generated Makefile object target:
  `make -C build/analyze-check -f CMakeFiles/daemon_desktop.dir/build.make CMakeFiles/daemon_desktop.dir/core/platform/desktop/sqlite_store.cpp.o`.
  Broadening to the whole `daemon_desktop` target fails in the unchanged
  `desktop.cpp`: GCC reports uninitialized values in libstdc++ `basic_string`
  allocator/concatenation internals. That broader check is not claimed passed;
  no unrelated code or diagnostic policy was changed to suppress it.
- `python3 tools/check_architecture.py` and `git diff --check` pass.

Voice tests use synthetic fixed-dimension vectors and explicitly TEST-ONLY
protector/authenticator implementations. They cover opt-in guest creation,
November 7/23/25 encounter continuity, visit/context deduplication, untrusted name
claims, immutable guest roles, ambiguous/invalid/overlapping samples, incompatible
encoder versions, key/storage failures, session expiry/lock/revision invalidation,
actor-scoped notes and model context, cross-actor operation-ID rejection,
voice-only denial of mail/private history, unlocked-owner approval of the stored
payload, approval/actor revocation before dispatch, actor inheritance in scheduled
work, deferred cancellation after permission changes, guest deletion, template
forgetting, missing production adapters, bootstrap adoption of legacy notes and
fail-closed storage recovery. Existing migration fixtures now remove v5 additions
when deliberately constructing old-schema test databases; v1/v3 upgrade behavior
and existing jobs/notes are still checked.

Host object sizes: Runtime 34,040 bytes; VoiceDirectory 648; VoiceProfile 312;
VoiceObservation 972; VoiceTemplate 1,160; MemoryPage 7,304; ToolPage 4,904.
These are host ABI values, not ESP32 memory or stack measurements. Matching has no
new dependency or domain heap allocation, but its stack/latency, real speaker
accuracy, calibration, anti-spoofing, key handling and audio cancellation must be
measured/integrated on the eventual platform. The complete enrollment/directory/
approval UI, real encoder, local unlock, encrypted-template adapter and
transport remain unimplemented; no live voice was enrolled or message sent.
