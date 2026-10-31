/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Ported to STM32 HAL.
 */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lv_port_encoder.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

typedef struct {
    knob_handle_t knob;
    button_handle_t button;
    QueueHandle_t button_events;
    int32_t last_count;
    int32_t diff;
    bool pressed;
} lvgl_port_encoder_ctx_t;

static void encoder_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    lvgl_port_encoder_ctx_t *ctx = lv_indev_get_driver_data(indev);
    taskENTER_CRITICAL();
    data->enc_diff = ctx->diff;
    ctx->diff = 0;
    taskEXIT_CRITICAL();
    /* 先消费积累的旋转，再消费按键边沿；短按不会在 BUSY 期间丢失。 */
    if(data->enc_diff == 0)
        xQueueReceive(ctx->button_events, &ctx->pressed, 0);
    data->state = ctx->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = uxQueueMessagesWaiting(ctx->button_events) != 0;
}

static void encoder_button(button_handle_t button, void *user_data)
{
    lvgl_port_encoder_ctx_t *ctx = user_data;
    bool pressed = iot_button_get_event(button) == BUTTON_PRESS_DOWN;
    xQueueSend(ctx->button_events, &pressed, 0);
}

static void encoder_rotate(void *knob, void *user_data)
{
    lvgl_port_encoder_ctx_t *ctx = user_data;
    int32_t count = iot_knob_get_count_value(knob);
    taskENTER_CRITICAL();
    ctx->diff += count - ctx->last_count;
    ctx->last_count = count;
    taskEXIT_CRITICAL();
}

lv_indev_t *lvgl_port_add_encoder(const lvgl_port_encoder_cfg_t *config)
{
    assert(config && config->disp);
    lvgl_port_encoder_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if(!ctx) return NULL;
    ctx->button_events = xQueueCreate(8, sizeof(bool));
    if(!ctx->button_events) { free(ctx); return NULL; }
    if(config->encoder_a_b)
    {
        ctx->knob = iot_knob_create(config->encoder_a_b);
        if(!ctx->knob) { vQueueDelete(ctx->button_events); free(ctx); return NULL; }
        iot_knob_register_cb(ctx->knob, KNOB_LEFT, encoder_rotate, ctx);
        iot_knob_register_cb(ctx->knob, KNOB_RIGHT, encoder_rotate, ctx);
    }
    ctx->button = config->encoder_enter;
    if(ctx->button)
    {
        iot_button_register_cb(ctx->button, BUTTON_PRESS_DOWN, NULL, encoder_button, ctx);
        iot_button_register_cb(ctx->button, BUTTON_PRESS_UP, NULL, encoder_button, ctx);
    }
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(indev, encoder_read);
    lv_indev_set_disp(indev, config->disp);
    lv_indev_set_driver_data(indev, ctx);
    lv_timer_set_period(lv_indev_get_read_timer(indev), 10);
    return indev;
}

int lvgl_port_remove_encoder(lv_indev_t *indev)
{
    assert(indev);
    lvgl_port_encoder_ctx_t *ctx = lv_indev_get_driver_data(indev);
    if(ctx->knob) iot_knob_delete(ctx->knob);
    if(ctx->button)
    {
        iot_button_unregister_cb(ctx->button, BUTTON_PRESS_DOWN, NULL);
        iot_button_unregister_cb(ctx->button, BUTTON_PRESS_UP, NULL);
    }
    lv_indev_delete(indev);
    vQueueDelete(ctx->button_events);
    free(ctx);
    return 0;
}
