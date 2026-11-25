set(DAEMON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
file(READ "${DAEMON_ROOT}/assets/personality/agent.md" DAEMON_AGENT_PROFILE)
configure_file("${DAEMON_ROOT}/cmake/agent_profile.hpp.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/daemon/agent_profile.hpp" @ONLY)
idf_component_register(
  SRCS
    "${DAEMON_ROOT}/core/framework/runtime.cpp"
    "${DAEMON_ROOT}/core/framework/application.cpp"
    "${DAEMON_ROOT}/core/services/creature.cpp"
    "${DAEMON_ROOT}/core/services/cognitive.cpp"
    "${DAEMON_ROOT}/core/services/assistant.cpp"
    "${DAEMON_ROOT}/core/services/bridge.cpp"
    "${DAEMON_ROOT}/core/services/agent_scheduler.cpp"
    "${DAEMON_ROOT}/core/services/speech_motion.cpp"
    "${DAEMON_ROOT}/core/services/voice_directory.cpp"
    "${DAEMON_ROOT}/core/services/power.cpp"
    "${DAEMON_ROOT}/core/services/resources.cpp"
    "${DAEMON_ROOT}/core/apps/launcher/launcher.cpp"
    "${DAEMON_ROOT}/core/apps/clock/clock.cpp"
    "${DAEMON_ROOT}/core/apps/settings/settings.cpp"
    "${DAEMON_ROOT}/core/apps/creature/creature_app.cpp"
  INCLUDE_DIRS "${DAEMON_ROOT}/core/include" PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(${COMPONENT_LIB} PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:-std=c++20>")
