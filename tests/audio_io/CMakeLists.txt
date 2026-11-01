cmake_minimum_required(VERSION 3.22)
project(audio_io_tests C)
enable_testing()
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(PLAYER "${REPO}/User/components/player")
add_executable(audio_io test_io.c "${REPO}/User/components/audio/audio_dma.c")
target_include_directories(audio_io PRIVATE mocks ../audio_player/mocks "${PLAYER}" "${REPO}/User/components/audio"
    "${REPO}/User/components/fatfs")
target_compile_options(audio_io PRIVATE -Wall -Wextra -Werror -UNDEBUG)
foreach(MODE transport faults drift restart headphone)
    add_test(NAME ${MODE} COMMAND audio_io ${MODE})
endforeach()

set(USB "${REPO}/Middlewares/ST/STM32_USB_Device_Library")
add_executable(usb_class test_class.c "${USB}/Class/AUDIO/Src/usbd_audio.c"
    "${REPO}/USB_DEVICE/App/usbd_audio_if.c")
target_include_directories(usb_class PRIVATE mocks "${USB}/Core/Inc"
    "${USB}/Class/AUDIO/Inc" "${REPO}/USB_DEVICE/App" "${PLAYER}" "${REPO}/User/components/audio")
target_compile_options(usb_class PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME usb_class COMMAND usb_class)
