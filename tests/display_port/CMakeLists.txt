cmake_minimum_required(VERSION 3.22)
project(display_port_tests C)
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
enable_testing()

foreach(MODE RANGE 0 2)
    add_executable(ssd1685_${MODE} test_ssd1685.c
        "${REPO}/User/components/display/stm_ssd1685/ssd1685.c")
    target_include_directories(ssd1685_${MODE} PRIVATE mocks
        "${REPO}/User/components/display/stm_ssd1685")
    target_compile_definitions(ssd1685_${MODE} PRIVATE TEST_REFRESH_MODE=${MODE})
    add_test(NAME ssd1685_${MODE} COMMAND ssd1685_${MODE})
endforeach()

add_executable(knob test_knob.c "${REPO}/User/components/display/lvgl_port/lv_port_knob.c")
target_include_directories(knob PRIVATE mocks "${REPO}/User/components/display/lvgl_port")
add_test(NAME knob COMMAND knob)

add_executable(log_format test_log.c)
target_include_directories(log_format PRIVATE mocks "${REPO}/User/components/log")
add_test(NAME log_format COMMAND log_format)

foreach(TARGET ssd1685_0 ssd1685_1 ssd1685_2 knob log_format)
    target_compile_features(${TARGET} PRIVATE c_std_11)
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endforeach()
