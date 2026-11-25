# Roadmap

| Version | Name | Outcome |
|---|---|---|
| 0.1 | Genesis | Portable core, persistent offline creature, functioning LVGL/SDL simulator |
| 0.2 | Habitat | App framework, shared presentation, service supervision and resource capabilities |
| 0.3 | Instinct | Richer deterministic autonomy, events, scheduled intentions and environmental reactions |
| 0.4 | Memory | Autobiographical retrieval, recall accounting, summaries, relationships and personality evolution |
| 0.5 | Consciousness | Configurable OpenAI-compatible provider, production bounded parser, streaming cognitive runtime |
| 0.6 | Voice | Push-to-talk, measured audio buffers/codecs, STT/TTS adapters and speech animation |
| 0.7 | Bridge | Kotlin/Compose Android companion, authenticated BLE, framing and reconnect |
| 0.8 | Incarnation | ESP-IDF port, real drivers, board revision/orientation and SQLite/FatFS qualification |
| 0.9 | Endurance | Measured power optimization, interruption durability, OTA and recovery |
| 1.0 | DæmonOS | Stable, documented and physically validated wearable release |

## Recommended next milestone: Habitat

Extract simulator-local LVGL presentation into a platform-neutral UI library;
keep SDL/ESP display drivers separate. Make modules own their view controllers
and event subscriptions. Expand application capability/resource leases, service
startup/shutdown dependencies and a real clock-based scheduler. Add settings
schema evolution and safe media reconnection. Review UI accessibility and touch
target sizing on real display scale. Keep the existing simulator tests as gates.

## Open issues / technical debt

- No physical board or ESP-IDF SDK/toolchain in the current environment; firmware
  integration seed is not compiled or flashed. Actual radio population/revision
  and display transforms remain unknown.
- Native SQLite passes host tests, but no FatFS SQLite VFS or power-loss durability
  is qualified. Minimal checkpoint is POSIX-only and not encrypted. Add NVS A/B
  slots, write-wear strategy and explicit verified import/export UX.
- Memory scans can grow with history despite bounded result pages. Recall counters,
  memory links, summaries, personality evolution and non-state schema entities
  need real services. The pending RAM memory queue is intentionally lossy on crash.
- UI rebuilding is small but simulator-local. No asset installation layout,
  runtime theme system, sprite variants or language/timezone settings yet.
- Audio, VAD/codecs, networking, NimBLE, Android, OTA, credential storage and
  production authentication are not implemented. Contract documentation is not
  an implementation claim.
- Mock cognition demonstrates budgets/validation, not cognitive intelligence.
  Its action schema is intentionally narrower than the long-term protocol.
- Host memory/RSS and simulated power decisions do not establish firmware fit,
  frame latency on QSPI, radio throughput or battery life.

See `docs/verification.md` for the checks actually executed in this environment.
