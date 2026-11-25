# ADR 0002: bounded elapsed time and conservative persistence

Status: accepted, 2026-11-25.

Needs use minute quanta with a persisted millisecond remainder. Live elapsed
time comes from monotonic clocks; boot catch-up comes from wall time, clamped
to seven days. Negative wall deltas contribute no time, and a new baseline
allows subsequent progress. This gives deterministic partitioning and bounded
catch-up work without requiring an always-running CPU. Second-resolution age
and unbounded neglect simulation are deferred.

Choose SQLite behind `StateStore` for desktop, DELETE journal and FULL sync.
Normalize needs/personality and commit them with optional memories. Use versioned
transactions for migrations. Batch vitals and experiences every virtual minute;
keep a CRC-protected minimal independent checkpoint at five-minute intervals.

WAL and an unreviewed ESP32 SQLite port are rejected as production assumptions.
POSIX atomic rename/fsync only validates the desktop checkpoint. Future NVS
checkpoint wear/durability and SQLite FatFS VFS/power interruption testing are
gates. Memory buffers have bounded loss telemetry; they are not a durable spool.
If both authoritative state and fallback are unreadable, refuse to create a new
identity rather than destroying continuity.
