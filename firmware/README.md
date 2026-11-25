# DæmonOS Habitat ESP32-S3 firmware

This firmware cross-builds with ESP-IDF v5.5.1 and links the same C++20 runtime
and LVGL watch presentation as the desktop. It is not yet usable on a physical
T-Watch Ultra: display/touch/audio/radio/sensor and explicit deep-sleep drivers
report unsupported. No flashing or hardware validation is claimed.

```sh
# From repository root; project-local SDK/toolchain/venv only:
python3 tools/bootstrap_embedded.py
python3 tools/idf.py build
python3 tools/firmware_report.py
```

Artifacts: `build/firmware/daemonos_firmware.bin`, ELF/map, partition table,
bootloader and clean `size-summary.json` / `size-components.json` evidence.
The image has two 4 MiB OTA slots in 16 MiB flash; an OTA updater is not present.
QSPI PSRAM contains the LVGL arena, runtime and budgeted draw/SQLite buffers.
FreeRTOS blocks on a static event queue/deadlines with tickless idle and configured
ESP power management. Actual low-power behavior requires measurements.

NVS A/B records retain minimum identity/vitals/personality/settings. SQLite is
compiled with a memory-only readiness probe and a persistent-file denial VFS.
SD history, locking/sync/durability and RTC/wake integration remain unimplemented.
No NVS erase is used to mask initialization errors. `config/agent.md` is embedded
as local bounded guidance. The physical GPIO/expander mapping remains source
inspection evidence, never a validated driver.

See [full build/adapter details](../docs/embedded-build.md),
[hardware matrix](../docs/hardware/compatibility-matrix.md),
[power policy](../docs/architecture/power.md) and
[actual verification](../docs/verification.md).
