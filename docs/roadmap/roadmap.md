# Roadmap after Habitat

| Version | Name | Outcome |
|---|---|---|
| 0.1 | Genesis | Working persistent offline desktop creature and portable runtime |
| 0.2 | Habitat | Shared presentation, bounded power/resources, event-driven waits and ESP32-S3 cross-build |
| 0.3 | Awakening | Real interchangeable inference, configurable identity/personality and embodied context |
| 0.4 | Memory | Richer autobiographical retrieval, summaries, relationships and recall accounting |
| 0.5 | Agency | Validated planning, intentions and resource-aware proactive behavior |
| 0.6 | Voice | Measured push-to-talk/audio codecs and interchangeable STT/TTS |
| 0.7 | Bridge | Optional Kotlin/Compose companion and authenticated BLE transport |
| 0.8 | Incarnation | Real board drivers, RTC/wake and SQLite/FatFS qualification |
| 0.9 | Endurance | Physical power optimization, interruption reliability, OTA/recovery |
| 1.0 | DæmonOS | Stable physically validated wearable release |

Habitat's software criteria are implemented and exercised on desktop plus a
real Xtensa cross-build. Physical behavior is explicitly outside that evidence.
The hardware matrix and verification report enumerate unsupported operations.

## Next: Awakening v0.3.0

Implement a configurable OpenAI-compatible provider behind `InferenceProvider`;
validate actual endpoint/model/streaming combinations, including local engines.
Keep identity, memory, agent configuration, body/emotion context and scheduling
on the watch. No application server, phone database or mandatory middleware.
Add protected credentials, TLS/authentication, bounded cancellation/backoff,
provider-independent errors and request/cost priorities. Background reflection
must be authorized and favor charging; local instincts remain independent.

Evolve the v2 proposal contract into personality/intention policies with explicit
capabilities and auditable local acceptance. Add context relevance tests, indexed
retrieval/retention, memory recall counters and safeguards against prompt injection
through recalled content. Speech remains optional. Do not let a provider directly
set identity, permissions, filesystem paths or SQL.

## Remaining debt and hardware gates

- Physical display/touch/PMIC/IMU/RTC/GNSS/NFC/radios/audio drivers and explicit
  deep sleep remain unsupported. Automatic IDF light sleep is configured, not
  physically measured; no three/five-day autonomy claim is valid yet.
- SQLite is cross-built with memory-only readiness code. A persistent FatFS VFS,
  locking, sync, power interruption, media replacement and SD history are absent.
- NVS minimum records are ABI/version gated, not portable full exports; old ABI
  migration, encryption, measured wear and brownout qualification remain work.
- Native memory queries have bounded output and VM work; large scans may return
  a budget error. Add indexes/FTS and retention rather than lifting limits blindly.
- Buffered experiences can be lost on process death. Minimal checkpoints omit
  history, catch-up remains capped at seven days and clock trust is unresolved.
- Response parsing rejects Unicode escape sequences; real providers need a
  reviewed full bounded JSON interoperability strategy. Personality/intentions
  are validated proposals only, denied execution in the current creature app.
- Asset installation packaging, accessibility/timezone settings, service-specific
  SDK sleep locks, stack/heap high-water traces and production telemetry remain.

See [verification](../verification.md), [hardware matrix](../hardware/compatibility-matrix.md)
and [the parametric energy model](../architecture/energy-budget.md).

## Integrated assistant extension

Durable tools/agent workflows, provider-neutral service catalog, approved LCD masks
and operational faces, playback mouth motion and the local voice directory are
implemented on Habitat's resource and shared-view architecture. These do not make
the real inference/audio/Android/authentication adapters or physical watch complete.
See architecture/voice-directory.md and architecture/agent-tasks.md.
