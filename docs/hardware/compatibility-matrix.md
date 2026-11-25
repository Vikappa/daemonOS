# Habitat hardware compatibility matrix

No physical board is available. “Cross-built” means compiled and linked for
ESP32-S3, never tested on a watch.

| Component | Desktop validation | ESP32-S3 cross-build | Physical status |
|---|---|---|---|
| C++20 domain/framework/power/resources | unit/integration/sanitizers | linked same sources | not run |
| LVGL four-app presentation | actual mouse/render tests | linked same view/controller | CO5300 flush unsupported |
| Partial draw buffer | real SDL output | PSRAM allocation code + map | QSPI/DMA unvalidated |
| PSRAM | budget model | external BSS verified in map | timing/rails unvalidated |
| SQLite 3.50.4 | native transactions/recovery/query limits | memory-only readiness probe linked | probe not executed on hardware |
| FatFS/SD SQLite VFS | faults modeled through adapter | persistent VFS explicitly denies files | locking/sync/durability unimplemented |
| Minimum checkpoint/settings | POSIX CRC recovery tested | NVS A/B adapter linked | interruption/wear unvalidated |
| FreeRTOS queue/tickless/ESP PM | deadline model tested | configured and linked | current/sleep behavior unmeasured |
| Display/touch, audio, IMU, GPS, NFC, LoRa, BLE, Wi-Fi | controls emulate conditions | gates/contracts | unsupported drivers |
| RTC and physical wake sources | virtual clock/time jumps tested | system-clock contract only | PCF85063A integration unsupported |
| Explicit deep sleep | policy/continuity tests | board operation denied | safe rails/wake not validated |

Board source remains the source-inspected T-Watch Ultra V1.0 mapping documented
in [the hardware evidence](t-watch-ultra.md). Header rotation and XL9555 expander
indexes are not interchangeable with portrait geometry or ESP GPIOs. Inspect the
actual revision/radio population before enabling any peripheral. Compilation
does not validate pin levels, reset sequencing, board ownership or battery life.
