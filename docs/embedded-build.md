# ESP32-S3 cross-build

Habitat compiles and links the **same `daemon_core` and `daemon_presentation`**
used by the simulator. This is a real ESP-IDF firmware binary, not a validated
physical watch implementation. No flashing is part of the verification workflow.

## Local toolchain

On Ubuntu 24.04 with Python 3.12, Git, CMake, Ninja and a native C/C++ compiler:

```sh
python3 tools/bootstrap_embedded.py
python3 tools/bootstrap_embedded.py --check
python3 tools/idf.py build
python3 tools/idf.py size size-components
python3 tools/firmware_report.py
```

Bootstrap downloads ESP-IDF **v5.5.1**, commit
`fcae32885b0296b32044cb99ecbdc50d98dddb83`, including pinned submodules, into
`.cache/esp-idf`. Its tool manifest verifies the Xtensa and ULP tool archives.
The tested Xtensa GCC is **14.2.0 / esp-14.2.0_20241119**. Python dependencies
are version-locked in `tools/esp-idf-python.lock` and installed into a private
venv; the checker compares the complete installed package set. The wrapper
uses this project lock instead of downloading IDF's mutable daily constraint file.
Tools, pip cache and Python environment stay under `.cache/idf-tools` and
`.cache/pip`. No shell profiles, global Python or system packages are modified.
The initial download is substantial; network access is needed only for setup
and missing pinned dependency archives. Bootstrap refuses a mismatched existing
SDK rather than replacing it.

`tools/idf.py` supplies SDK/tool paths to a child process, validates the commit,
and directs all generated configuration/artifacts to `build/firmware`. No global
`export.sh` or `IDF_PATH` setup is needed. It accepts normal IDF actions, including
`reconfigure` and `menuconfig`. Existing configured values take precedence over
new defaults; CI starts with a fresh build directory.

## Components and board configuration

- `daemon_core`: the exact C++20 domain, framework, power and resource sources;
  exceptions and RTTI remain disabled.
- `daemon_presentation`: the exact shared watch controller/view and sprite asset.
- `lvgl`: the same hash-pinned LVGL 9.2.2, with the common `presentation/lv_conf.h`.
- `daemon_sqlite`: hash-pinned SQLite 3.50.4 with a bounded PSRAM heap and an
  explicit persistent-storage denial VFS. This is a compile/readiness gate.
- `main`: FreeRTOS event queue/waits, ESP power configuration, NVS minimum state,
  PSRAM allocation and unsupported peripheral adapters.

`esp_psram` is an explicit dependency. Without it, its Kconfig symbols are absent
from a minimal IDF build and external BSS attributes become ineffective. The
firmware report asserts the LVGL arena actually lands in `.ext_ram.bss`.
The native build uses `-std=c++20` specifically on C++ compilation, not C/assembly.

The 16 MiB partition table has NVS, OTA metadata, PHY data, **two 4 MiB OTA slots**,
a 0x7d0000-byte assets partition and a 64 KiB coredump partition. There is no OTA
updater or mounted assets/FatFS driver yet. The image must fit its slot.
QSPI PSRAM, tickless idle, automatic light-sleep support, a 32 KiB application
task stack and a 64 KiB internal allocator reserve are configured. Real board
rails, RTC, display orientation and memory timing require physical validation.

## Runtime behavior and limits

The runtime lives in static PSRAM, its LVGL arena in external BSS, and its partial
draw buffer/SQLite heap use the budgeted ESP allocator. The FreeRTOS handoff queue
and task stack use internal SRAM. The task blocks in `xQueueReceive` until an
event or the next actual runtime/presentation deadline. A CPU frequency lock
covers work and is released before blocking. IDF's automatic light sleep may
then operate when SDK locks and wake constraints permit.

NVS A/B records preserve minimum identity, needs, personality and settings with
version, ABI size, sequence and CRC checks. Initialization errors do not erase
NVS. This adapter is compiled, not physically validated; records are not a
portable export format and currently have no historical ABI migration.
Full autobiographical storage, history queries and exports return `unsupported`.
An interrupted or absent SD VFS cannot be mistaken for validated SQLite storage.

Display, audio, BLE, Wi-Fi, sensors and explicit deep-sleep requests reach
adapters that report `unsupported`. The view is built and rendered into a
partial buffer, but its physical flush is denied and acknowledged to LVGL
without sending anything to hardware. No synthetic GPIO driver or always-on
microphone is introduced. No touch interrupt, RTC alarm or radio connection is
configured. Deep-sleep policy is visible, while actual deep sleep is gated by
the unsupported board adapter; the task remains blocked and may use automatic
light sleep. It cannot yet be used as a watch.

The built-in `config/agent.md` is embedded as text and validated before context
use. Desktop accepts `--agent PATH` and `--name NAME`; naming stays in local
creature state. Real providers and peripheral actions remain Awakening work.

SQLite's upstream `NDEBUG` assumptions require
`CONFIG_COMPILER_ASSERT_NDEBUG_EVALUATE=n`: otherwise IDF evaluates SQLite debug
assert expressions whose debug-only variables do not exist. This does not turn
off application/SDK assertions. A third-party-only `-Wno-unused-value` handles
the literal `0` generated by `SQLITE_OMIT_WAL`. No first-party diagnostic is
suppressed. See ADR 0007 and the verification report for results and limitations.
