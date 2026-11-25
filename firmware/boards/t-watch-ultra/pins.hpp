#pragma once
// Source-inspected, NOT physically validated. See docs/hardware/t-watch-ultra.md.
namespace daemonos::board::t_watch_ultra {
inline constexpr int portrait_width=410, portrait_height=502;
namespace gpio {
inline constexpr int i2c_sda=3, i2c_scl=2;
inline constexpr int spi_mosi=34, spi_miso=33, spi_sck=35, sd_cs=21;
inline constexpr int rtc_irq=1, touch_irq=12, pmic_irq=7, sensor_irq=8;
inline constexpr int nfc_cs=4, nfc_irq=5;
inline constexpr int pdm_clock=17, pdm_data=18;
inline constexpr int i2s_bclk=9, i2s_wclk=10, i2s_dout=11;
inline constexpr int gps_tx=43, gps_rx=44, gps_pps=13;
inline constexpr int radio_cs=36, radio_reset=47, radio_busy=48, radio_irq=14;
inline constexpr int display_cs=41, display_sck=40, display_reset=37, display_te=6;
inline constexpr int display_d0=38, display_d1=39, display_d2=42, display_d3=45;
inline constexpr int boot=0;
}
namespace xl9555 {
inline constexpr int haptic_enable=6, display_enable=7, touch_reset=8;
inline constexpr int sd_detect=10, radio_rf_switch=11;
}
}
