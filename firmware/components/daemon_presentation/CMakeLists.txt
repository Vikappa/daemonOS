if(CMAKE_BUILD_EARLY_EXPANSION)
  idf_component_register(REQUIRES daemon_core lvgl)
  return()
endif()
set(DAEMON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
idf_component_register(SRCS "${DAEMON_ROOT}/presentation/watch_view.cpp" "${DAEMON_ROOT}/presentation/ember_sprite.cpp"
  INCLUDE_DIRS "${DAEMON_ROOT}/presentation/include"
  REQUIRES daemon_core lvgl)
target_compile_options(${COMPONENT_LIB} PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:-std=c++20>")
