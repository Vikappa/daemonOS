if(CMAKE_BUILD_EARLY_EXPANSION)
  idf_component_register(REQUIRES esp_system esp_timer freertos)
  return()
endif()
set(DAEMON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
include(FetchContent)
set(ARCHIVE "${DAEMON_ROOT}/.cache/downloads/sqlite-amalgamation-3500400.zip")
if(NOT EXISTS "${ARCHIVE}")
  set(ARCHIVE "https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip")
endif()
FetchContent_Declare(daemon_sqlite URL "${ARCHIVE}"
  URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_GetProperties(daemon_sqlite)
if(NOT daemon_sqlite_POPULATED)
  FetchContent_Populate(daemon_sqlite)
endif()
idf_component_register(SRCS "${daemon_sqlite_SOURCE_DIR}/sqlite3.c" "unsupported_vfs.c"
  INCLUDE_DIRS "${daemon_sqlite_SOURCE_DIR}" REQUIRES esp_system esp_timer freertos)
target_compile_definitions(${COMPONENT_LIB} PRIVATE SQLITE_OS_OTHER=1 SQLITE_THREADSAFE=0
  SQLITE_TEMP_STORE=3 SQLITE_DEFAULT_CACHE_SIZE=-32 SQLITE_DEFAULT_MEMSTATUS=0
  SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED SQLITE_ENABLE_MEMSYS5
  SQLITE_OMIT_WAL SQLITE_OMIT_JSON)
# Upstream SQLITE_OMIT_WAL deliberately replaces a statement with literal 0.
# Keep this suppression confined to the unmodified third-party component.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-value)
