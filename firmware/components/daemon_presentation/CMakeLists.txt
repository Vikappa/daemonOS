if(CMAKE_BUILD_EARLY_EXPANSION)
  idf_component_register(REQUIRES daemon_core lvgl)
  return()
endif()
set(DAEMON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(SPRITE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/ember_pixels.hpp")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(OUTPUT "${SPRITE_HEADER}"
  COMMAND "${PYTHON}" "${DAEMON_ROOT}/tools/compile_sprite.py" "${DAEMON_ROOT}/assets/sprites/ember.sprite" "${SPRITE_HEADER}"
  DEPENDS "${DAEMON_ROOT}/assets/sprites/ember.sprite" "${DAEMON_ROOT}/tools/compile_sprite.py")
idf_component_register(SRCS "${DAEMON_ROOT}/presentation/watch_view.cpp" "${DAEMON_ROOT}/presentation/assets.cpp" "${SPRITE_HEADER}"
  INCLUDE_DIRS "${DAEMON_ROOT}/presentation/include" PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/generated"
  REQUIRES daemon_core lvgl)
target_compile_options(${COMPONENT_LIB} PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:-std=c++20>")
