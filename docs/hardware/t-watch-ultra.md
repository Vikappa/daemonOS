# T-Watch Ultra hardware evidence

Reviewed for Genesis on **2026-11-25**. No physical hardware is available. This
document distinguishes vendor claims, source inspection and physical validation.

The [official product documentation](https://wiki.lilygo.cc/products/t-watch-series/t-watch-ultra/)
lists ESP32-S3, 16 MB QSPI flash, 8 MB QSPI PSRAM, a 410 x 502 CO5300 AMOLED,
CST9217 touch, BHI260AP motion sensor, MIA-M10Q GNSS, ST25R3916 NFC, T3902 PDM
microphone, MAX98357A I2S amplifier, DRV2605 haptic driver, AXP2101 PMIC,
PCF85063A RTC and XL9555 expander. Battery and MicroSD claims are 1100 mAh and
up to 32 GB FAT32. These are specifications, not measurements.

The official schematic link identifies **V1.0**, file
`T-Watch Ultra V1.0 SCH 25-07-24.pdf`. Vendor software distinguishes radio options
SX1262, SX1280, CC1101, LR1121 and SI4432. Not every option is a LoRa radio.
No later physical PCB revision was established in this review; inspect actual
silkscreen, module population and schematic before porting. ESP32-S3's 512 KiB
internal SRAM is a chip-level budget, not entirely free application memory.

Source inspection is pinned to LilyGoLib commit
`9ebd8096a10a149374b90bf933460b3a6a5e940d`:

- [Variant pin header](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/9ebd8096a10a149374b90bf933460b3a6a5e940d/variants/lilygo_twatch_ultra/pins_arduino.h).
- [Board interface and I2C addresses](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/9ebd8096a10a149374b90bf933460b3a6a5e940d/src/LilyGoWatchUltra.h).
- [Official schematic](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/9ebd8096a10a149374b90bf933460b3a6a5e940d/schematic/T-Watch%20Ultra%20V1.0%20SCH%2025-07-24.pdf).

## Mapping cross-check

| Function | ESP32 GPIO(s) |
|---|---|
| I2C SDA / SCL | 3 / 2 |
| Shared SPI MOSI / MISO / SCK | 34 / 33 / 35 |
| SD CS | 21 |
| RTC / touch / PMIC IRQ | 1 / 12 / 7 |
| NFC CS / IRQ | 4 / 5 |
| BHI260 IRQ | 8 |
| PDM clock / data | 17 / 18 |
| I2S BCLK / WCLK / DOUT | 9 / 10 / 11 |
| GPS UART TX / RX / PPS macro | 43 / 44 / 13 |
| Radio CS / reset / busy / IRQ | 36 / 47 / 48 / 14 |
| Display CS / SCK / reset / TE | 41 / 40 / 37 / 6 |
| Display data 0 / 1 / 2 / 3 | 38 / 39 / 42 / 45 |
| BOOT button | 0 |

XL9555 expander indexes **are not ESP32 GPIOs**: haptic enable 6, display enable
7, touch reset 8, SD detect 10, radio RF switch 11. The board header lists I2C
addresses touch 0x1A, BHI260 0x28, DRV2605 0x5A, AXP2101 0x34, XL9555 0x20,
RTC 0x51. UART TX/RX names must be checked at the MCU/device boundary before use.

The source pin header declares `DISP_WIDTH=502`, `DISP_HEIGHT=410`; the product
specification describes portrait **410 x 502**. Genesis deliberately uses portrait;
driver rotation, touch transforms, CO5300 offsets and TE synchronization must be
verified together on the board. Do not silently copy the header's orientation.

The main library currently documents Arduino-ESP32 3.x, while its separate
PlatformIO route uses Arduino 2.0.17 / ESP-IDF 4.4.7. This is not evidence that
LilyGoLib drops into the chosen ESP-IDF baseline unchanged. Prefer native IDF
drivers; use Arduino only behind an adapter if an integration experiment proves
necessary, with an ADR and pinned versions.

## Hardware acceptance gate

Inspect revision and radio, then verify rails/reset sequence, I2C discovery,
display/touch orientation, shared SPI arbitration, SD remove/power-loss behavior,
audio DMA and microphone default-off, sensor firmware loading, RTC sleep/wake,
BLE reconnection and current in every power state. Record board photos, build
revision, firmware, instruments and raw results. No such test has been run.
