# DæmonOS

**The watch is the creature. The language model is its cognitive coprocessor.**

**Habitat v0.2.0** is an offline-first C++20 wearable foundation targeting LilyGO
T-Watch Ultra. Linux and ESP32-S3 now compile the **same core and LVGL watch
presentation**. The simulator works; the ESP-IDF firmware cross-builds. Physical
watch drivers, sleep behavior and battery endurance remain unvalidated.

Screenshots: [watch](docs/screenshots/creature.png),
[launcher](docs/screenshots/launcher.png), [separate lab](docs/screenshots/lab.png).

## Desktop: Ubuntu 24.04

Prerequisites: CMake >=3.24, Ninja, GCC 13-compatible C++20, Python 3.12 and SDL2
2.30.0 development files. SQLite 3.50.4 and LVGL 9.2.2 are hash-pinned local source
builds. Add `-DDAEMON_SYSTEM_SDL=OFF` to build pinned SDL2 locally as CI does.
No system packages are installed by the project.

```sh
cmake --preset desktop
cmake --build --preset desktop
ctest --preset desktop
./build/desktop/daemon_sim
```

Open Familiar, Clock or Settings. Feed, pet and rest work offline. The familiar
remains **Ember** by default; identity and memories belong to the local device.
Two windows show the 410 × 502 watch and a separate 460 × 960 Habitat Lab.
`H` opens Home, `Esc` goes back, `Space` wakes the display. Close either window
to save. The clock explicitly uses UTC.

```sh
./build/desktop/daemon_sim --name Nyx --agent config/agent.md
./build/desktop/daemon_sim --headless --frames 240 --epoch 1700000000 \
  --seed 42 --data-dir /tmp/daemon-habitat --report /tmp/habitat.json \
  --screenshot /tmp/watch.bmp --debug-screenshot /tmp/lab.bmp
```

The lab injects time, battery/charging/motion, connection/storage faults, memory
pressure, finite peripheral leases and sleep/wake. It shows service/wake-lock
ownership, memory reservations/peaks, queue use, FPS, store operations, cognitive
lifecycle and wake history. Developer controls stay outside the watch UI.

## ESP32-S3: project-local SDK and tools

```sh
python3 tools/bootstrap_embedded.py
python3 tools/idf.py build
python3 tools/firmware_report.py
```

The pinned SDK/toolchain/venv live in `.cache`; artifacts live in `build/firmware`.
The build produces `daemonos_firmware.bin` and an ELF/map, with 16 MiB flash,
dual 4 MiB OTA slots, QSPI PSRAM, FreeRTOS tickless idle and ESP power management.
See [embedded build instructions](docs/embedded-build.md) before using it.

The firmware links the shared runtime/view, NVS minimum-state adapter and SQLite
readiness component. Persistent SD SQLite access and physical display/input,
audio, radio, sensors and explicit deep sleep are **unsupported**. No flashing
or hardware operation has been validated. It cannot yet function as a watch.

## Habitat behavior

- The original four applications, deterministic needs, sprite, interactions,
  native SQLite authority, migrations, pagination, export and recovery remain.
- One view/controller implementation serves both targets. Partial redraw uses
  bounded buffers and changed regions; the watch performs no flushes asleep.
- Deadline-driven execution replaces unconditional frame work. Platforms block
  when idle; the creature catches up from elapsed time on wake/reboot.
- A central power service coordinates service masks, application suspension and
  finite generation-safe RAII wake locks with expiry diagnostics.
- Resource categories/pools enforce caps, headroom and allocation reservations;
  memory pressure cancels cognition, denies optional reservations and trims cache.
- Versioned Awakening response v2 supports silence, expression/voice parameters,
  memory candidates and future personality/intention proposals. Outputs remain
  untrusted and capability checked. Only the streaming mock is implemented.

Desktop state is in `var/simulator/sd/creature.sqlite`, with a minimum independent
checkpoint in `var/simulator/flash/identity.checkpoint`. Both are Git-ignored.
The firmware currently uses NVS for minimum state/settings, not full history.
The local [agent configuration](config/agent.md) is optional guidance, not an
external source of authority. No phone, server, cloud or model is mandatory.

## Verification and structure

```sh
cmake --preset release -DDAEMON_SYSTEM_SDL=OFF
cmake --build --preset release
ctest --test-dir build/release --output-on-failure
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
python3 tools/analyze.py --build-dir build/desktop
python3 tools/check_architecture.py
python3 tools/energy_budget.py
```

`core/` owns domain/framework/services, `presentation/` the shared LVGL watch,
`simulator/` SDL and lab, `firmware/` ESP-IDF adapters, `protocol/` versioned
contracts, `config/` agent guidance, and `tests/` deterministic regression/fault
checks. Android remains an optional integration contract.

Read [actual verification results](docs/verification.md),
[architecture](docs/architecture/overview.md), [power policy](docs/architecture/power.md),
[resource budgets](docs/architecture/resources.md), [persistence/recovery](docs/architecture/persistence.md),
[hardware matrix](docs/hardware/compatibility-matrix.md), [roadmap](docs/roadmap/roadmap.md)
and [AGENTS.md](AGENTS.md). Genesis evidence is [archived](docs/verification-genesis.md).

The five-day design objective and three-day desirable minimum are hypotheses,
not battery results. The [parametric model](docs/architecture/energy-budget.md)
labels every assumed current and separates them from physical measurements.

Source and original assets use [MIT](LICENSE); dependencies retain their
[upstream licenses and pinned versions](docs/dependencies.md).

## Assistant, expressions and voice directory

Integrated with the Habitat runtime: sarcastic Italian default personality, durable
notes/reminders, scheduled agent workflows and provider-neutral bridge interfaces,
approved LCD cat masks, activity faces and playback-driven mouth animation. The
shared watch view is used by both desktop and firmware; it stops rendering asleep.

The local voice directory has owner/authorized/guest roles and timestamped recurring
guest encounters. Familiarity never grants permissions. Voice-only sessions cannot
read private history or approve messages. Tool/task actors and owner approvals are
checked again before execution. Real audio encoding, local authentication, template
encryption/key storage, APK mappings and enrollment/approval UI remain integrations.
See [voice directory](docs/architecture/voice-directory.md),
[agent workflows](docs/architecture/agent-tasks.md) and
[activity faces](docs/adr/0007-operational-faces-and-speech.md).
