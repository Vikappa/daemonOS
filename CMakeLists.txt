cmake_minimum_required(VERSION 3.24)
project(DaemonOS VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(DAEMON_BUILD_SIMULATOR "Build LVGL/SDL desktop UI" ON)
option(DAEMON_SANITIZE "Enable address and undefined behavior sanitizers" OFF)
option(DAEMON_SYSTEM_SDL "Use exactly SDL 2.30.0 from the system" ON)
option(DAEMON_STATIC_ANALYSIS "Run Clang static analysis after building the portable core" OFF)
include(CTest)
include(cmake/Dependencies.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS assets/personality/agent.md)
file(READ "${PROJECT_SOURCE_DIR}/assets/personality/agent.md" DAEMON_AGENT_PROFILE)
string(LENGTH "${DAEMON_AGENT_PROFILE}" DAEMON_AGENT_PROFILE_SIZE)
if(DAEMON_AGENT_PROFILE_SIZE GREATER 1800)
  message(FATAL_ERROR "Personality profile exceeds its 1800-byte context budget")
endif()
configure_file(cmake/agent_profile.hpp.in generated/daemon/agent_profile.hpp @ONLY)

add_library(daemon_options INTERFACE)
target_compile_options(daemon_options INTERFACE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Werror>)
if(DAEMON_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(daemon_core
  core/framework/runtime.cpp
  core/framework/application.cpp
  core/services/creature.cpp
  core/services/cognitive.cpp
  core/services/assistant.cpp
  core/services/bridge.cpp
  core/services/agent_scheduler.cpp
  core/services/speech_motion.cpp
  core/services/voice_directory.cpp
  core/services/power.cpp
  core/services/resources.cpp
  core/apps/launcher/launcher.cpp
  core/apps/clock/clock.cpp
  core/apps/settings/settings.cpp
  core/apps/creature/creature_app.cpp)
target_include_directories(daemon_core PUBLIC core/include PRIVATE "${PROJECT_BINARY_DIR}/generated")
target_link_libraries(daemon_core PRIVATE daemon_options)
add_library(daemon_portrait presentation/ember_sprite.cpp)
target_include_directories(daemon_portrait PUBLIC presentation presentation/include)
target_link_libraries(daemon_portrait PUBLIC daemon_core PRIVATE daemon_options)
if(DAEMON_STATIC_ANALYSIS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  find_program(DAEMON_CLANG clang++ REQUIRED)
  add_custom_command(TARGET daemon_core POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/analyze.py
      --build-dir ${CMAKE_BINARY_DIR} --core-only --clang ${DAEMON_CLANG})
endif()

daemon_sqlite_dependency()
add_library(daemon_desktop
  core/platform/desktop/desktop.cpp
  core/platform/desktop/sqlite_store.cpp)
target_include_directories(daemon_desktop PUBLIC core/include)
target_link_libraries(daemon_desktop PUBLIC daemon_core PRIVATE sqlite3 daemon_options)
add_executable(daemon_resources tools/resources.cpp)
target_link_libraries(daemon_resources PRIVATE daemon_core daemon_options)
add_executable(daemon_face_frames tools/face_frames.cpp)
target_link_libraries(daemon_face_frames PRIVATE daemon_portrait daemon_options)

if(DAEMON_BUILD_SIMULATOR)
  daemon_gui_dependencies()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_library(daemon_presentation presentation/watch_view.cpp)
  target_include_directories(daemon_presentation PUBLIC presentation/include presentation)
  target_link_libraries(daemon_presentation PUBLIC daemon_core lvgl daemon_portrait PRIVATE daemon_options)
  add_executable(daemon_sim simulator/main.cpp simulator/ui.cpp simulator/sdl_hal.cpp)
  target_include_directories(daemon_sim PRIVATE simulator)
  target_link_libraries(daemon_sim PRIVATE daemon_desktop daemon_presentation SDL2::SDL2 daemon_options)
  target_compile_definitions(daemon_sim PRIVATE DAEMON_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets")
  target_compile_definitions(daemon_sim PRIVATE DAEMON_DEFAULT_AGENT="${PROJECT_SOURCE_DIR}/config/agent.md")
endif()

if(BUILD_TESTING)
  add_executable(daemon_tests tests/test_main.cpp)
  target_link_libraries(daemon_tests PRIVATE daemon_desktop sqlite3 daemon_options)
  add_test(NAME domain_and_integration COMMAND daemon_tests)
  add_executable(daemon_assistant_tests tests/assistant_tests.cpp)
  target_link_libraries(daemon_assistant_tests PRIVATE daemon_desktop sqlite3 daemon_options)
  add_test(NAME deferred_tools COMMAND daemon_assistant_tests)
  add_executable(daemon_agent_tests tests/agent_scheduler_tests.cpp)
  target_link_libraries(daemon_agent_tests PRIVATE daemon_desktop sqlite3 daemon_options)
  add_test(NAME agent_scheduler COMMAND daemon_agent_tests)
  add_executable(daemon_voice_tests tests/voice_directory_tests.cpp)
  target_link_libraries(daemon_voice_tests PRIVATE daemon_desktop sqlite3 daemon_options)
  add_test(NAME voice_identity_and_authorization COMMAND daemon_voice_tests)
  add_executable(daemon_face_tests tests/face_tests.cpp)
  target_link_libraries(daemon_face_tests PRIVATE daemon_portrait daemon_desktop daemon_options)
  add_test(NAME creature_faces_and_speech COMMAND daemon_face_tests)
  add_executable(habitat_tests tests/habitat_tests.cpp)
  target_link_libraries(habitat_tests PRIVATE daemon_desktop sqlite3 daemon_options)
  add_test(NAME habitat COMMAND habitat_tests)
  if(DAEMON_BUILD_SIMULATOR)
    target_link_libraries(habitat_tests PRIVATE daemon_presentation)
    target_compile_definitions(habitat_tests PRIVATE DAEMON_PRESENTATION_TESTS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME simulator_restart COMMAND ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/tests/integration/simulator_restart.py $<TARGET_FILE:daemon_sim>)
    set_tests_properties(simulator_restart PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 30)
    add_test(NAME habitat_simulator COMMAND ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/tests/integration/habitat_simulator.py $<TARGET_FILE:daemon_sim>)
    set_tests_properties(habitat_simulator PROPERTIES TIMEOUT 30)
  endif()
endif()
