cmake_minimum_required(VERSION 3.24)
project(DaemonOS VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(DAEMON_BUILD_SIMULATOR "Build LVGL/SDL desktop UI" ON)
option(DAEMON_SANITIZE "Enable address and undefined behavior sanitizers" OFF)
option(DAEMON_SYSTEM_SDL "Use exactly SDL 2.30.0 from the system" ON)
option(DAEMON_STATIC_ANALYSIS "Enable GCC static analyzer for first-party C++" OFF)
include(CTest)
include(cmake/Dependencies.cmake)

add_library(daemon_options INTERFACE)
target_compile_options(daemon_options INTERFACE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Werror>)
if(DAEMON_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
if(DAEMON_STATIC_ANALYSIS)
  target_compile_options(daemon_options INTERFACE -fanalyzer)
endif()

add_library(daemon_core
  core/framework/runtime.cpp
  core/framework/application.cpp
  core/services/creature.cpp
  core/services/cognitive.cpp
  core/apps/launcher/launcher.cpp
  core/apps/clock/clock.cpp
  core/apps/settings/settings.cpp
  core/apps/creature/creature_app.cpp)
target_include_directories(daemon_core PUBLIC core/include)
target_link_libraries(daemon_core PRIVATE daemon_options)

daemon_sqlite_dependency()
add_library(daemon_desktop
  core/platform/desktop/desktop.cpp
  core/platform/desktop/sqlite_store.cpp)
target_include_directories(daemon_desktop PUBLIC core/include)
target_link_libraries(daemon_desktop PUBLIC daemon_core PRIVATE sqlite3 daemon_options)
add_executable(daemon_resources tools/resources.cpp)
target_link_libraries(daemon_resources PRIVATE daemon_core daemon_options)

if(DAEMON_BUILD_SIMULATOR)
  daemon_gui_dependencies()
  add_executable(daemon_sim simulator/main.cpp simulator/ui.cpp simulator/sdl_hal.cpp)
  target_include_directories(daemon_sim PRIVATE simulator)
  target_link_libraries(daemon_sim PRIVATE daemon_desktop lvgl SDL2::SDL2 daemon_options)
  target_compile_definitions(daemon_sim PRIVATE DAEMON_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets")
endif()

if(BUILD_TESTING)
  add_executable(daemon_tests tests/test_main.cpp)
  target_link_libraries(daemon_tests PRIVATE daemon_desktop sqlite3 daemon_options)
  add_test(NAME domain_and_integration COMMAND daemon_tests)
  if(DAEMON_BUILD_SIMULATOR)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME simulator_restart COMMAND ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/tests/integration/simulator_restart.py $<TARGET_FILE:daemon_sim>)
    set_tests_properties(simulator_restart PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 30)
  endif()
endif()
