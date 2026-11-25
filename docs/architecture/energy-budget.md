# Parametric energy budget

Design objective: **five days** under moderate use; desirable minimum: **three
days**. Neither is a measured result. The manufacturer's nominal battery claim
is **1100 mAh**; available capacity depends on age, temperature, regulator loss
and cutoff voltage. No whole-board current measurement or manufacturer current
profile has been established for this firmware.

`python3 tools/energy_budget.py` computes a reproducible hypothetical profile:

| State | Assumed current | Assumed minutes/day |
|---|---:|---:|
| Standby/background | 5 mA | 1408 |
| Interactive display/application | 80 mA | 20 |
| Thinking/transport | 180 mA | 5 |
| Speaking | 100 mA | 5 |
| Storage work | 50 mA | 2 |

All currents and durations in this table are **design assumptions**, not vendor
current specifications or simulated electrical measurements. States are mutually
exclusive bins here; a more detailed model must account for concurrent rails.
With assumed usable fraction 0.8, usable capacity is 880 mAh, average assumed
current 7.0417 mA and projected duration **5.2071 days**. This projection is not
an autonomy claim. A 5 mA baseline that proves too optimistic invalidates it.

The formula is `days = usable_mAh / (24 * average_mA)`, where average current is
the duration-weighted sum. At the same assumed usable capacity, five days require
<=7.3333 mA average and three days <=12.2222 mA. Change any current, duration,
capacity or fraction through CLI flags; invalid/overlapping day budgets fail.

The simulator supplies power states and duty-cycle observations, not current.
No desktop timing, RSS or GPU behavior is substituted for chip consumption.
The only manufacturer value used numerically is nominal capacity from the
[official product specification](https://wiki.lilygo.cc/products/t-watch-series/t-watch-ultra/).

Hardware qualification must measure battery-side current in deep sleep,
connected standby, representative OLED luminance, sensing, Wi-Fi/BLE inference,
audio, SD writes and charging transitions. Capture wake spikes, regulator and
PMIC overhead, PSRAM retention, RTC/sensor leakage and actual daily use patterns.
Record instrument, board/radio revision, firmware commit, settings, raw traces,
usable capacity and error bounds before accepting a three/five-day result.
