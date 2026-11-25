# Local persistence and recovery

The watch owns authoritative state. Desktop paths emulate two independent media:
`sd/creature.sqlite` for identity, state, memories and schema; `flash/identity.checkpoint`
for identity, vital state and personality only. Settings/history are not in the
minimal checkpoint. It is not a full backup.

SQLite 3.50.4 is compiled locally. Native POSIX VFS, foreign keys, prepared queries,
DELETE journaling, FULL synchronization, 100 ms busy timeout and a 128 KiB cache
are used. WAL is deliberately not assumed safe on FatFS. Each save atomically
writes creature, needs, personality, settings and an optional memory. On failure,
RAII rolls back and queued memories stay in RAM. A batch can partially drain in
separate complete transactions; duplicate execution is not retried after success.
The pending queue holds 16 experiences and drops new ones with telemetry when full.

Migrations 1 and 2 create normalized state plus memory/conversation/relationship/
event/intention/knowledge/settings entities and retrieval indexes. Only state,
settings and memory APIs are exercised in Genesis; the other tables reserve a
documented schema for later services. Each migration is transactional and recorded
in `schema_migrations`. A newer schema is rejected without overwriting it.
`PRAGMA quick_check(1)` runs at open; it detects structural corruption, not all
logical inconsistencies or filesystem failures.

Memory retrieval returns at most eight records and ranks 60% importance, 40%
recency relative to the creature timestamp, with deterministic ties. Content,
tags and entities use literal case-insensitive substring filtering. There are no
mandatory embeddings or vector database. Offset <=10000. Output RAM is bounded. Habitat also interrupts retrieval after approximately
100,000 SQLite VM instructions; an exhausted query returns a budget/busy error
and an empty result. Indexed full-text search and retention policies remain
necessary for large histories. Recall frequency, last-recall, summary and memory-link fields are
stored, but recall counters and automatic summarization are not updated yet.

Live state uses monotonic time; persisted wall time supports offline catch-up.
Saves occur every virtual minute and orderly shutdown. Checkpoints are written
at boot, every five virtual minutes and orderly shutdown. Sudden host termination
can lose up to one minute of state and queued experiences; the minimal fallback
can be up to five minutes old. Repeated reboots still cause writes; production
NVS wear-aware A/B slots and brownout policy remain to be designed.

The desktop checkpoint is a versioned, length-limited text encoding with CRC32.
It is written using a same-directory temporary file, fsync, atomic rename and
parent-directory fsync. This POSIX property is not a claim about SD or NVS.
CRC detects accidental corruption, not malicious tampering or confidentiality.

## Degraded state

The lab's storage fault makes all database operations fail through the store
adapter. Simulation and local interactions continue; state remains in RAM and
the independent checkpoint continues. Clearing the injected fault drains queued
experiences. A physically replaced or corrupt card requires restart/recovery;
the toggle does not reopen a different database. Persistent failures remain
visible. Failed database shutdown writes still preserve a successful checkpoint.

At startup, a readable database is authoritative. If it is unavailable or damaged,
a valid checkpoint preserves identity and vital state. Corrupt source files are
never automatically deleted or reformatted. Without any prior database state,
a new identity can be created. Unreadable existing state without a valid
checkpoint causes boot failure, not a silent identity reset. A locked database
or newer schema also fails boot. Checkpoint recovery can lose autobiographical
history; keep complete exports separately.

## Backup, transfer and explicit recovery

`daemon_sim --backup /safe/location/ember.sqlite` performs a consistent SQLite
`VACUUM INTO` export after flushing. Existing destinations are refused. Verify
the export with `PRAGMA integrity_check` on another connection. An export contains
all creature tables and can transfer the creature to another simulator/device
with a compatible validated store. No remote account is involved.

1. Stop the simulator/device. Preserve the damaged SD directory, including journal
   files, for diagnosis. Never remove hot journals from a live database.
2. For full recovery, put a verified export at `sd/creature.sqlite` in a **new**
   data directory and start with `--data-dir` pointing to it. A new minimal
   checkpoint is produced from the exported identity.
3. For minimal recovery, copy only a verified `flash/identity.checkpoint` into a
   **new** data directory, leaving `sd` absent. Startup reconstructs local SQLite
   from the checkpoint. History cannot be reconstructed from this file.
4. Verify identity and state, then preserve both old media and the export until
   the recovery has been reviewed. No command here destroys the original.

No in-place import or automatic salvage tool is implemented. Treat exported
memories as personal data; neither database nor backup is encrypted in Genesis.

## Embedded SQLite gate

ESP-IDF exposes FatFS through a POSIX-like VFS, but this does not prove SQLite
locking or sync semantics. Before enabling it, implement/review `xLock`,
`xUnlock`, `xSync`, `xDelete`, sector-size/device characteristics and open-file
limits; serialize one writer; test interrupted journal writes, media removal,
brownouts, repeated fsync and filesystem repair across supported SD cards.
Measure code size, stack/heap, cache placement, latency and write energy. Do not
claim ACID durability on hardware until those tests pass. The portable store
contract allows a journaled key-value alternative if SQLite fails that gate.

Sources: [SQLite locking](https://www.sqlite.org/lockingv3.html),
[SQLite corruption modes](https://www.sqlite.org/howtocorrupt.html),
[ESP-IDF FatFS VFS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/fatfs.html).

## Habitat embedded readiness and memory caps

The desktop simulator gives SQLite a fixed 1 MiB memsys5 arena from its budgeted
allocator before initialization; cache is still 128 KiB. Unit tests retain native
allocation for independent persistence validation. Query failures never expose
partial memory pages. Memory pressure requests native cache release; the arena
remains reserved while its owner exists.

ESP32-S3 now cross-builds SQLite 3.50.4 with OS_OTHER, a serialized owner, 32 KiB
cache and a fixed 192 KiB PSRAM heap. Its VFS deliberately rejects persistent
files; a compiled memory-only readiness probe must not be described as an executed
hardware test. WAL, JSON extensions and loadable extensions are omitted. A real
FatFS VFS, locking/sync and power interruption remain unimplemented gates.

The firmware uses NVS A/B minimum records (version, ABI size, monotonic record
sequence and CRC covering header/state/settings) for identity/vitals/personality
and small settings. These records are compiled but not tested on hardware and
are not compatible with the desktop text checkpoint format. No old ABI migration,
portable checkpoint import, encryption or measured wear guarantee exists yet.
Full history/query/export calls return unsupported. NVS initialization failures
never trigger automatic erasure.

At a technical reboot or a depleted-battery suspension, catch-up preserves
previous health while other needs and age progress under the seven-day cap.
This avoids punishing a hardware problem as neglect. Physical RTC continuity
and safe wake sources still need board integration.
