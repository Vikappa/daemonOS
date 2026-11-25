# ESP-IDF integration seed — unvalidated

This is not working T-Watch Ultra firmware. It is a small FreeRTOS integration
seed importing exactly the desktop domain sources through an ESP-IDF component.
The entrypoint advances the shared needs engine without board I/O. It generates
ephemeral state and must not be used as a persistent creature installation.

Baseline candidate: ESP-IDF **v5.5.1**, ESP32-S3, C++20, QSPI PSRAM. No ESP-IDF
SDK/toolchain is available in the Genesis environment, so this path has not been
compiled, flashed or hardware-tested. Even sdkconfig defaults are provisional.
Do not assume the Arduino-oriented LilyGoLib is a native-IDF drop-in.

When the SDK is deliberately installed outside this workflow:

```sh
cd firmware
idf.py set-target esp32s3
idf.py build
```

No flashing command is part of Genesis validation. Before a real device build,
provide RTC/clock and randomness qualification, NVS checkpoint, tested MicroSD
store/VFS, display/touch driver, PMIC sequencing and single-owner task wiring.
Review memory/stack/partition maps and radio revision. The board pin contract is
source-inspected only; see `../docs/hardware/t-watch-ultra.md`.

[Official C++ support](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/cplusplus.html)
documents compiler support and exceptions being disabled by default. The core
does not require exceptions or RTTI; native desktop SQLite exception translation
is not included in this firmware component.
