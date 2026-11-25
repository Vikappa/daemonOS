# Habitat v0.2.0 implementation plan

Baseline: Genesis commit `69d8c68`. Preserve all existing domain and integration
tests and evolve the working runtime rather than replacing it.

1. Inspect repository evidence and official ESP-IDF/LVGL documentation. Install
   a pinned ESP-IDF SDK/toolchain only in the ignored project cache.
2. Introduce bounded RAII resource reservations and wake locks, service resource
   declarations, central power coordination and application suspend/resume.
3. Replace frame-driven domain updates with monotonic deadlines, explicit events,
   blocked platform waits and elapsed-time catch-up. Stop watch rendering asleep.
4. Extract the actual watch view/controller into one shared LVGL presentation
   library; keep SDL, asset loading and lab controls in desktop adapters.
5. Cross-build that same runtime/presentation on ESP32-S3 using PSRAM-aware
   allocation, NVS checkpoints, explicit unsupported peripherals, tickless idle,
   a 16 MiB partition layout and an experimental SQLite build/VFS readiness gate.
6. Add a versioned bounded Awakening response/context contract, local agent.md
   configuration and configurable creature naming without real inference.
7. Expand lab telemetry/fault controls and add deterministic suspension, resource,
   power, shared-presentation and cognitive-contract tests.
8. Run Debug, Release, embedded cross-build, sanitizers, static analysis and a
   graphical smoke run; inspect embedded size reports and memory maps.
9. Update architecture/ADRs, hardware matrix, resource and parametric energy
   budgets, roadmap (Awakening v0.3.0), verification evidence and AGENTS.md.
10. Create a local commit only after verification. No remote or push.

Hardware behavior and durability remain unvalidated. Stub operations must return
`unsupported`; compiling a component is not evidence that a physical driver works.
Record any unmet acceptance criterion explicitly rather than declaring completion.

## Completion evidence

Implemented shared watch presentation, finite RAII power/resource ownership,
deadline-driven runtime/platform waits, pressure handling, bounded database work,
local naming/agent configuration and response v2. All original Genesis tests are
preserved. Debug, source-SDL Release and full ASan/UBSan/LeakSanitizer each pass
4/4 CTest entries; C++ coverage totals 25 cases / 1069 checks. Clang analyzes 17
first-party units. Xtensa links the common runtime/view and SQLite; the firmware
map and OTA slot checks pass. X11 windows and screenshots were exercised/reviewed.
See `verification.md` for actual memory sizes and unsupported hardware gates.
The final local commit follows these checks; no remote or push is part of Habitat.
