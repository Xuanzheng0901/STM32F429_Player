# 此清单对应当前 lv_conf.h：软件绘制、label、image、自定义字体、FreeRTOS、内置标准库。
# 新增控件、内置字体、解码器或平台适配时，需同步调整这里的筛选条件。
# 保留上游 LVGL 的源文件顺序，不修改库内的 CMake 文件。
get_target_property(LVGL_SOURCES lvgl SOURCES)
list(LENGTH LVGL_SOURCES LVGL_SOURCE_COUNT_BEFORE)

# 显示和输入设备由 User/components 提供；只使用软件绘制后端。
list(FILTER LVGL_SOURCES EXCLUDE REGEX "/src/drivers/")
list(FILTER LVGL_SOURCES EXCLUDE REGEX
    "/src/draw/(dma2d|nema_gfx|nxp|opengles|renesas|sdl|vg_lite)/")

# 内置字体全部关闭，保留字体引擎和二进制字体加载接口。
list(FILTER LVGL_SOURCES EXCLUDE REGEX
    "/src/font/lv_font_(montserrat|simsun|source_han|dejavu|unscii)[^/]*[.]c$")

# 当前启用 label、image 和 observer；保留绘制流程使用的二进制图片解码器。
set(LVGL_UNUSED_SOURCES ${LVGL_SOURCES})
list(FILTER LVGL_UNUSED_SOURCES INCLUDE REGEX "/src/(widgets|others|libs)/")
list(FILTER LVGL_UNUSED_SOURCES EXCLUDE REGEX
    "/src/(widgets/(label|image)|others/observer|libs/bin_decoder)/")
list(REMOVE_ITEM LVGL_SOURCES ${LVGL_UNUSED_SOURCES})

# 保留通用 OS 接口、FreeRTOS 适配，以及内置 malloc/string/sprintf。
list(FILTER LVGL_SOURCES EXCLUDE REGEX
    "/src/osal/lv_(cmsis_rtos2|linux|mqx|os_none|pthread|rtthread|sdl2|windows)[.]c$")
list(FILTER LVGL_SOURCES EXCLUDE REGEX
    "/src/stdlib/(clib|micropython|rtthread|uefi)/")

set_property(TARGET lvgl PROPERTY SOURCES "${LVGL_SOURCES}")
list(LENGTH LVGL_SOURCES LVGL_SOURCE_COUNT_AFTER)
message(STATUS "LVGL source files: ${LVGL_SOURCE_COUNT_BEFORE} -> ${LVGL_SOURCE_COUNT_AFTER}")
