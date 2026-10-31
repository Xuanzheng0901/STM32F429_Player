#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H
#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

// 初始化显示端口
void lv_port_disp_init(void);

//启用刷新并使全屏失效；调用前持有 lvgl_port_lock
void disp_enable_update(void);

// 暂停刷新；显示端口初始化前可直接调用，初始化后需持有 lvgl_port_lock
void disp_disable_update(void);

bool lvgl_port_lock(uint32_t timeout_ms);

void lvgl_port_unlock(void);

#endif
