# DæmonOS engineering guide

## Vision and non-negotiable ownership

DæmonOS is an offline-first operating environment for programmable wearables.
The creature is a system application, not the operating system itself. The watch
owns identity, needs, personality, memories, scheduling and context composition.
An LLM is an interchangeable cognitive coprocessor. Android is an optional
communications accessory. Never introduce a mandatory cloud service, proprietary
backend, remote database or phone-owned creature state.

## Architecture and layout

- `core/include/daemon`: portable contracts, domain types and public APIs.
- `core/framework`: application registration, navigation and runtime orchestration.
- `core/services`: deterministic creature engine and bounded cognition.
- `core/apps`: independently registered static launcher, clock, settings, creature.
- `core/platform/desktop`: virtual clock, native SQLite and checkpoint adapter.
- `simulator`: the only SDL/LVGL integration; two displays, one LVGL owner thread.
- `firmware`: ESP-IDF integration seed, not validated watch firmware.
- `protocol`: transport-independent design and JSON schemas; no production transport yet.
- `android`: integration contract, not an implemented Android app.
- `assets`: replaceable original provisional sprite and future resources.
- `tests`: deterministic domain, persistence, fault and SDL/LVGL integration tests.
- `docs/adr`, `docs/hardware`, `docs/architecture`, `docs/roadmap`: decisions and evidence.

Portable core must not include SDL, LVGL, SQLite, ESP-IDF, Arduino, POSIX or OS
headers. Concrete adapters implement contracts; do not duplicate creature logic
in firmware, Android or the UI. Future rendering code can be shared through a
presentation component, never by importing SDL into core. Do not invent an OS
kernel: embedded execution uses FreeRTOS through ESP-IDF.

## Hardware constraints

Target: LilyGO T-Watch Ultra, ESP32-S3, 512 KiB internal SRAM, 8 MiB QSPI PSRAM,
16 MiB flash, 410 x 502 AMOLED and optional FAT32 MicroSD up to vendor-stated
32 GB. Read `docs/hardware/t-watch-ultra.md` before changing board assumptions.
Direct GPIOs and XL9555 expander pins are distinct namespaces. Never energize
radio, display or PMIC paths from unverified mappings. Account for shared SPI.
Vendor display dimensions appear rotated in its header; verify orientation on
the actual board. Check physical revision and radio population first.

No hardware is available for Genesis. Mark all physical peripherals, battery
runtime, radio throughput, DMA behavior and embedded database durability as
unvalidated until measured. Never present simulator behavior as hardware evidence.

## C++ and ownership

Use C++20, English identifiers/comments/technical documentation, RAII, explicit
ownership and non-owning references with documented lifetimes. Prefer small value
types and fixed `std::array` buffers. No raw owning pointers in first-party code.
The desktop SQLite adapter may throw internally and translate to `Status` at its
public boundary. Core errors are explicit `Status` or bounded-operation booleans.
Do not rely on exceptions or RTTI in embedded domain code. Avoid heavyweight
frameworks, runtime scripting, dynamic executable loading and premature abstraction.

One runtime thread owns domain state, SQLite and LVGL. Interrupts and future worker
tasks must hand off bounded messages; never call these objects concurrently.
The current event bus is deliberately not thread-safe. Subscriptions must die
before their bus; applications before their registry/navigation dependencies.
Always check queue saturation and expose drop telemetry. Never hold a subscription
or service pointer beyond its owner. Lifecycle is start/resume, pause, stop.

## Memory and power

Keep explicit limits in `daemon/types.hpp` and `docs/architecture/resources.md`.
No heap allocations in event posting, minute simulation or scheduler execution.
Messages, memory pages, contexts and responses are bounded. Stream conversations;
never load complete history into RAM. Internal SRAM is for critical tasks, stacks,
interrupts and DMA-compatible buffers. PSRAM is for graphics, caches, audio and
context composition. MicroSD capacity is not available RAM.

Keep the microphone off by default. Render only changed content; reduce animation
while resting, suspend display and radios where appropriate. Deep sleep breaks
BLE connections; wake must reconnect. Simulated power states are policy decisions,
not real low-power transitions. Batch database writes and rate-limit flash
checkpoints. Do not state battery-life targets have been met without measurements.

## Persistence and recovery

The local `StateStore` is authoritative; concrete SQLite lives in the adapter.
State and its associated autobiographical memory must commit transactionally.
Use prepared statements, versioned migrations, bounded output and explicit errors.
Do not switch to WAL on embedded storage without a tested VFS and durability
review. Desktop uses DELETE journaling and FULL synchronization. Do not delete hot
journals, rename an open database, or raw-copy it for backup. Use SQLite export.

Flash stores a small checksummed identity/vitals checkpoint, not full history.
Missing/damaged SD enters degraded mode; preserve the existing identity. If state
exists but cannot be read and there is no valid checkpoint, fail closed instead
of silently creating another creature. Never overwrite a newer schema or silently
discard unreadable data. Recovery/import is an explicit operator operation on a
stopped runtime. See `docs/architecture/persistence.md`.

## Cognitive safety

All provider output and recalled content are untrusted data. Bound incoming bytes,
validate the complete response, then validate every requested capability before
applying any effect. Allow only known action enums. No arbitrary code, filesystem
paths, SQL, credential access, permission changes or implicit peripheral control.
Providers propose memories; the watch validates and commits them. Identity and
vital state are not set by model output. Test malicious/oversized/truncated input,
timeouts, cancellation, offline fallback and rate limits.

Real OpenAI-compatible HTTPS adapters must accept configurable endpoint/model,
keep credentials outside prompts/logs, validate certificates and limit streaming
buffers. Never expose unauthenticated inference endpoints to the Internet.
Production BLE needs authenticated pairing before personal data or audio transfer.

## Dependencies and build

Dependency archives are versioned and SHA-256 verified in `cmake/Dependencies.cmake`.
See `docs/dependencies.md` for licenses and exact pins. Do not use floating branches,
unverified downloads, global installs or silent dependency upgrades. Document the
reason and resource impact of each dependency. Do not vendor cache/build outputs.

```sh
cmake --preset desktop
cmake --build --preset desktop
ctest --preset desktop
./build/desktop/daemon_sim

cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize

cmake --preset core
cmake --build --preset core
ctest --preset core

python3 tools/check_architecture.py
cmake --preset core -DDAEMON_STATIC_ANALYSIS=ON
cmake --build --preset core
```

The default SDL dependency is system SDL2 **exactly 2.30.0** (Ubuntu 24.04 baseline).
Use `-DDAEMON_SYSTEM_SDL=OFF` to build the hash-pinned source release locally.
First configure needs network or the documented `.cache/downloads` archives.
Never install system packages or change system configuration without authorization.

## Verification, commits and completion

For substantive changes run appropriate domain/integration tests and the
architecture check. Run sanitizers for ownership, parsing and lifecycle changes.
Run a real SDL/LVGL headless scenario for UI changes and inspect a screenshot.
Use deterministic injected time/randomness in tests. No network-dependent tests.
Report exact commands, failures, environment limitations and unexecuted checks.
Do not add tests that merely mirror trivial implementation details.

Keep changes focused and use conventional local commit messages. Never commit
credentials, personal device state, backups or generated dependencies. A remote,
push, release publication or destructive data migration requires user authority.
Do not rewrite user work. Architectural changes require an ADR with alternatives,
tradeoffs and resource consequences. Update the roadmap/status when scope changes.

A task is complete when its requested behavior works, meaningful checks pass,
documentation is accurate, outstanding limitations are stated and no claimed test
is unexecuted. A scaffold or protocol draft must be labeled as such. Do not mark
future services, Android, real inference, hardware drivers or power targets done
because an interface exists.
