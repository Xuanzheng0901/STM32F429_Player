file(GLOB COMPONENT_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/log/*.c
        ${CMAKE_CURRENT_LIST_DIR}/display/*.c
        ${CMAKE_CURRENT_LIST_DIR}/display/stm_ssd1685/*.c
        ${CMAKE_CURRENT_LIST_DIR}/display/lvgl_port/*.c
        ${CMAKE_CURRENT_LIST_DIR}/es9018/*.c
        ${CMAKE_CURRENT_LIST_DIR}/main/*.c
        ${CMAKE_CURRENT_LIST_DIR}/fatfs/*.c
        ${CMAKE_CURRENT_LIST_DIR}/sdcard/*.c
        ${CMAKE_CURRENT_LIST_DIR}/player/*.c
        ${CMAKE_CURRENT_LIST_DIR}/decoder/*.c
        ${CMAKE_CURRENT_LIST_DIR}/audio/*.c
        ${CMAKE_CURRENT_LIST_DIR}/UI/*.c
)

# 只给实时解码器和文件音频输出单独优化。
set_source_files_properties(${COMPONENT_SOURCES}
    TARGET_DIRECTORY ${CMAKE_PROJECT_NAME}
    PROPERTIES COMPILE_OPTIONS "-fstack-usage")
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/decoder/mp3_decoder.c
    TARGET_DIRECTORY ${CMAKE_PROJECT_NAME}
    PROPERTIES COMPILE_OPTIONS "-Os;-fstack-usage;-fno-strict-aliasing")
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/decoder/flac_decoder.c
    TARGET_DIRECTORY ${CMAKE_PROJECT_NAME}
    PROPERTIES COMPILE_OPTIONS "-O2;-fstack-usage;-fno-strict-aliasing")

set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/audio/audio_player.c
    TARGET_DIRECTORY ${CMAKE_PROJECT_NAME}
    PROPERTIES COMPILE_OPTIONS "-O2;-fstack-usage")

add_library(user_components INTERFACE)
target_sources(user_components INTERFACE ${COMPONENT_SOURCES})

target_include_directories(user_components INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/log
        ${CMAKE_CURRENT_LIST_DIR}/display
        ${CMAKE_CURRENT_LIST_DIR}/display/stm_ssd1685
        ${CMAKE_CURRENT_LIST_DIR}/display/lvgl_port
        ${CMAKE_CURRENT_LIST_DIR}/es9018
        ${CMAKE_CURRENT_LIST_DIR}/fatfs
        ${CMAKE_CURRENT_LIST_DIR}/sdcard
        ${CMAKE_CURRENT_LIST_DIR}/player
        ${CMAKE_CURRENT_LIST_DIR}/UI
        ${CMAKE_CURRENT_LIST_DIR}/decoder
        ${CMAKE_CURRENT_LIST_DIR}/audio

)
