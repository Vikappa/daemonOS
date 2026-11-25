include(FetchContent)

function(daemon_archive name filename url hash)
  set(local "${PROJECT_SOURCE_DIR}/.cache/downloads/${filename}")
  if(EXISTS "${local}")
    set(url "${local}")
  endif()
  FetchContent_Declare(${name} URL "${url}" URL_HASH "SHA256=${hash}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endfunction()

function(daemon_sqlite_dependency)
  daemon_archive(sqlite sqlite-amalgamation-3500400.zip
    https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
    1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032)
  FetchContent_MakeAvailable(sqlite)
  add_library(sqlite3 STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
  target_include_directories(sqlite3 PUBLIC ${sqlite_SOURCE_DIR})
  target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_CACHE_SIZE=-128 SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED)
  target_link_libraries(sqlite3 PRIVATE ${CMAKE_DL_LIBS} m)
endfunction()

function(daemon_gui_dependencies)
  if(DAEMON_SYSTEM_SDL)
    find_package(SDL2 2.30.0 EXACT REQUIRED CONFIG)
  else()
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    daemon_archive(sdl SDL2-2.30.0.tar.gz
      https://www.libsdl.org/release/SDL2-2.30.0.tar.gz
      36e2e41557e0fa4a1519315c0f5958a87ccb27e25c51776beb6f1239526447b0)
    FetchContent_MakeAvailable(sdl)
  endif()
  set(LV_CONF_PATH "${PROJECT_SOURCE_DIR}/simulator/lv_conf.h" CACHE STRING "" FORCE)
  set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
  set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
  set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
  daemon_archive(lvgl lvgl-v9.2.2.tar.gz
    https://codeload.github.com/lvgl/lvgl/tar.gz/refs/tags/v9.2.2
    129b4e00e06639fa79d7e8a6cab3c1ecce2445b1a246652ccd34f22e7b17ad6f)
  FetchContent_MakeAvailable(lvgl)
endfunction()
