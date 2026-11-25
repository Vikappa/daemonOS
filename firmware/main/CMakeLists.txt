idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." REQUIRES daemon_core daemon_presentation daemon_sqlite esp_timer esp_system esp_pm esp_psram nvs_flash EMBED_TXTFILES "../../config/agent.md")
target_compile_options(${COMPONENT_LIB} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-std=c++20>")
