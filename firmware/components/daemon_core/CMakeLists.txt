set(DAEMON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
idf_component_register(
  SRCS
    "${DAEMON_ROOT}/core/framework/runtime.cpp"
    "${DAEMON_ROOT}/core/framework/application.cpp"
    "${DAEMON_ROOT}/core/services/creature.cpp"
    "${DAEMON_ROOT}/core/services/cognitive.cpp"
    "${DAEMON_ROOT}/core/apps/launcher/launcher.cpp"
    "${DAEMON_ROOT}/core/apps/clock/clock.cpp"
    "${DAEMON_ROOT}/core/apps/settings/settings.cpp"
    "${DAEMON_ROOT}/core/apps/creature/creature_app.cpp"
  INCLUDE_DIRS "${DAEMON_ROOT}/core/include")
target_compile_options(${COMPONENT_LIB} PUBLIC -std=c++20)
