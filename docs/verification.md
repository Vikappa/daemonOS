# Habitat and assistant integration verification

Date: 2026-11-25. Integration combines remote Habitat `9dc6fce` and the approved-face update `d4fdb4b` with assistant,
scheduler, LCD/speech and voice-directory work through `258abdd`. Historical evidence
is preserved in [Habitat](verification-habitat.md), [assistant](verification-assistant.md)
and [Genesis](verification-genesis.md) reports. Those baseline measurements are not
claims about the integrated firmware image.

## Current host verification

- Workspace-local CMake build: `cmake --build build/check -j 4`.
- `ctest --test-dir build/check --output-on-failure`: **8/8 suites pass**,
  including all prior domain/tool/agent/voice/portrait checks, Habitat power/resource
  and nullable v2 contract tests, actual SDL restart/face rendering and three-day
  shared-view suspension/pressure tests. Added integration assertions verify
  Habitat v2 plus tool calls, cognitive memory reclamation and wake-lock release.
- `cmake --build build/sanitize-check -j 4` and
  `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize-check --output-on-failure`:
  **6/6 non-GUI suites pass**, AddressSanitizer and UBSan. LeakSanitizer is disabled
  in this traced workspace; no leak-detection or GUI-sanitizer claim is made here.
- `python3 tools/check_architecture.py` checks portable core/shared-view boundaries
  and protocol JSON. `git diff --check` checks the merge resolutions.
- Actual 410 x 502 LCD screenshot was inspected after rendering through the shared
  LVGL view. Launcher/familiar/lab screenshots are refreshed from SDL captures;
  rendering masks, activity states and mouth motion are also covered by pixel tests.

The integrated host Runtime occupies 37,400 bytes, VoiceDirectory 648, Event 12,
MemoryPage 7,304 and ToolPage 4,904. These are host ABI values, not ESP SRAM/stack
measurements. Runtime/task category cap is now 96 KiB, allowing the 32 KiB internal
task claim and larger external runtime together; pool caps remain 128 KiB internal
and 6 MiB external. Empty unsupported/idle ledgers add no periodic wake deadline.
Scheduled storage/cognition and physical audio hints own finite resource leases.

Real speaker/audio/authentication/key-protection adapters, APK service mappings,
complete enrollment/approval UI, physical board operation and battery endurance
remain unimplemented or unvalidated. No live voice enrollment or real external
message was performed by these tests.

Clang 19.1.7 static analysis passes for all **22** first-party core, desktop,
shared-presentation and simulator translation units via `tools/analyze.py` using
the actual build/check compilation database. Compiler packages were extracted
locally from Debian trixie package archives and checked against the archive
SHA-256 metadata; no system configuration or global package was changed.
JSON Schema definitions and both legacy-tool / Habitat v2 examples validate.

## Integrated firmware verification

`python3 tools/bootstrap_embedded.py --check`, `python3 tools/idf.py build`
and `python3 tools/firmware_report.py` pass with pinned ESP-IDF v5.5.1 and Xtensa
GCC 14.2.0. The SDK/toolchain/Python environment were installed in the project
cache; no global installation or flashing was performed. Project-local CMake and Unix Makefiles
were used for this cross-build. Host tests, sanitizer checks and analysis were repeated
after integrating the latest approved-face commit.

The final binary is **971,136 bytes**, within each 4,194,304-byte OTA slot. Map
sections include 560,880 bytes external BSS, 15,496 bytes internal data, 4,496 bytes
internal BSS, 62,523 bytes IRAM text plus 1,028 bytes vectors. The report confirms
shared core/view linkage and actual external placement of the LVGL arena.
[Integrated machine-readable report](verification/assistant-firmware-size.json).
Physical display/audio/biometric/SD durability and battery behavior remain untested.

Cross-build checks found and corrected Xtensa's distinct `int32_t` type in clamp
and uint32 formatting. The latest shared presentation uses the canonical public
cat-mask header and includes all operational faces/mouth poses; no retired body
asset or sprite-generation dependency is restored.
