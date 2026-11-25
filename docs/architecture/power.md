# Central power and event execution

`PowerManager` owns a bounded service registry and 16 RAII wake-lock slots.
Each service declares its allowed resource mask and maximum lease duration.
The plan coordinates display, audio, BLE, Wi-Fi, storage, sensors, scheduled
work, application lifecycle and CPU activity. Registered callbacks are invoked
on transitions; a resource shared with CPU does not accidentally enable an
unrelated physical peripheral.

States: DeepSleep, Standby, Interactive, Listening, Thinking, Speaking and Charging.
Standby retains a requested connection; deep sleep drops radios. Charging does
not require an illuminated display. At 60 seconds idle, the display and foreground
application suspend unless a blocking lock exists. Critical battery overrides
nonessential work. The current adapters model these effects on desktop and report
unsupported physical operations on embedded.

Wake locks identify a service, exact resources, acquisition time, deadline and
whether they prevent suspension. They are move-only; destruction/reset releases
ownership. Generation tokens prevent an expired handle from releasing a reused
slot. Deadlines are enforced and expired owner/count remain inspectable. Leases
are finite (typically <=30 s, audio/application <=120 s, cognition <=15 s).
Storage transactions/retrieval/exports acquire storage locks; cognition reserves
memory and a CPU/transport lock and releases both on completion, rejection,
timeout, pressure or disconnect. Scheduler callbacks execute under a scoped lock.
Callbacks must be bounded and must not busy-wait or block the owning runtime.

The creature advances on explicit interactions or minute deadlines while awake.
No task runs a needs loop during suspension. Before sleep, state/checkpoint are
flushed once. On wake, elapsed monotonic time is applied in bounded minute quanta.
Boot uses wall-clock catch-up; backward corrections contribute zero time.
Reboot/critical-battery technical downtime preserves departure health while
other needs and age progress. Catch-up still has the documented seven-day cap;
time beyond that cap is discarded. There is no permanent creature death policy.

The runtime exposes its next deadline. Desktop waits in `SDL_WaitEventTimeout`;
touch reads are triggered by input events rather than an LVGL polling timer.
The watch uses explicit partial refresh only for changed state/animation/time.
The refresh timer is paused; asleep updates produce **zero watch flushes**.
Idle animation runs around 4.5 updates/s, static/sleeping/time views at most 1/s,
and input can force immediate refresh. The separate lab samples diagnostics at
2 Hz; this work is not compiled into the firmware. Finite headless tests advance
a virtual clock without sleeping; that is a test harness, not an idle service.

On embedded, a static FreeRTOS queue blocks until an event or a deadline.
`CONFIG_FREERTOS_USE_TICKLESS_IDLE` and `CONFIG_PM_ENABLE` are enabled, and
`esp_pm_configure` requests automatic light sleep. A CPU-frequency lock is held
only during work. Real drivers must acquire their own SDK clock/no-light-sleep
locks when DMA, audio clocks or radio timing require them; no such driver is
claimed implemented. SDK locks, active work, pending writes, task deadlines and
configured wake sources can prevent light sleep. Explicit deep sleep additionally
requires a successful checkpoint, stopped transports, safe rails and verified
wake sources. The current board adapter denies that transition as unsupported.

Only owner-triggered mock requests run today. No periodic inference is scheduled;
future nonessential reflection must be authorized by `CognitiveScheduler` and
prefer charging. Microphone, Wi-Fi, GPS, NFC and radio remain off without a
consumer. Desktop process CPU/RSS are not ESP32 energy measurements.

Sources: [ESP-IDF power management](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/power_management.html),
[sleep modes and radio continuity](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/sleep_modes.html).
