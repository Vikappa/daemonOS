idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." REQUIRES daemon_core esp_timer esp_system)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=c++20)
