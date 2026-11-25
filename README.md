# DæmonOS

**The watch is the creature. The language model is its cognitive coprocessor.**

Genesis **v0.1.0** is an offline-first C++20 operating-environment foundation for
programmable wearables, initially targeting LilyGO T-Watch Ultra. A working Linux
LVGL 9 / SDL2 simulator is the primary development platform. No physical watch
peripheral or battery-life claim has been validated.

Actual simulator captures: [launcher](docs/screenshots/launcher.png),
[Ember](docs/screenshots/creature.png), [Genesis Lab](docs/screenshots/lab.png).

## Run on Ubuntu 24.04

Prerequisites: CMake >= 3.24, Ninja, GCC 13 or a compatible C++20 compiler, Python 3
and SDL2 development files at version 2.30.0. SQLite and LVGL are fetched as
hash-pinned source archives; no global SQLite installation is necessary.
If SDL2 development files are unavailable, add `-DDAEMON_SYSTEM_SDL=OFF` during
configuration to build its pinned source locally. System packages are never
installed by the project.

```sh
cmake --preset desktop
cmake --build --preset desktop
ctest --preset desktop
./build/desktop/daemon_sim
```

Two windows open: the **410 x 502 watch** and a separate **Genesis Lab** panel.
Open Ember, feed, pet or rest. Open Clock or Settings from Home. The lab can jump
one hour, accelerate time, inject steps/battery/charging, toggle simulated
connectivity, inject storage failures, suspend and wake. `H` goes home, `Esc`
goes back and `Space` wakes the display. Close either window to save and exit.
The clock displays UTC explicitly; host-local timezone and DST are future work.

The creature's identity and needs survive restart in `var/simulator/sd/creature.sqlite`.
A minimal independent checkpoint lives in `var/simulator/flash/identity.checkpoint`.
These are local equivalents of MicroSD and internal flash, not validated embedded
storage adapters. Copy/export personal data only deliberately; it is Git-ignored.

```sh
./build/desktop/daemon_sim --headless --frames 120 --epoch 1700000000 \
  --seed 42 --data-dir /tmp/daemonos-demo --screenshot /tmp/daemonos.bmp \
  --report /tmp/daemonos.json

cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
python3 tools/check_architecture.py
```

`--help` lists CLI options. UI scripts inject real SDL mouse events, not direct
domain mutations. See [the simulator guide](docs/simulator.md). The test suite
covers restart, offline catch-up, transactional rollback, export, corruption,
checkpoint recovery, cognitive validation and actual LVGL navigation.

## What Genesis implements

- Portable C++20 runtime, bounded events and services, static application registry,
  lifecycle and navigation; four independently registered applications.
- Original replaceable pixel familiar, efficient idle animation, local interactions
  and a deterministic needs engine with minute resolution and autonomous rest.
- Injectable clocks/randomness, virtual time, simulated device conditions and
  a power-policy state machine. Offline catch-up is capped at seven days.
- SQLite authority with two schema migrations, transactions, bounded memory pages,
  recency/importance/text/tag/entity retrieval, consistent export and recovery.
- Incremental mock cognitive responses, bounded context, request rate/timeout/
  cancellation/backoff and fail-closed capability validation. No network is needed.
- CMake presets, Linux CI, sanitizer configuration and architecture checks.

## Repository map and boundaries

```text
core/          portable domain, services, application runtime and desktop adapters
simulator/     LVGL presentation and SDL display/touch implementation
firmware/      ESP-IDF integration seed and documented board mapping (unvalidated)
android/       optional Kotlin/Compose bridge contract (not implemented)
protocol/      versioned protocol design, schemas and examples
assets/        original provisional sprite and replaceable visual resources
tests/         domain, persistence, faults and SDL/LVGL integration
tools/         architecture checks and resource inspection
docs/          architecture, ADRs, hardware evidence, verification and roadmap
```

The watch core never imports SDL, LVGL, SQLite or ESP-IDF. The UI sends events;
Android and real inference will be adapters. There is no mandatory phone, backend
or cloud. BLE/Wi-Fi transports, speech/audio, advanced autobiographical cognition,
OTA, full service supervision and physical drivers remain future work.

Read [architecture](docs/architecture/overview.md), [resource budgets](docs/architecture/resources.md),
[persistence/recovery](docs/architecture/persistence.md), [hardware evidence](docs/hardware/t-watch-ultra.md),
[verification results](docs/verification.md), [roadmap](docs/roadmap/roadmap.md) and
[AGENTS.md](AGENTS.md) before extending the system.

Source code and original assets are licensed under [MIT](LICENSE).
Dependencies retain their [upstream licenses](docs/dependencies.md).
