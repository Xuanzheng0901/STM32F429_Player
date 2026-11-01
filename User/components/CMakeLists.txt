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
        ${CMAKE_CURRENT_LIST_DIR}/battery/*.c
        ${CMAKE_CURRENT_LIST_DIR}/UI/*.c
)

# 只给实时解码器和 PCM 输出单独优化。
option(PLAYER_AUDIO_O3 "Use -O3 for real-time audio processing" OFF)
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
    ${CMAKE_CURRENT_LIST_DIR}/audio/usb_audio.c
    TARGET_DIRECTORY ${CMAKE_PROJECT_NAME}
    PROPERTIES COMPILE_OPTIONS "-O2;-fstack-usage")

if(PLAYER_AUDIO_O3)
    # 最后追加 -O3 覆盖原优化级别，保留栈报告和解码器的别名兼容选项。
    set_property(SOURCE
        ${CMAKE_CURRENT_LIST_DIR}/decoder/flac_decoder.c
        ${CMAKE_CURRENT_LIST_DIR}/decoder/mp3_decoder.c
        ${CMAKE_CURRENT_LIST_DIR}/decoder/file_decoder.c
        ${CMAKE_CURRENT_LIST_DIR}/audio/audio_player.c
        ${CMAKE_CURRENT_LIST_DIR}/audio/usb_audio.c
        TARGET_DIRECTORY ${CMAKE_PROJECT_NAME}
        APPEND PROPERTY COMPILE_OPTIONS -O3)
endif()

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
        ${CMAKE_CURRENT_LIST_DIR}/battery

)
