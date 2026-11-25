# ADR 0001: portable runtime and desktop-first vertical slice

Status: accepted, 2026-11-25.

The board is unavailable. Implement and test offline domain behavior on Linux
before binding it to FreeRTOS peripherals. Choose C++20, fixed-capacity value
messages, injected clocks/randomness and small non-owning interfaces. Keep SDL,
LVGL and native SQLite outside `daemon_core`. Static applications have explicit
lifecycle and navigation; no dynamic executable loading.

Alternatives: implementing all peripheral services as stubs would create false
completeness; importing Arduino/LVGL into the domain would prevent simple tests.
A custom kernel, scripting runtime and mandatory cloud backend are rejected.

The desktop presentation is currently simulator-local and must become a shared
LVGL presentation component before physical firmware integration. HAL contracts
for unavailable devices are contracts only. Compile/link and hardware validation
are separate milestones. This limits Genesis scope while preserving one source
of domain logic and local authority.
