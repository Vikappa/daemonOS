# ADR 0007: cross-build evidence without invented physical drivers

Status: accepted, 2026-11-25.

Pin ESP-IDF v5.5.1 and its manifest-verified tools in the project cache. Compile
the complete runtime and shared presentation with Xtensa GCC 14.2.0, exceptions
and RTTI disabled. Require `esp_psram` explicitly so its Kconfig and external BSS
attributes exist. Verify placement through the map, not just sdkconfig text.

Use a 16 MiB partition layout with dual 4 MiB OTA slots. Link FreeRTOS queue
blocking, tickless idle, dynamic frequency support and automatic light sleep.
Physical display/input/radio/PMIC/deep-sleep operations remain unsupported until
verified. Do not erase NVS to get past an initialization error.

Compile SQLite with its own OS_OTHER VFS, a fixed 192 KiB PSRAM allocator, no WAL
or loadable extensions and memory-only probe. Persistent files return CANTOPEN.
This proves toolchain/link compatibility, not FatFS locking or durability. NVS
A/B records supply minimum identity/vitals/settings; they do not pretend to be
the full autobiographical database. A future real VFS needs interruption tests.

Disable IDF's evaluation of NDEBUG assertions because SQLite references debug-only
variables inside assertions. Keep live application/SDK assertions. Suppress only
the third-party `unused-value` warning produced by omitted-WAL macros.

For C++ static analysis use Clang 18. GCC 13 `-fanalyzer` reported uninitialized
return objects despite initialized move-only RAII handles; its official manual
limits that release to C. No first-party analyzer warning is suppressed. Clang
checks core, shared presentation and adapters using real compilation flags.
Source: [GCC 13 analyzer scope](https://gcc.gnu.org/onlinedocs/gcc-13.3.0/gcc/Static-Analyzer-Options.html).
