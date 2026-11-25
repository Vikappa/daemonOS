# ADR 0005: scoped resource ownership and deadline-driven execution

Status: accepted, 2026-11-25.

Replace frame-driven service work with explicit events and bounded deadlines.
The platform blocks until an input or the earliest deadline. Simulated needs are
materialized at interactions/minute deadlines while awake and once on wake after
suspension. Keep the deterministic engine and original tests intact.

Use fixed resource categories/pools with move-only reservations and actual buffer
allocation through platform allocators. Reserve SDK/driver headroom rather than
budgeting nominal memory. A central power service owns finite RAII wake locks,
generation-safe expiry and per-service masks/callbacks. Foreground lifecycle
suspends with the display. Critical battery may override nonessential locks.

Continuous microphone/inference, unconditional radio enable, always-running
background threads and timer-polled touch are rejected. Headless finite virtual
time execution and the desktop-only diagnostics sampler are explicit exceptions
to real-time waiting, not firmware services.

Unknown technical reboot downtime and depleted battery must not punish health;
preserve departure health while materializing other needs. Keep the seven-day
catch-up bound from Genesis and document discarded excess time. Full trust of
physical RTC/wake behavior remains a hardware gate.
