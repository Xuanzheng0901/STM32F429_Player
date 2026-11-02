/*
* SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ported to STM32 HAL by Gemini
 */
#ifndef STM32_ENCODER_H
#define STM32_ENCODER_H

#include "lvgl.h"
#include "lv_port_knob.h"
#include "lv_port_button.h"

// LVGL 旋钮和按键的配置
typedef struct {
    lv_disp_t *disp; // LVGL 显示设备句柄
    const knob_config_t *encoder_a_b; // 旋钮配置
    button_handle_t encoder_enter; // 按键句柄 (外部创建并传入)
    void (*control_click)(unsigned index); /* 上方五键，回调在 LVGL 读取输入时执行。 */
    void (*volume_step)(int direction); /* 侧键：+1 增大、-1 减小音量，在 LVGL 读取时执行。 */
} lvgl_port_encoder_cfg_t;


// 添加编码器作为 LVGL 输入设备
lv_indev_t *lvgl_port_add_encoder(const lvgl_port_encoder_cfg_t *encoder_cfg);

// 移除编码器
int lvgl_port_remove_encoder(lv_indev_t *encoder);

#endif //STM32_ENCODER_H
