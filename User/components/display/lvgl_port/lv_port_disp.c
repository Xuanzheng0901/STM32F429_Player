#include "lv_port_disp.h"
#include "display.h"
#include "main.h"
#include "log.h"
#include "lv_port_encoder.h"
#include "tim.h"

static SemaphoreHandle_t xGuiSemaphore;
static bool disp_flush_enabled = true;
static _Alignas(4) uint8_t disp_buf1[DISP_BUF_SIZE];

void disp_enable_update(void)
{
    disp_flush_enabled = true;
    lv_obj_invalidate(lv_screen_active());
}

void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if(disp_flush_enabled)
    {
        (void)area;
        display_draw(px_map + 8, DISP_HOR_RES / 8);
    }
    lv_display_flush_ready(disp);
}

bool lvgl_port_lock(uint32_t timeout_ms)
{
    if(xGuiSemaphore == NULL)
        return false;
    TickType_t ticks = timeout_ms == portMAX_DELAY ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(xGuiSemaphore, ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    xSemaphoreGive(xGuiSemaphore);
}

static void lvgl_task(void *argument)
{
    (void)argument;
    LOGI("LVGL", "Display task running");
    while(1)
    {
        lvgl_port_lock(portMAX_DELAY);
        uint32_t wait_ms = lv_timer_handler();
        lvgl_port_unlock();
        if(wait_ms > LV_DEF_REFR_PERIOD)
            wait_ms = LV_DEF_REFR_PERIOD;
        vTaskDelay(pdMS_TO_TICKS(wait_ms) + 1);
    }
}

void lv_port_disp_init(void)
{
    display_init();
    xGuiSemaphore = xSemaphoreCreateMutex();
    LOGD("LVGL", "Heap before lv_init: %u bytes", (unsigned)xPortGetFreeHeapSize());
    lv_init();
    LOGD("LVGL", "Heap after lv_init: %u bytes", (unsigned)xPortGetFreeHeapSize());
    lv_tick_set_cb(HAL_GetTick);
    lv_display_t *disp = lv_display_create(DISP_HOR_RES, DISP_VER_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);
    /* 墨水屏使用单缓冲；flush 等待发送与刷新完成后才交回 LVGL 复用。 */
    lv_display_set_buffers(disp, disp_buf1, NULL, DISP_BUF_SIZE, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, disp_flush);

    if(xTaskCreate(lvgl_task, "LVGL", 1024, NULL, 6, NULL) == pdPASS)
        LOGI("LVGL", "Display initialized, heap free: %u bytes", (unsigned)xPortGetFreeHeapSize());
    else
        LOGE("LVGL", "Display task creation failed");
}
