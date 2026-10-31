file(GLOB COMPONENT_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/log/*.c
        ${CMAKE_CURRENT_LIST_DIR}/display/*.c
        ${CMAKE_CURRENT_LIST_DIR}/display/stm_ssd1685/*.c
        ${CMAKE_CURRENT_LIST_DIR}/display/lvgl_port/*.c
        ${CMAKE_CURRENT_LIST_DIR}/es9018/*.c
        ${CMAKE_CURRENT_LIST_DIR}/main/*.c
        ${CMAKE_CURRENT_LIST_DIR}/fatfs/*.c
)

add_library(user_components INTERFACE)
target_sources(user_components INTERFACE ${COMPONENT_SOURCES})

target_include_directories(user_components INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/log
        ${CMAKE_CURRENT_LIST_DIR}/display
        ${CMAKE_CURRENT_LIST_DIR}/display/stm_ssd1685
        ${CMAKE_CURRENT_LIST_DIR}/display/lvgl_port
        ${CMAKE_CURRENT_LIST_DIR}/es9018
        ${CMAKE_CURRENT_LIST_DIR}/fatfs

)
