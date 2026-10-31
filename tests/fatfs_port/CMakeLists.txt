cmake_minimum_required(VERSION 3.22)
project(fatfs_port_tests C)
enable_testing()
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(PORT "${REPO}/User/components/fatfs")

set(PORT_SOURCES
    "${PORT}/diskio.c" "${PORT}/ff.c" "${PORT}/ffunicode.c"
    "${PORT}/ffsystem.c" "${PORT}/sdmmc.c")
set(METADATA_SOURCES
    "${REPO}/User/components/player/music_tags.c")
set(LIBRARY_SOURCES ${PORT_SOURCES} ${METADATA_SOURCES}
    "${REPO}/User/components/player/music_library.c")
set(FILESYSTEM_SOURCES ${LIBRARY_SOURCES} "${REPO}/User/components/sdcard/sdcard.c")
add_executable(removal test_removal.c mock_platform.c ${FILESYSTEM_SOURCES})
add_executable(diskio test_diskio.c mock_platform.c "${PORT}/diskio.c"
    "${PORT}/sdmmc.c")
add_executable(filesystem test_filesystem.c mock_platform.c ${FILESYSTEM_SOURCES})
add_executable(tags test_tags.c mock_platform.c ${PORT_SOURCES} ${METADATA_SOURCES})
add_executable(library test_library.c mock_platform.c ${LIBRARY_SOURCES})
foreach(TEST diskio filesystem removal tags library)
    target_include_directories(${TEST} PRIVATE mocks "${PORT}"
        "${REPO}/User/components/audio" "${REPO}/User/components/sdcard" "${REPO}/User/components/player" "${REPO}/User/components/audio" "${REPO}/User/components/decoder")
    target_compile_features(${TEST} PRIVATE c_std_11)
    target_compile_options(${TEST} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME ${TEST} COMMAND ${TEST})
endforeach()

add_test(NAME removal_paused COMMAND removal paused)
add_test(NAME removal_quick COMMAND removal quick)
