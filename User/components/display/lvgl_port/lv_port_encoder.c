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
#include "timers.h"

typedef struct {
    knob_handle_t knob;
    button_handle_t button;
    QueueHandle_t button_events;
    int32_t last_count;
    int32_t diff;
    bool pressed;
    TimerHandle_t key_timer;

    void (*control_click)(unsigned index);

    void (*volume_step)(int direction);

    struct {
        uint8_t candidate, stable, count;
    } keys[8];
    uint8_t middle_ticks;
    bool middle_long;
    void (*middle_click)(void);
    void (*middle_long_press)(void);
} lvgl_port_encoder_ctx_t;

static void encoder_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    lvgl_port_encoder_ctx_t *ctx = lv_indev_get_driver_data(indev);
    taskENTER_CRITICAL();
    data->enc_diff = ctx->diff;
    ctx->diff = 0;
    taskEXIT_CRITICAL();
    /* 先消费积累的旋转，再消费按键边沿；短按不会在 BUSY 期间丢失。 */
    uint8_t event;
    if(data->enc_diff == 0 && xQueueReceive(ctx->button_events, &event, 0) == pdTRUE)
    {
        if(event < 2)
            ctx->pressed = event;
        else if(event <= 6)
        {
            if(ctx->control_click)
                ctx->control_click(event - 2U);
        }
        else if(event <= 8 && ctx->volume_step)
            ctx->volume_step(event == 8 ? 1 : -1);
        else if(event == 9 && ctx->middle_click)
            ctx->middle_click();
        else if(event == 10 && ctx->middle_long_press)
            ctx->middle_long_press();
    }
    data->state = ctx->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = uxQueueMessagesWaiting(ctx->button_events) != 0;
}

static void encoder_button(button_handle_t button, void *user_data)
{
    lvgl_port_encoder_ctx_t *ctx = user_data;
    uint8_t pressed = iot_button_get_event(button) == BUTTON_PRESS_DOWN;
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

static void encoder_keys(TimerHandle_t timer)
{
    lvgl_port_encoder_ctx_t *ctx = pvTimerGetTimerID(timer);
    /* 0/1 为编码器确认边沿，2..6 为五个 UI 按钮，7/8 为音量减/加。9/10 为中侧键短按/长按。 */
    static const struct {
        GPIO_TypeDef *port;
        uint16_t pin;
        uint8_t event;
    } pins[] = {
        {BUTTON_SIDE1_GPIO_Port, BUTTON_SIDE1_Pin, 7},
        {BUTTON_SIDE3_GPIO_Port, BUTTON_SIDE3_Pin, 8},
        {BUTTON_UP1_GPIO_Port, BUTTON_UP1_Pin, 6}, {BUTTON_UP2_GPIO_Port, BUTTON_UP2_Pin, 5},
        {BUTTON_UP3_GPIO_Port, BUTTON_UP3_Pin, 4}, {BUTTON_UP4_GPIO_Port, BUTTON_UP4_Pin, 3},
        {BUTTON_UP5_GPIO_Port, BUTTON_UP5_Pin, 2},
        {BUTTON_SIDE2_GPIO_Port, BUTTON_SIDE2_Pin, 9},
    };
    for(unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i)
    {
        uint8_t pressed = HAL_GPIO_ReadPin(pins[i].port, pins[i].pin) == GPIO_PIN_RESET;
        if(pressed != ctx->keys[i].candidate)
        {
            ctx->keys[i].candidate = pressed;
            ctx->keys[i].count = 0;
        }
        if(ctx->keys[i].count < 3)
            ++ctx->keys[i].count;
        if(ctx->keys[i].count != 3 || pressed == ctx->keys[i].stable)
            continue;
        ctx->keys[i].stable = pressed;
        if(i == 7)
        {
            if(pressed) { ctx->middle_ticks = 0; ctx->middle_long = false; }
            else if(!ctx->middle_long) xQueueSend(ctx->button_events, &pins[i].event, 0);
            continue;
        }
        if(pressed)
            xQueueSend(ctx->button_events, &pins[i].event, 0);
    }
    if(ctx->keys[7].stable && !ctx->middle_long && ++ctx->middle_ticks >= 80)
    {
        ctx->middle_long = true;
        const uint8_t event = 10;
        xQueueSend(ctx->button_events, &event, 0);
    }
}

lv_indev_t *lvgl_port_add_encoder(const lvgl_port_encoder_cfg_t *config)
{
    assert(config && config->disp);
    lvgl_port_encoder_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if(!ctx)
        return NULL;
    ctx->button_events = xQueueCreate(8, sizeof(uint8_t));
    if(!ctx->button_events)
    {
        free(ctx);
        return NULL;
    }
    if(config->encoder_a_b)
    {
        ctx->knob = iot_knob_create(config->encoder_a_b);
        if(!ctx->knob)
        {
            vQueueDelete(ctx->button_events);
            free(ctx);
            return NULL;
        }
        iot_knob_register_cb(ctx->knob, KNOB_LEFT, encoder_rotate, ctx);
        iot_knob_register_cb(ctx->knob, KNOB_RIGHT, encoder_rotate, ctx);
    }
    ctx->button = config->encoder_enter;
    if(ctx->button)
    {
        iot_button_register_cb(ctx->button, BUTTON_PRESS_DOWN, NULL, encoder_button, ctx);
        iot_button_register_cb(ctx->button, BUTTON_PRESS_UP, NULL, encoder_button, ctx);
    }
    ctx->control_click = config->control_click;
    ctx->volume_step = config->volume_step;
    ctx->middle_click = config->middle_click;
    ctx->middle_long_press = config->middle_long_press;
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(indev, encoder_read);
    lv_indev_set_disp(indev, config->disp);
    lv_indev_set_driver_data(indev, ctx);
    lv_timer_set_period(lv_indev_get_read_timer(indev), 10);
    ctx->key_timer = xTimerCreate("input_keys", pdMS_TO_TICKS(10), pdTRUE, ctx, encoder_keys);
    if(!ctx->key_timer || xTimerStart(ctx->key_timer, 0) != pdPASS)
    {
        lvgl_port_remove_encoder(indev);
        return NULL;
    }
    return indev;
}

int lvgl_port_remove_encoder(lv_indev_t *indev)
{
    assert(indev);
    lvgl_port_encoder_ctx_t *ctx = lv_indev_get_driver_data(indev);
    if(ctx->key_timer)
    {
        xTimerStop(ctx->key_timer, 0);
        xTimerDelete(ctx->key_timer, 0);
    }
    if(ctx->knob)
        iot_knob_delete(ctx->knob);
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
