# Genesis v0.1.0 implementation plan

The watch owns the creature. All essential behavior must remain available offline.
The initial deliverable is a Linux desktop vertical slice using the same portable
C++20 domain and application runtime intended for a future ESP-IDF port.

1. Inspect the environment and official hardware/library documentation.
2. Pin and hash local dependency downloads; configure CMake, presets and CI.
3. Implement bounded events, service registration, application lifecycle/navigation,
   injectable clocks and HAL contracts without SDL, SQLite or ESP-IDF in the core.
4. Implement deterministic needs, local interactions, power decisions and a bounded
   mock cognitive path with explicit capabilities and request budgets.
5. Implement transactional SQLite persistence, migrations, bounded memory queries,
   backup, minimal flash-equivalent checkpoint and degraded storage behavior.
6. Build a 410 x 502 LVGL/SDL watch and a separate simulator debug window. Connect
   controls through the same event path used by applications and simulated HAL.
7. Exercise domain/framework tests, persistence/restart/fault integration tests,
   headless UI automation and a graphical smoke run. Run desktop sanitizers.
8. Record actual results, architecture decisions, hardware verification sources,
   resource budgets, known limits, roadmap and future-agent instructions.
9. Create a local Git commit using the existing identity. No remote or push.

## Scope boundaries

No physical hardware is available. No peripheral, battery lifetime, embedded
SQLite/FatFS durability, Android app, BLE authentication, real speech or real LLM
connection will be claimed validated. These remain explicit contracts or roadmap
items. Prioritize a coherent working simulator over empty subsystem scaffolds.

## Verification targets

- Normal Linux debug build and deterministic CTest suite.
- Separate ASan/UBSan build of first-party C++ targets and dependencies where practical.
- Real LVGL rendering through SDL's dummy driver, mouse events and saved screenshots.
- Restart identity/needs continuity; storage fault and checkpoint recovery.
- Offline behavior and rejection of unauthorized cognitive actions.

## Completion evidence

The implementation steps above produced the portable runtime, four static apps,
transactional state store, bounded mock cognition and functioning two-window
simulator. Debug and Release/source-SDL builds, 16 domain/integration cases with
952 checks, SDL/LVGL restart/fault scenarios, full ASan/UBSan/LeakSanitizer,
portable-core GCC analysis and an X11 graphical smoke run passed locally.
See `verification.md` for actual commands, resource observations and limits.
Final source and documentation are recorded in the initial local Git commit;
no remote or push is part of this plan.
