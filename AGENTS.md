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
- `presentation`: shared LVGL watch state/controller/view; no SDL/ESP-IDF imports.
- `simulator`: SDL display/input and a separate lab; one LVGL owner thread.
- `firmware`: cross-built ESP-IDF runtime/adapters; physical operations unsupported.
- `protocol`: transport-independent design and JSON schemas; no production transport yet.
- `android`: integration contract, not an implemented Android app.
- `assets`: replaceable original provisional sprite and future resources.
- `tests`: deterministic domain, persistence, fault and SDL/LVGL integration tests.
- `docs/adr`, `docs/hardware`, `docs/architecture`, `docs/roadmap`: decisions and evidence.

Portable core must not include SDL, LVGL, SQLite, ESP-IDF, Arduino, POSIX or OS
headers. Concrete adapters implement contracts; do not duplicate creature logic
in firmware, Android or the UI. All watch rendering now lives in the shared
presentation component; never import SDL into it or into core. Do not invent an OS
kernel: embedded execution uses FreeRTOS through ESP-IDF.

## Hardware constraints

Target: LilyGO T-Watch Ultra, ESP32-S3, 512 KiB internal SRAM, 8 MiB QSPI PSRAM,
16 MiB flash, 410 x 502 AMOLED and optional FAT32 MicroSD up to vendor-stated
32 GB. Read `docs/hardware/t-watch-ultra.md` before changing board assumptions.
Direct GPIOs and XL9555 expander pins are distinct namespaces. Never energize
radio, display or PMIC paths from unverified mappings. Account for shared SPI.
Vendor display dimensions appear rotated in its header; verify orientation on
the actual board. Check physical revision and radio population first.

No physical hardware is available for Habitat. Mark all physical peripherals, battery
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
BLE connections; wake must reconnect. Power policy drives service callbacks and application lifecycle. Physical
unsupported operations remain explicit; configured SDK light sleep is not measured. Batch database writes and rate-limit flash
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
python3 tools/analyze.py --build-dir build/desktop

python3 tools/bootstrap_embedded.py
python3 tools/idf.py build
python3 tools/firmware_report.py
```

The default SDL dependency is system SDL2 **exactly 2.30.0** (Ubuntu 24.04 baseline).
Use `-DDAEMON_SYSTEM_SDL=OFF` to build the hash-pinned source release locally.
First configure needs network or the documented `.cache/downloads` archives.
Never install system packages or change system configuration without authorization.

## Verification, commits and completion

For substantive changes run appropriate domain/integration tests and the
architecture check. Run sanitizers for ownership, parsing and lifecycle changes.
Run shared-presentation and real SDL/LVGL headless scenarios for UI changes and inspect a screenshot.
Cross-build after changing shared headers, firmware adapters or presentation.
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

## Mandatory Habitat energy/resource policy

All services must be event-driven. No busy waits, unjustified polling, permanently
active idle worker threads, continuous needs updates or periodic unauthorized
inference. Block on bounded queues/timers; prefer physical interrupts when real
drivers exist. Tickless idle and ESP power management must remain enabled.
Initialize expensive peripherals lazily, declare the consuming service/resource
mask and finite wake constraints, and disable unused peripherals. Wi-Fi, GPS,
NFC, LoRa and the microphone must not become active without an explicit consumer.
Future nonessential reflection should favor charging and require scheduler approval.

Use `PowerManager` RAII leases; check failures, preserve generation-safe release,
exercise expiry and expose the owner in telemetry. No service may silently keep
the system awake. Storage operations, scheduled work and active cognition need
scoped ownership. Suspend/resume foreground lifecycle with the power plan.
Keep safe physical deep sleep behind checkpoint/rail/radio/wake qualification;
the current adapter reports unsupported. Never infer board current from desktop
process activity. Five days is the design goal and three days the desirable
minimum, both unverified until physical measurements.

Use `ResourceManager` for actual application buffers or declared fixed-arena
reservations, with explicit placement. Default pool caps are 128 KiB internal
and 6 MiB external; nominal 512 KiB/8 MiB is not free application memory. Respect
category caps, track peaks/rejections and test pressure cancellation/reclamation.
Critical ISR handoff buffers and task stacks stay internal; runtime/LVGL/SQLite
arenas use PSRAM. Do not declare a PSRAM buffer DMA-safe before driver validation.
SDK allocations are outside the ledger and need independent heap/stack telemetry.

Watch rendering is shared, partial and on demand. Stop watch flushes while asleep;
lab diagnostics must stay outside the product UI and firmware. Finite virtual-time
headless execution is a test harness, not a background service policy.

## Embedded/readiness and Awakening contracts

Use the pinned project-local SDK/tool manifest and Python lock, never global
installation. Run `tools/bootstrap_embedded.py --check` and `tools/idf.py build`.
Read `docs/embedded-build.md` before interpreting the ELF, NVS or VFS. Confirm
`.ext_ram.bss` placement with `tools/firmware_report.py`; text in sdkconfig alone
is insufficient. Persistent SD SQLite operations are deliberately unsupported.
NVS A/B minimum records are not a full portable creature export or a durability claim.

Use Clang static analysis for C++ (`tools/analyze.py`); GCC 13's analyzer is
C-only in its documented scope and is not the Habitat C++ gate. Keep first-party
warnings as errors. Document targeted third-party compatibility flags in ADRs.

Awakening v0.3.0 is the next milestone. Habitat uses bounded response schema v2,
legacy v1 compatibility, local configurable agent.md and persistent familiar name.
Speech may be null. Validate counts, UTF-8, numeric ranges and every capability
before effects. Personality/intention proposals remain denied execution in the
current creature app; adding a schema field does not grant permission. No real
provider or mandatory middleware belongs in Habitat. Keep the watch authoritative.
